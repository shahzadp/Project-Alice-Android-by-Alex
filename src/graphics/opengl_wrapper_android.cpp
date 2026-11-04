// opengl_wrapper_android.cpp
//
// Equivalent to opengl_wrapper_nix.cpp. There the context is created by
// glfwCreateWindow and this file only makes it current; on Android the context
// and the EGL surface are created by window_android.cpp (APP_CMD_INIT_WINDOW),
// so here we likewise only make sure it is current on this thread.
// There is no GLEW: the GLES functions come straight from libGLESv3 (see
// android_gl/glew.h).

#include "opengl_wrapper.hpp"
#include "system_state.hpp"

#include <EGL/egl.h>

namespace ogl {

void create_opengl_context(sys::state& state) {
	assert(state.win_ptr && state.win_ptr->egl_context != EGL_NO_CONTEXT);
	auto& win = *state.win_ptr;

	if(!eglMakeCurrent(win.egl_display, win.egl_surface, win.egl_surface, win.egl_context)) {
		window::emit_error_message("eglMakeCurrent falhou ao iniciar o OpenGL ES", true);
	}
	eglSwapInterval(win.egl_display, 1); // Vsync

#ifndef NDEBUG
	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(debug_callback, nullptr);
	glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
	glDebugMessageControl(GL_DONT_CARE, GL_DEBUG_TYPE_OTHER, GL_DEBUG_SEVERITY_LOW, 0, nullptr, GL_FALSE);
#endif
}

// o contexto EGL e destruido pelo window_android.cpp quando o app fecha
void shutdown_opengl(sys::state& state) { }

} // namespace ogl
