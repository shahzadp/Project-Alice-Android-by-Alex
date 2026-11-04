// window_android.cpp
//
// Android implementation of the interface declared in window.hpp -- no GLFW.
//
// Lifecycle (android_native_app_glue -> handle_cmd):
//   APP_CMD_INIT_WINDOW  -> creates the EGL display/context (only the first time) and the
//                           surface for the new ANativeWindow
//   APP_CMD_TERM_WINDOW  -> destroys only the surface; the context (and with it the
//                           game textures/shaders) stays alive
//   APP_CMD_RESUME/PAUSE -> turns the render loop on/off
//   APP_CMD_GAINED/LOST_FOCUS -> pauses the sound (if mute_on_focus_lost)
//   APP_CMD_WINDOW_RESIZED/CONFIG_CHANGED -> updates x_size/y_size
//   APP_CMD_DESTROY      -> app->destroyRequested, the loop exits
//
// The loop only draws with a surface + app in the foreground; otherwise it
// blocks on ALooper without wasting CPU/battery.
//
// Touch -> game mouse events (see the "input" section below):
//   quick tap                   -> left click
//   hold still (~0.5 s)         -> right click (movement orders etc.)
//   drag over the interface     -> left button drag (bars, windows)
//   drag over the map           -> middle button drag (moves the map)
//   two fingers                 -> pinch = zoom (mouse wheel), move = drags the map
//   three fingers               -> shows/hides the performance counter
//   back button/gesture         -> ESC
// Physical mice and keyboards (USB/Bluetooth) also work.

#include "window.hpp"
#include "system_state.hpp"

#include <android/configuration.h>
#include <android/input.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <sys/resource.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>

#define ALICE_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "Alice", __VA_ARGS__)
#define ALICE_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "Alice", __VA_ARGS__)

namespace window {

int32_t cursor_blink_ms() {
	return 1000; // same as the fixed value used in window_nix.cpp
}
int32_t double_click_ms() {
	return 500; // same as the fixed value used in window_nix.cpp
}

bool is_key_depressed(sys::state const& game_state, sys::virtual_key key) {
	// TODO (input): physical/bluetooth keyboard via AInputEvent. On a touchscreen
	// there are no "held" keys, so false is the correct behavior for now.
	return false;
}

void get_window_size(sys::state const& game_state, int& width, int& height) {
	if(game_state.win_ptr && game_state.win_ptr->native_window && game_state.win_ptr->render_width > 0) {
		width = game_state.win_ptr->render_width;
		height = game_state.win_ptr->render_height;
	} else {
		// no window (app in the background): last known size
		width = game_state.x_size;
		height = game_state.y_size;
	}
}

bool is_in_fullscreen(sys::state const& game_state) {
	return true; // on Android the game always fills the whole screen
}

void set_borderless_full_screen(sys::state& game_state, bool fullscreen) {
	// no-op: on Android there is no "window" to toggle -- always fullscreen
}

void close_window(sys::state& game_state) {
	// equivalent of glfwSetWindowShouldClose: asks the system to close the
	// Activity, which comes back as APP_CMD_DESTROY
	if(game_state.win_ptr && game_state.win_ptr->app)
		ANativeActivity_finish(game_state.win_ptr->app->activity);
}

void create_window(sys::state& game_state, creation_parameters const& params) {
	// On Android the system is the one that creates the window (APP_CMD_INIT_WINDOW),
	// and the main loop is run_android_main_loop called by android_main.
	emit_error_message("create_window is not used on Android -- see run_android_main_loop", false);
}

namespace {

// drawn pixels / screen pixels: converts touch and mouse coordinates
// (always in screen pixels) into the game ones
float g_input_scale = 1.f;

// Sets the window buffer size from render_scale. The Android compositor
// hardware scales the image up to the whole screen.
void apply_render_scale(window_data_impl& win) {
	if(!win.native_window)
		return;
	// (0, 0) goes back to the natural size, to measure the real screen
	ANativeWindow_setBuffersGeometry(win.native_window, 0, 0, WINDOW_FORMAT_RGBX_8888);
	win.physical_width = ANativeWindow_getWidth(win.native_window);
	win.physical_height = ANativeWindow_getHeight(win.native_window);
	if(win.physical_width <= 0 || win.physical_height <= 0)
		return;
	float const scale = std::clamp(win.render_scale, 0.25f, 1.f);
	win.render_width = std::max(1, int32_t(std::lround(float(win.physical_width) * scale)));
	win.render_height = std::max(1, int32_t(std::lround(float(win.physical_height) * scale)));
	if(win.render_width != win.physical_width || win.render_height != win.physical_height)
		ANativeWindow_setBuffersGeometry(win.native_window, win.render_width, win.render_height, WINDOW_FORMAT_RGBX_8888);
	g_input_scale = float(win.render_width) / float(win.physical_width);
}

bool init_egl_display(window_data_impl& win) {
	win.egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if(win.egl_display == EGL_NO_DISPLAY || !eglInitialize(win.egl_display, nullptr, nullptr)) {
		ALICE_LOGE("eglInitialize failed (0x%x)", eglGetError());
		return false;
	}

	// OPAQUE surface (no alpha channel). With alpha, the semi-transparent pixels
	// that the game leaves in the framebuffer (borders and textures with the
	// panels' transparency) become translucent pixels of the app window, and the
	// Android compositor blends them with what is behind -- the panels flickered on
	// the Adreno. On desktop the window alpha is ignored, so it never showed up there.
	EGLint const config_attribs[] = {
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 0,
		EGL_DEPTH_SIZE, 24,
		EGL_STENCIL_SIZE, 8,
		EGL_NONE
	};
	// eglChooseConfig treats the sizes as minimums and sorts the ones with
	// more color bits first (RGBA8 before RGB8), so look for one without alpha
	EGLConfig configs[64];
	EGLint num_configs = 0;
	if(!eglChooseConfig(win.egl_display, config_attribs, configs, 64, &num_configs) || num_configs == 0) {
		ALICE_LOGE("no EGL configuration with GLES 3 + RGB8 + depth24/stencil8 (0x%x)", eglGetError());
		return false;
	}
	win.egl_config = configs[0];
	for(EGLint i = 0; i < num_configs; ++i) {
		EGLint r = 0, g = 0, b = 0, a = 0;
		eglGetConfigAttrib(win.egl_display, configs[i], EGL_RED_SIZE, &r);
		eglGetConfigAttrib(win.egl_display, configs[i], EGL_GREEN_SIZE, &g);
		eglGetConfigAttrib(win.egl_display, configs[i], EGL_BLUE_SIZE, &b);
		eglGetConfigAttrib(win.egl_display, configs[i], EGL_ALPHA_SIZE, &a);
		if(r == 8 && g == 8 && b == 8 && a == 0) {
			win.egl_config = configs[i];
			break;
		}
	}
	EGLint chosen_alpha = 0;
	eglGetConfigAttrib(win.egl_display, win.egl_config, EGL_ALPHA_SIZE, &chosen_alpha);
	ALICE_LOGI("EGL configuration: %s", chosen_alpha == 0 ? "opaque RGB8" : "RGBA8 (no opaque option; buffer marked as RGBX)");
	return true;
}

bool create_egl_context(window_data_impl& win) {
	// the game needs GLES 3.2 (see uses-feature in AndroidManifest.xml)
	EGLint context_attribs[] = {
		EGL_CONTEXT_MAJOR_VERSION, 3,
		EGL_CONTEXT_MINOR_VERSION, 2,
#ifndef NDEBUG
		EGL_CONTEXT_OPENGL_DEBUG, EGL_TRUE,
#endif
		EGL_NONE
	};
	win.egl_context = eglCreateContext(win.egl_display, win.egl_config, EGL_NO_CONTEXT, context_attribs);
#ifndef NDEBUG
	if(win.egl_context == EGL_NO_CONTEXT) {
		// some drivers reject a debug context -- try without it
		context_attribs[4] = EGL_NONE;
		win.egl_context = eglCreateContext(win.egl_display, win.egl_config, EGL_NO_CONTEXT, context_attribs);
	}
#endif
	if(win.egl_context == EGL_NO_CONTEXT) {
		ALICE_LOGE("eglCreateContext (GLES 3.2) failed (0x%x)", eglGetError());
		return false;
	}
	return true;
}

bool create_egl_surface(window_data_impl& win, ANativeWindow* native_window) {
	win.native_window = native_window;
	apply_render_scale(win);
	win.egl_surface = eglCreateWindowSurface(win.egl_display, win.egl_config, native_window, nullptr);
	if(win.egl_surface == EGL_NO_SURFACE) {
		ALICE_LOGE("eglCreateWindowSurface failed (0x%x)", eglGetError());
		return false;
	}
	if(!eglMakeCurrent(win.egl_display, win.egl_surface, win.egl_surface, win.egl_context)) {
		ALICE_LOGE("eglMakeCurrent failed (0x%x)", eglGetError());
		return false;
	}
	eglSwapInterval(win.egl_display, 1); // vsync, like the desktop's glfwSwapInterval(1)
	return true;
}

void destroy_egl_surface(window_data_impl& win) {
	if(win.egl_display != EGL_NO_DISPLAY) {
		eglMakeCurrent(win.egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		if(win.egl_surface != EGL_NO_SURFACE)
			eglDestroySurface(win.egl_display, win.egl_surface);
	}
	win.egl_surface = EGL_NO_SURFACE;
	win.native_window = nullptr;
}

void terminate_egl(window_data_impl& win) {
	destroy_egl_surface(win);
	if(win.egl_display != EGL_NO_DISPLAY) {
		if(win.egl_context != EGL_NO_CONTEXT)
			eglDestroyContext(win.egl_display, win.egl_context);
		eglTerminate(win.egl_display);
	}
	win.egl_context = EGL_NO_CONTEXT;
	win.egl_display = EGL_NO_DISPLAY;
}

void update_window_size(sys::state& state) {
	auto& win = *state.win_ptr;
	if(!win.native_window)
		return;
	apply_render_scale(win); // rotation etc.: measures the screen again
	int32_t const width = win.render_width;
	int32_t const height = win.render_height;
	if(width <= 0 || height <= 0 || (width == state.x_size && height == state.y_size))
		return;
	ALICE_LOGI("screen %dx%d, drawing at %dx%d (scale %.2f)", win.physical_width, win.physical_height, width, height, win.render_scale);
	if(win.game_started)
		state.on_resize(width, height, window_state::maximized);
	state.x_size = width;
	state.y_size = height;
}

void on_window_created(sys::state& state, ANativeWindow* native_window) {
	auto& win = *state.win_ptr;
	if(win.egl_display == EGL_NO_DISPLAY) {
		if(!init_egl_display(win) || !create_egl_context(win)) {
			emit_error_message("Could not start OpenGL ES 3.2 (EGL). Is the device compatible?", true);
		}
	}
	if(!create_egl_surface(win, native_window)) {
		emit_error_message("Could not create the drawing surface (EGL).", true);
	}

	static bool logged_gl_info = false;
	if(!logged_gl_info) {
		logged_gl_info = true;
		ALICE_LOGI("GL_VERSION: %s", reinterpret_cast<char const*>(glGetString(GL_VERSION)));
		ALICE_LOGI("GL_RENDERER: %s", reinterpret_cast<char const*>(glGetString(GL_RENDERER)));
		ALICE_LOGI("GL_VENDOR: %s", reinterpret_cast<char const*>(glGetString(GL_VENDOR)));
	}

	update_window_size(state);
	// the game itself is started by android_launcher_update once the scenario
	// has been loaded (see android_start_game)
}

// When going to the background Android may kill the process at any moment to
// free memory, with no warning. So, in a single-player game:
//  - pauses the simulation as the pause button does (stores the speed in
//    held_game_speed), so the game time does not run with the app off screen;
//  - requests a save through the same command as the "Save" menu, which the game
//    thread executes (no writing the state from here, with the simulation running).
// The file is always the same (overwritten), and at most one every 30 s.
constexpr char const* background_save_name = "android_autosave.bin";
constexpr auto background_save_interval = std::chrono::seconds(30);

void pause_and_save_on_background(sys::state& state) {
	auto& win = *state.win_ptr;
	if(!win.game_started || !state.current_scene.game_in_progress)
		return;
	if(state.network_mode != sys::network_mode_type::single_player)
		return; // in multiplayer the host decides pause and saves
	if(!state.local_player_nation)
		return; // e.g.: still choosing the country

	if(auto const speed = state.actual_game_speed.load(); speed > 0) {
		state.ui_state.held_game_speed = speed;
		state.actual_game_speed = 0;
	}

	auto const now = std::chrono::steady_clock::now();
	if(now - win.last_background_save < background_save_interval)
		return;
	win.last_background_save = now;
	ALICE_LOGI("going to the background: pausing and saving to %s", background_save_name);
	command::save_game(state, state.local_player_nation, false, background_save_name);
}

void handle_cmd(android_app* app, int32_t cmd) {
	auto& state = *static_cast<sys::state*>(app->userData);
	auto& win = *state.win_ptr;

	switch(cmd) {
	case APP_CMD_INIT_WINDOW:
		ALICE_LOGI("APP_CMD_INIT_WINDOW");
		if(app->window)
			on_window_created(state, app->window);
		break;
	case APP_CMD_TERM_WINDOW:
		ALICE_LOGI("APP_CMD_TERM_WINDOW");
		destroy_egl_surface(win);
		break;
	case APP_CMD_WINDOW_RESIZED:
	case APP_CMD_CONFIG_CHANGED:
	case APP_CMD_CONTENT_RECT_CHANGED:
		update_window_size(state);
		break;
	case APP_CMD_GAINED_FOCUS:
		win.has_focus = true;
		if(win.game_started && state.user_settings.mute_on_focus_lost)
			sound::resume_all(state);
		break;
	case APP_CMD_LOST_FOCUS:
		win.has_focus = false;
		if(win.game_started && state.user_settings.mute_on_focus_lost)
			sound::pause_all(state);
		break;
	case APP_CMD_RESUME:
		ALICE_LOGI("APP_CMD_RESUME");
		win.resumed = true;
		break;
	case APP_CMD_PAUSE:
		ALICE_LOGI("APP_CMD_PAUSE");
		win.resumed = false;
		pause_and_save_on_background(state);
		break;
	case APP_CMD_SAVE_STATE:
		// TODO: autosave -- Android may kill the app in the background
		break;
	case APP_CMD_DESTROY:
		ALICE_LOGI("APP_CMD_DESTROY");
		break;
	default:
		break;
	}
}

// ---------------------------------------------------------------------------
// input: touch, mouse and keyboard
// ---------------------------------------------------------------------------

constexpr auto long_press_time = std::chrono::milliseconds(500);
constexpr float touch_slop_dp = 10.f;	// how far the finger may move and still be a tap/hold
constexpr float wheel_clicks_per_doubling = 3.f; // pinch: doubling the distance = 3 wheel "clicks"

struct touch_tracker {
	enum class mode : uint8_t {
		none,
		pending,         // finger touched down; not yet known if it is a tap, a hold or a drag
		left_drag,       // drag on the interface (left button)
		map_pan,         // drag on the map (middle button)
		pinch,           // two fingers
		ignore_until_up  // gesture already consumed; waits for all fingers to lift
	};
	mode current = mode::none;
	int32_t primary_id = -1;
	float start_x = 0.f, start_y = 0.f;
	float last_x = 0.f, last_y = 0.f;
	float pinch_last_distance = 0.f;
	std::chrono::steady_clock::time_point down_time;

	// only for the visual feedback while the game is not loaded
	enum class flash : uint8_t { none, tap, long_press };
	flash last_flash = flash::none;
	std::chrono::steady_clock::time_point flash_time;
	float placeholder_zoom = 0.f;
};
touch_tracker touch;

// pointer position in game pixels (see g_input_scale)
float event_x(AInputEvent const* event, size_t pointer) {
	return AMotionEvent_getX(event, pointer) * g_input_scale;
}
float event_y(AInputEvent const* event, size_t pointer) {
	return AMotionEvent_getY(event, pointer) * g_input_scale;
}

// in game pixels
float touch_slop_px(android_app* app) {
	int32_t const density = app->config ? AConfiguration_getDensity(app->config) : ACONFIGURATION_DENSITY_MEDIUM;
	return touch_slop_dp * float(density > 0 ? density : ACONFIGURATION_DENSITY_MEDIUM) / float(ACONFIGURATION_DENSITY_MEDIUM) * g_input_scale;
}

sys::key_modifiers modifiers_from_meta(int32_t meta) {
	uint32_t val = 0;
	if(meta & AMETA_CTRL_ON)
		val |= uint32_t(sys::key_modifiers::modifiers_ctrl);
	if(meta & AMETA_ALT_ON)
		val |= uint32_t(sys::key_modifiers::modifiers_alt);
	if(meta & AMETA_SHIFT_ON)
		val |= uint32_t(sys::key_modifiers::modifiers_shift);
	return sys::key_modifiers(val);
}

// The events only reach the game after on_create; on desktop they run
// inside the same render lock (see the window_nix.cpp loop).
template<typename F>
void dispatch_to_game(sys::state& state, F&& f) {
	if(!state.win_ptr->game_started)
		return;
	std::shared_lock lock(state.game_state_resetting_lock);
	state.game_state_resetting_cv.wait(lock, [&] { return !state.yield_game_state_resetting_lock; });
	f();
}

void game_mouse_move(sys::state& state, int32_t x, int32_t y, sys::key_modifiers mod) {
	dispatch_to_game(state, [&] {
		state.on_mouse_move(x, y, mod);
		if(state.win_ptr->left_mouse_down)
			state.on_mouse_drag(x, y, mod);
	});
	state.mouse_x_position = x;
	state.mouse_y_position = y;
}
void game_lbutton(sys::state& state, int32_t x, int32_t y, bool down, sys::key_modifiers mod) {
	dispatch_to_game(state, [&] {
		if(down)
			state.on_lbutton_down(x, y, mod);
		else
			state.on_lbutton_up(x, y, mod);
	});
	state.win_ptr->left_mouse_down = down;
	if(!down)
		state.ui_state.selecting_edit_text = ui::edit_selection_mode::none;
	state.mouse_x_position = x;
	state.mouse_y_position = y;
}
void game_rbutton(sys::state& state, int32_t x, int32_t y, bool down, sys::key_modifiers mod) {
	dispatch_to_game(state, [&] {
		if(down)
			state.on_rbutton_down(x, y, mod);
		else
			state.on_rbutton_up(x, y, mod);
	});
	state.mouse_x_position = x;
	state.mouse_y_position = y;
}
void game_mbutton(sys::state& state, int32_t x, int32_t y, bool down, sys::key_modifiers mod) {
	dispatch_to_game(state, [&] {
		if(down)
			state.on_mbutton_down(x, y, mod);
		else
			state.on_mbutton_up(x, y, mod);
	});
	state.mouse_x_position = x;
	state.mouse_y_position = y;
}
void game_wheel(sys::state& state, int32_t x, int32_t y, float amount, sys::key_modifiers mod) {
	dispatch_to_game(state, [&] { sys::on_mouse_wheel(state, x, y, mod, amount); });
	if(!state.win_ptr->game_started)
		touch.placeholder_zoom = std::clamp(touch.placeholder_zoom + amount, -10.f, 10.f);
}
void game_key(sys::state& state, sys::virtual_key key, bool down, sys::key_modifiers mod) {
	dispatch_to_game(state, [&] {
		if(down)
			state.on_key_down(key, mod);
		else
			state.on_key_up(key, mod);
	});
}

// Is the finger over the interface (and not over the map)? The game computes
// ui_state.under_mouse every frame from mouse_x/y_position, which has already been
// updated on ACTION_DOWN -- so on the first movement it is already valid.
bool touch_is_over_ui(sys::state const& state) {
	if(!state.win_ptr->game_started)
		return false;
	return state.ui_state.under_mouse != nullptr || state.iui_state.over_ui;
}

int32_t find_pointer_index(AInputEvent const* event, int32_t id) {
	size_t const count = AMotionEvent_getPointerCount(event);
	for(size_t i = 0; i < count; ++i) {
		if(AMotionEvent_getPointerId(event, i) == id)
			return int32_t(i);
	}
	return -1;
}

void pinch_geometry(AInputEvent const* event, float& mid_x, float& mid_y, float& distance) {
	float const x0 = event_x(event, 0), y0 = event_y(event, 0);
	float const x1 = event_x(event, 1), y1 = event_y(event, 1);
	mid_x = (x0 + x1) * 0.5f;
	mid_y = (y0 + y1) * 0.5f;
	distance = std::max(std::hypot(x1 - x0, y1 - y0), 1.f);
}

// ends an ongoing single-finger gesture, without generating a click
void end_single_finger_gesture(sys::state& state) {
	auto const x = int32_t(touch.last_x), y = int32_t(touch.last_y);
	auto const mod = sys::key_modifiers::modifiers_none;
	if(touch.current == touch_tracker::mode::left_drag)
		game_lbutton(state, x, y, false, mod);
	else if(touch.current == touch_tracker::mode::map_pan || touch.current == touch_tracker::mode::pinch)
		game_mbutton(state, x, y, false, mod);
	touch.current = touch_tracker::mode::none;
}

void start_flash(touch_tracker::flash f) {
	touch.last_flash = f;
	touch.flash_time = std::chrono::steady_clock::now();
}

// Performance counter: frames per second, average time and worst frame, and
// how many game days passed in the last second. The game's own FPS counter
// (the console's "fps" command) depends on a UI element that does not
// exist in the files, so this one is shown by AliceActivity.
struct perf_counter {
	bool visible = false;
	std::chrono::steady_clock::time_point window_start;
	std::chrono::steady_clock::time_point last_frame;
	int32_t frames = 0;
	float worst_ms = 0.f;
	int32_t start_date = 0;
};
perf_counter perf;

void toggle_perf_counter() {
	perf.visible = !perf.visible;
	perf.frames = 0;
	perf.worst_ms = 0.f;
	perf.window_start = perf.last_frame = std::chrono::steady_clock::now();
	perf.start_date = -1;
	android_set_stats(perf.visible ? "measuring..." : "");
}

void update_perf_counter(sys::state& state) {
	if(!perf.visible)
		return;
	auto const now = std::chrono::steady_clock::now();
	float const frame_ms = std::chrono::duration<float, std::milli>(now - perf.last_frame).count();
	perf.last_frame = now;
	int32_t const date = state.current_date.to_raw_value();
	if(perf.start_date < 0 || frame_ms > 1000.f) {
		// first measurement, or the app sat still in the background: start over
		perf.window_start = now;
		perf.frames = 0;
		perf.worst_ms = 0.f;
		perf.start_date = date;
		return;
	}
	perf.worst_ms = std::max(perf.worst_ms, frame_ms);
	++perf.frames;

	float const elapsed_ms = std::chrono::duration<float, std::milli>(now - perf.window_start).count();
	if(elapsed_ms < 1000.f || perf.frames == 0)
		return;
	int32_t const speed = state.actual_game_speed.load(std::memory_order::relaxed);
	char speed_text[24] = "paused";
	if(speed > 0)
		snprintf(speed_text, sizeof(speed_text), "speed %d", int(speed));
	char text[128];
	snprintf(text, sizeof(text), "FPS %.0f  frame %.0f ms (worst %.0f)\n%s  days/s %.1f",
		float(perf.frames) * 1000.f / elapsed_ms, elapsed_ms / float(perf.frames), perf.worst_ms,
		speed_text, float(date - perf.start_date) * 1000.f / elapsed_ms);
	android_set_stats(text);
	perf.window_start = now;
	perf.frames = 0;
	perf.worst_ms = 0.f;
	perf.start_date = date;
}

int32_t handle_touch(android_app* app, sys::state& state, AInputEvent* event) {
	int32_t const action = AMotionEvent_getAction(event);
	int32_t const masked = action & AMOTION_EVENT_ACTION_MASK;
	auto const mod = sys::key_modifiers::modifiers_none;

	switch(masked) {
	case AMOTION_EVENT_ACTION_DOWN: {
		touch.current = touch_tracker::mode::pending;
		touch.primary_id = AMotionEvent_getPointerId(event, 0);
		touch.start_x = touch.last_x = event_x(event, 0);
		touch.start_y = touch.last_y = event_y(event, 0);
		touch.down_time = std::chrono::steady_clock::now();
		// no button pressed: just positions the "cursor" so the game finds what is under the finger
		game_mouse_move(state, int32_t(touch.last_x), int32_t(touch.last_y), mod);
		break;
	}
	case AMOTION_EVENT_ACTION_POINTER_DOWN: {
		if(AMotionEvent_getPointerCount(event) == 3 && touch.current != touch_tracker::mode::ignore_until_up) {
			// third finger: undoes the pinch that started with the first two
			end_single_finger_gesture(state);
			toggle_perf_counter();
			touch.current = touch_tracker::mode::ignore_until_up;
		} else if(AMotionEvent_getPointerCount(event) == 2 && touch.current != touch_tracker::mode::ignore_until_up) {
			end_single_finger_gesture(state);
			float mid_x, mid_y;
			pinch_geometry(event, mid_x, mid_y, touch.pinch_last_distance);
			touch.last_x = mid_x;
			touch.last_y = mid_y;
			game_mouse_move(state, int32_t(mid_x), int32_t(mid_y), mod);
			game_mbutton(state, int32_t(mid_x), int32_t(mid_y), true, mod); // drags the map around the midpoint
			touch.current = touch_tracker::mode::pinch;
		}
		break;
	}
	case AMOTION_EVENT_ACTION_MOVE: {
		if(touch.current == touch_tracker::mode::pinch) {
			if(AMotionEvent_getPointerCount(event) < 2)
				break;
			float mid_x, mid_y, distance;
			pinch_geometry(event, mid_x, mid_y, distance);
			touch.last_x = mid_x;
			touch.last_y = mid_y;
			game_mouse_move(state, int32_t(mid_x), int32_t(mid_y), mod);
			float const clicks = std::log2(distance / touch.pinch_last_distance) * wheel_clicks_per_doubling;
			if(std::abs(clicks) > 0.01f) {
				game_wheel(state, int32_t(mid_x), int32_t(mid_y), clicks, mod);
				touch.pinch_last_distance = distance;
			}
			break;
		}

		int32_t const index = find_pointer_index(event, touch.primary_id);
		if(index < 0)
			break;
		float const x = event_x(event, index);
		float const y = event_y(event, index);

		if(touch.current == touch_tracker::mode::pending) {
			if(std::hypot(x - touch.start_x, y - touch.start_y) < touch_slop_px(app))
				break;
			// it became a drag: starts at the point where the finger touched down
			auto const sx = int32_t(touch.start_x), sy = int32_t(touch.start_y);
			if(touch_is_over_ui(state)) {
				game_lbutton(state, sx, sy, true, mod);
				touch.current = touch_tracker::mode::left_drag;
			} else {
				game_mbutton(state, sx, sy, true, mod);
				touch.current = touch_tracker::mode::map_pan;
			}
		}
		if(touch.current == touch_tracker::mode::left_drag || touch.current == touch_tracker::mode::map_pan) {
			touch.last_x = x;
			touch.last_y = y;
			game_mouse_move(state, int32_t(x), int32_t(y), mod);
		}
		break;
	}
	case AMOTION_EVENT_ACTION_POINTER_UP: {
		if(touch.current == touch_tracker::mode::pinch) {
			// one of the fingers lifted: ends the pinch and ignores the remaining finger
			// (otherwise it would become an accidental tap/drag)
			end_single_finger_gesture(state);
			touch.current = touch_tracker::mode::ignore_until_up;
		}
		break;
	}
	case AMOTION_EVENT_ACTION_UP: {
		if(touch.current == touch_tracker::mode::pending) {
			// quick tap = left click where the finger touched down
			auto const x = int32_t(touch.start_x), y = int32_t(touch.start_y);
			game_lbutton(state, x, y, true, mod);
			game_lbutton(state, x, y, false, mod);
			start_flash(touch_tracker::flash::tap);
			touch.current = touch_tracker::mode::none;
		} else {
			end_single_finger_gesture(state);
		}
		break;
	}
	case AMOTION_EVENT_ACTION_CANCEL: {
		end_single_finger_gesture(state);
		break;
	}
	default:
		break;
	}
	return 1;
}

// Holding still generates no event, so the right click is checked every frame.
void update_touch(sys::state& state) {
	if(touch.current != touch_tracker::mode::pending)
		return;
	if(std::chrono::steady_clock::now() - touch.down_time < long_press_time)
		return;
	auto const x = int32_t(touch.start_x), y = int32_t(touch.start_y);
	game_rbutton(state, x, y, true, sys::key_modifiers::modifiers_none);
	game_rbutton(state, x, y, false, sys::key_modifiers::modifiers_none);
	start_flash(touch_tracker::flash::long_press);
	touch.current = touch_tracker::mode::ignore_until_up;
}

// USB/Bluetooth mouse: forwards directly, like on desktop
int32_t handle_mouse(sys::state& state, AInputEvent* event) {
	int32_t const masked = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
	auto const x = int32_t(event_x(event, 0));
	auto const y = int32_t(event_y(event, 0));
	auto const mod = modifiers_from_meta(AMotionEvent_getMetaState(event));

	switch(masked) {
	case AMOTION_EVENT_ACTION_HOVER_MOVE:
	case AMOTION_EVENT_ACTION_MOVE:
		game_mouse_move(state, x, y, mod);
		return 1;
	case AMOTION_EVENT_ACTION_BUTTON_PRESS:
	case AMOTION_EVENT_ACTION_BUTTON_RELEASE: {
		// AMotionEvent_getActionButton only exists on API 33+; compare with the previous state
		static int32_t previous_buttons = 0;
		int32_t const buttons = AMotionEvent_getButtonState(event);
		int32_t const changed = buttons ^ previous_buttons;
		previous_buttons = buttons;
		if(changed & AMOTION_EVENT_BUTTON_PRIMARY)
			game_lbutton(state, x, y, (buttons & AMOTION_EVENT_BUTTON_PRIMARY) != 0, mod);
		if(changed & AMOTION_EVENT_BUTTON_SECONDARY)
			game_rbutton(state, x, y, (buttons & AMOTION_EVENT_BUTTON_SECONDARY) != 0, mod);
		if(changed & AMOTION_EVENT_BUTTON_TERTIARY)
			game_mbutton(state, x, y, (buttons & AMOTION_EVENT_BUTTON_TERTIARY) != 0, mod);
		return 1;
	}
	case AMOTION_EVENT_ACTION_SCROLL:
		game_wheel(state, x, y, AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_VSCROLL, 0), mod);
		return 1;
	case AMOTION_EVENT_ACTION_DOWN:
	case AMOTION_EVENT_ACTION_UP:
		// already handled as BUTTON_PRESS/RELEASE
		return 1;
	default:
		return 0;
	}
}

sys::virtual_key android_key_to_virtual_key(int32_t code) {
	if(code >= AKEYCODE_A && code <= AKEYCODE_Z)
		return sys::virtual_key(uint8_t(sys::virtual_key::A) + (code - AKEYCODE_A));
	if(code >= AKEYCODE_0 && code <= AKEYCODE_9)
		return sys::virtual_key(uint8_t(sys::virtual_key::NUM_0) + (code - AKEYCODE_0));
	if(code >= AKEYCODE_F1 && code <= AKEYCODE_F12)
		return sys::virtual_key(uint8_t(sys::virtual_key::F1) + (code - AKEYCODE_F1));
	if(code >= AKEYCODE_NUMPAD_0 && code <= AKEYCODE_NUMPAD_9)
		return sys::virtual_key(uint8_t(sys::virtual_key::NUMPAD0) + (code - AKEYCODE_NUMPAD_0));
	switch(code) {
	case AKEYCODE_BACK: // back button/gesture
	case AKEYCODE_ESCAPE: return sys::virtual_key::ESCAPE;
	case AKEYCODE_ENTER:
	case AKEYCODE_NUMPAD_ENTER: return sys::virtual_key::RETURN;
	case AKEYCODE_TAB: return sys::virtual_key::TAB;
	case AKEYCODE_DEL: return sys::virtual_key::BACK; // backspace
	case AKEYCODE_FORWARD_DEL: return sys::virtual_key::DELETE_KEY;
	case AKEYCODE_SPACE: return sys::virtual_key::SPACE;
	case AKEYCODE_DPAD_LEFT: return sys::virtual_key::LEFT;
	case AKEYCODE_DPAD_RIGHT: return sys::virtual_key::RIGHT;
	case AKEYCODE_DPAD_UP: return sys::virtual_key::UP;
	case AKEYCODE_DPAD_DOWN: return sys::virtual_key::DOWN;
	case AKEYCODE_PAGE_UP: return sys::virtual_key::PRIOR;
	case AKEYCODE_PAGE_DOWN: return sys::virtual_key::NEXT;
	case AKEYCODE_MOVE_HOME: return sys::virtual_key::HOME;
	case AKEYCODE_MOVE_END: return sys::virtual_key::END;
	case AKEYCODE_INSERT: return sys::virtual_key::INSERT;
	case AKEYCODE_SHIFT_LEFT: return sys::virtual_key::LSHIFT;
	case AKEYCODE_SHIFT_RIGHT: return sys::virtual_key::RSHIFT;
	case AKEYCODE_CTRL_LEFT: return sys::virtual_key::LCONTROL;
	case AKEYCODE_CTRL_RIGHT: return sys::virtual_key::RCONTROL;
	case AKEYCODE_ALT_LEFT: return sys::virtual_key::LMENU;
	case AKEYCODE_ALT_RIGHT: return sys::virtual_key::RMENU;
	case AKEYCODE_COMMA: return sys::virtual_key::COMMA;
	case AKEYCODE_PERIOD: return sys::virtual_key::PERIOD;
	case AKEYCODE_MINUS: return sys::virtual_key::MINUS;
	case AKEYCODE_EQUALS:
	case AKEYCODE_PLUS: return sys::virtual_key::PLUS;
	case AKEYCODE_NUMPAD_ADD: return sys::virtual_key::ADD;
	case AKEYCODE_NUMPAD_SUBTRACT: return sys::virtual_key::SUBTRACT;
	case AKEYCODE_SEMICOLON: return sys::virtual_key::SEMICOLON;
	case AKEYCODE_SLASH: return sys::virtual_key::FORWARD_SLASH;
	case AKEYCODE_BACKSLASH: return sys::virtual_key::BACK_SLASH;
	case AKEYCODE_LEFT_BRACKET: return sys::virtual_key::OPEN_BRACKET;
	case AKEYCODE_RIGHT_BRACKET: return sys::virtual_key::CLOSED_BRACKET;
	case AKEYCODE_APOSTROPHE: return sys::virtual_key::QUOTE;
	case AKEYCODE_GRAVE: return sys::virtual_key::TILDA;
	default: return sys::virtual_key::NONE;
	}
}

int32_t handle_key(sys::state& state, AInputEvent* event) {
	int32_t const code = AKeyEvent_getKeyCode(event);
	auto const key = android_key_to_virtual_key(code);
	if(key == sys::virtual_key::NONE)
		return 0; // volume etc.: let the system handle it

	auto const mod = modifiers_from_meta(AKeyEvent_getMetaState(event));
	switch(AKeyEvent_getAction(event)) {
	case AKEY_EVENT_ACTION_DOWN:
		// repeat only for the editing keys, like in window_nix.cpp
		if(AKeyEvent_getRepeatCount(event) > 0) {
			switch(key) {
			case sys::virtual_key::RETURN: case sys::virtual_key::BACK: case sys::virtual_key::DELETE_KEY:
			case sys::virtual_key::LEFT: case sys::virtual_key::RIGHT: case sys::virtual_key::UP: case sys::virtual_key::DOWN:
				break;
			default:
				return 1;
			}
		}
		game_key(state, key, true, mod);
		return 1;
	case AKEY_EVENT_ACTION_UP:
		game_key(state, key, false, mod);
		return 1;
	default:
		return 1;
	}
	// TODO (input): typed characters (on_text) need the virtual keyboard
	// and KeyEvent.getUnicodeChar via JNI -- the native API does not deliver the character.
}

int32_t handle_input(android_app* app, AInputEvent* event) {
	auto& state = *static_cast<sys::state*>(app->userData);
	switch(AInputEvent_getType(event)) {
	case AINPUT_EVENT_TYPE_MOTION:
		if(AInputEvent_getSource(event) == AINPUT_SOURCE_MOUSE)
			return handle_mouse(state, event);
		if(AInputEvent_getSource(event) & AINPUT_SOURCE_CLASS_POINTER)
			return handle_touch(app, state, event);
		return 0;
	case AINPUT_EVENT_TYPE_KEY:
		return handle_key(state, event);
	default:
		return 0;
	}
}

// While the game is not loaded: shows the recognized gestures, to
// test touch on the device. The background brightens/darkens with the pinch; white
// square = finger dragging, yellow = tap (click), red = hold (right click).
void render_touch_placeholder(sys::state& state) {
	float z = touch.placeholder_zoom / 10.f; // -1..1
	if(state.win_ptr->loading) {
		// loading: the background "breathes" to show the app has not frozen
		auto const ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		z += 0.5f * std::sin(float(ms % 2000) / 2000.f * 6.2831853f);
	}
	glViewport(0, 0, state.x_size, state.y_size);
	glDisable(GL_SCISSOR_TEST);
	glClearColor(0.11f + 0.08f * z, 0.20f + 0.10f * z, 0.33f + 0.12f * z, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	auto square = [&](float x, float y, float r, float g, float b) {
		int32_t const half = int32_t(touch_slop_px(state.win_ptr->app) * 4.f);
		glEnable(GL_SCISSOR_TEST);
		glScissor(int32_t(x) - half, state.y_size - int32_t(y) - half, half * 2, half * 2); // GL has y pointing up
		glClearColor(r, g, b, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		glDisable(GL_SCISSOR_TEST);
	};

	using m = touch_tracker::mode;
	if(touch.current == m::left_drag || touch.current == m::map_pan || touch.current == m::pinch || touch.current == m::pending)
		square(touch.last_x, touch.last_y, 0.95f, 0.95f, 0.95f);

	if(std::chrono::steady_clock::now() - touch.flash_time < std::chrono::milliseconds(300)) {
		if(touch.last_flash == touch_tracker::flash::tap)
			square(touch.start_x, touch.start_y, 0.98f, 0.85f, 0.20f);
		else if(touch.last_flash == touch_tracker::flash::long_press)
			square(touch.start_x, touch.start_y, 0.90f, 0.25f, 0.20f);
	}
}

bool can_render(window_data_impl const& win) {
	return win.resumed && win.egl_surface != EGL_NO_SURFACE;
}

void render_frame(sys::state& state) {
	auto& win = *state.win_ptr;

	if(win.game_started) {
		// Loading a save freezes the screen until the game thread finishes (and its
		// "busy" cursor request stays queued on this same screen). The
		// loading window opens before waiting, and receives the touches.
		bool const must_wait = state.yield_game_state_resetting_lock;
		if(must_wait)
			android_set_loading("Loading...");
		std::shared_lock lock(state.game_state_resetting_lock);
		state.game_state_resetting_cv.wait(lock, [&] { return !state.yield_game_state_resetting_lock; });
		if(must_wait)
			android_set_loading("");
		state.render();
	} else {
		// no game loaded yet: proves that EGL/surface/vsync and touch work
		render_touch_placeholder(state);
	}

	// A phone GPU draws in tiles on the chip itself; telling it that the frame's depth
	// and stencil will no longer be used avoids writing them to memory.
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	GLenum const discard[] = { GL_DEPTH, GL_STENCIL };
	glInvalidateFramebuffer(GL_FRAMEBUFFER, 2, discard);

	if(!eglSwapBuffers(win.egl_display, win.egl_surface)) {
		EGLint const err = eglGetError();
		ALICE_LOGE("eglSwapBuffers failed (0x%x)", err);
		if(err == EGL_BAD_SURFACE || err == EGL_BAD_NATIVE_WINDOW) {
			// the window disappeared between the poll and the swap -- recreate on the same window if it still exists
			ANativeWindow* native_window = win.native_window;
			destroy_egl_surface(win);
			if(native_window && win.app->window == native_window)
				create_egl_surface(win, native_window);
		} else if(err == EGL_CONTEXT_LOST) {
			// rare (driver restarted): all GL resources were lost
			emit_error_message("The OpenGL ES context was lost by the system.", true);
		}
	}

	if(win.game_started)
		sound::update_music_track(state);
}

} // namespace

float android_default_ui_scale(int32_t width, int32_t height, int32_t density_dpi) {
	// Victoria 2's interface was made for at least ~1024x720 pixels:
	// with scale s, the "logical" screen is (width/s) x (height/s). So:
	//  - by density: s = dpi/160 (1 Android dp = 1 interface pixel),
	//    which gives buttons and text a comfortable physical size for a finger;
	//  - but never past the largest scale that still fits 1024x720 (on a phone in
	//    landscape that is what limits it: 1080 height -> at most 1.5).
	int32_t const long_side = std::max(width, height);
	int32_t const short_side = std::min(width, height);
	float const by_density = float(density_dpi > 0 ? density_dpi : 160) / 160.f;
	float const fits = std::min(float(long_side) / 1024.f, float(short_side) / 720.f);
	float const target = std::min(by_density, fits);

	// the largest value in the options list that does not exceed the target (minimum 0.75)
	float chosen = 0.75f;
	for(float v : sys::ui_scales) {
		if(v <= target + 0.001f && v >= chosen)
			chosen = v;
	}
	return chosen;
}

void android_start_game(sys::state& state) {
	auto& win = *state.win_ptr;
	assert(win.egl_surface != EGL_NO_SURFACE && !win.game_started);
	ALICE_LOGI("starting the game (OpenGL, sound, on_create)");

	ogl::initialize_opengl(state);

	sound::initialize_sound_system(state);
	sound::start_music(state, state.user_settings.master_volume * state.user_settings.music_volume);

	// equivalent of the initial on_window_change in window_nix.cpp
	int32_t const width = win.render_width;
	int32_t const height = win.render_height;

	if(win.first_run_defaults) {
		// first run (no user_settings.dat); it is saved and can be
		// changed later in the game options:
		//  - interface scale based on the screen. The density counts in drawn
		//    pixels: with render_scale 0.75 each one takes up more space on screen.
		int32_t const density = win.app->config ? AConfiguration_getDensity(win.app->config) : ACONFIGURATION_DENSITY_MEDIUM;
		int32_t const render_density = int32_t(float(density) * float(width) / float(std::max(1, win.physical_width)));
		state.user_settings.ui_scale = android_default_ui_scale(width, height, render_density);
		//  - no MSAA: the game draws into a separate multisample texture and resolves
		//    it with its own shader; on a phone GPU (tile-based) that takes the
		//    4 samples of every pixel off the chip into memory each frame.
		state.user_settings.antialias_level = 0;
		state.save_user_settings();
		win.first_run_defaults = false;
		ALICE_LOGI("first run: interface scale %.2f (drawing %dx%d, %d effective dpi), MSAA off", state.user_settings.ui_scale, width, height, render_density);
	}

	state.on_resize(width, height, window_state::maximized);
	state.x_size = width;
	state.y_size = height;

	state.on_create();
	win.loading = false;
	win.game_started = true;

	// This thread reads touch and draws. With time running, the simulation takes
	// every core (game thread + TBB workers) and, at the same
	// priority, touch waits its turn. "Urgent display" priority, the same
	// one Android uses on apps' drawing thread. It goes at the end: threads
	// inherit the priority of whoever creates them, and the game's were already created.
	if(setpriority(PRIO_PROCESS, gettid(), -8) != 0)
		ALICE_LOGE("could not raise the priority of the drawing thread (errno %d)", errno);
}

void run_android_main_loop(sys::state& game_state, android_app* app) {
	game_state.win_ptr = std::make_unique<window_data_impl>();
	auto& win = *game_state.win_ptr;
	win.app = app;

	app->userData = &game_state;
	app->onAppCmd = handle_cmd;
	app->onInputEvent = handle_input;

	while(!app->destroyRequested) {
		// rendering: only processes what has already arrived (timeout 0); idle: waits for an event
		int const timeout = can_render(win) ? 0 : -1;
		android_poll_source* source = nullptr;
		int result = ALooper_pollOnce(timeout, nullptr, nullptr, reinterpret_cast<void**>(&source));
		while(result >= 0 || result == ALOOPER_POLL_CALLBACK) {
			if(source)
				source->process(app, source);
			if(app->destroyRequested)
				break;
			source = nullptr;
			result = ALooper_pollOnce(0, nullptr, nullptr, reinterpret_cast<void**>(&source));
		}
		if(app->destroyRequested)
			break;

		android_launcher_update(game_state);

		if(can_render(win)) {
			update_touch(game_state);
			render_frame(game_state);
			update_perf_counter(game_state);
		}
	}

	terminate_egl(win);
	ALICE_LOGI("main loop finished");
}

void change_cursor(sys::state& state, cursor_type type) {
	// There is no cursor on a touchscreen. But the game marks with the "busy" cursor the
	// long jobs that block the thread (creating the interface when entering the
	// game, loading a save...): on Android that becomes the loading window.
	static std::atomic<bool> busy{ false };
	bool const now_busy = type == cursor_type::busy;
	if(busy.exchange(now_busy) != now_busy)
		android_set_loading(now_busy ? "Loading..." : "");
}

void emit_error_message(std::string const& content, bool fatal) {
	__android_log_print(fatal ? ANDROID_LOG_FATAL : ANDROID_LOG_ERROR, "Alice", "%s", content.c_str());
	if(fatal) {
		std::abort(); // std::exit is not safe from inside the Android app process
	}
}

// text services (IME) -- same no-ops used in window_nix.cpp; text input
// on Android will be handled via android_app / soft keyboard, not through this Windows COM interface.
win32_text_services::win32_text_services() { }
win32_text_services::~win32_text_services() { }
void win32_text_services::start_text_services() { }
void win32_text_services::end_text_services() { }
void win32_text_services::on_text_change(text_services_object* ts, uint32_t old_start, uint32_t old_end, uint32_t new_end) { }
void win32_text_services::on_selection_change(text_services_object* ts) { }
bool win32_text_services::send_mouse_event_to_tso(text_services_object* ts, int32_t x, int32_t y, uint32_t buttons) {
	return false;
}
void win32_text_services::set_focus(sys::state& win, text_services_object* o) { }
void win32_text_services::suspend_keystroke_handling() { }
void win32_text_services::resume_keystroke_handling() { }
text_services_object* win32_text_services::create_text_service_object(sys::state& win, ui::element_base& ei) {
	return nullptr;
}
void release_text_services_object(text_services_object* ptr) { }

} // namespace window
