package org.projectalice.mobile;

import android.app.AlertDialog;
import android.app.NativeActivity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.DocumentsContract;
import android.provider.Settings;
import android.graphics.Typeface;
import android.view.Gravity;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.LinearLayout;
import android.widget.PopupWindow;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

/**
 * NativeActivity with the minimum that C++ cannot do on its own:
 * requesting file access, opening Android's folder picker and showing
 * messages. Everything else (window, EGL, touch, game) stays in libAlice.so.
 *
 * The public methods are called from C++ (entry_point_android.cpp), from
 * another thread -- that is why everything that touches the UI goes through runOnUiThread.
 */
public class AliceActivity extends NativeActivity {
	private static final int REQUEST_PICK_FOLDER = 1;

	// NativeActivity loads the .so on its own (dlopen), which does not
	// register this class's "native" methods in the JNI; loading it also through
	// System.loadLibrary solves it (it is the same .so, it does not load twice)
	static {
		System.loadLibrary("Alice");
	}

	private static native void nativeOnGameFolderPicked(String path);

	private boolean waitingForAllFilesAccess = false;
	private AlertDialog folderDialog = null;

	private final Handler uiHandler = new Handler(Looper.getMainLooper());
	private AlertDialog loadingDialog = null;
	private PopupWindow statsPopup = null;
	private TextView statsText = null;
	private Runnable pendingLoadingDialog = null;
	private TextView loadingText = null;
	private String loadingMessage = null;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		hideSystemBars();
	}

	// The system shows the bars again in several situations (dialogs,
	// keyboard, coming back from another app); hidden again when focus is regained.
	@Override
	public void onWindowFocusChanged(boolean hasFocus) {
		super.onWindowFocusChanged(hasFocus);
		if(hasFocus)
			hideSystemBars();
	}

	/**
	 * Immersive mode: hides the status and navigation bars (which in
	 * landscape sit on the side, over the game). Swiping from the edge
	 * shows them for a few seconds, without taking the game out of fullscreen.
	 */
	private void hideSystemBars() {
		Window window = getWindow();
		window.setDecorFitsSystemWindows(false);
		WindowInsetsController controller = window.getInsetsController();
		if(controller != null) {
			controller.hide(WindowInsets.Type.systemBars());
			controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
		}
	}

	@Override
	protected void onResume() {
		super.onResume();
		if(waitingForAllFilesAccess) {
			waitingForAllFilesAccess = false;
			if(Environment.isExternalStorageManager()) {
				launchFolderPicker();
			} else {
				showFolderDialog("Without the \"All files access\" permission the game cannot read the Victoria 2 folder.");
			}
		}
	}

	/** Called by C++ when no valid game folder is configured. */
	public void requestGameFolder(final String reason) {
		runOnUiThread(() -> showFolderDialog(reason));
	}

	/**
	 * Loading window, requested by C++ (change_cursor "busy") before
	 * jobs that block the game thread -- e.g.: creating the interface when
	 * entering the game. It is drawn by the system, so it appears and animates
	 * even with the game frozen, and while it is open the touches go to it:
	 * without that Android showed "the app is not responding". It only appears if the
	 * job takes more than 300 ms, so quick loads do not flicker.
	 * It also shows the app startup steps (extracting files, assembling and
	 * loading the scenario): with it open, a new message just replaces the text.
	 * An empty message closes it.
	 */
	public void setLoading(final String message) {
		runOnUiThread(() -> {
			if(message == null || message.isEmpty()) {
				loadingMessage = null;
				if(pendingLoadingDialog != null) {
					uiHandler.removeCallbacks(pendingLoadingDialog);
					pendingLoadingDialog = null;
				}
				if(loadingDialog != null) {
					loadingDialog.dismiss();
					loadingDialog = null;
					loadingText = null;
				}
				return;
			}
			// open or about to open: just replaces the text (loading steps)
			loadingMessage = message;
			if(loadingDialog != null) {
				loadingText.setText(message);
				return;
			}
			if(pendingLoadingDialog != null)
				return;
			pendingLoadingDialog = () -> {
				pendingLoadingDialog = null;
				showLoadingDialog(loadingMessage);
			};
			uiHandler.postDelayed(pendingLoadingDialog, 300);
		});
	}

	private void showLoadingDialog(String message) {
		if(isFinishing() || message == null)
			return;
		int padding = (int)(24 * getResources().getDisplayMetrics().density);
		LinearLayout layout = new LinearLayout(this);
		layout.setOrientation(LinearLayout.HORIZONTAL);
		layout.setGravity(Gravity.CENTER_VERTICAL);
		layout.setPadding(padding, padding, padding, padding);
		ProgressBar spinner = new ProgressBar(this);
		spinner.setIndeterminate(true);
		layout.addView(spinner);
		loadingText = new TextView(this);
		loadingText.setText(message);
		loadingText.setTextSize(18);
		loadingText.setPadding(padding, 0, 0, 0);
		layout.addView(loadingText);
		loadingDialog = new AlertDialog.Builder(this)
			.setView(layout)
			.setCancelable(false)
			.create();
		loadingDialog.setCanceledOnTouchOutside(false);
		// A new dialog window brings the system bars back. Opens
		// without focus, with the bars already hidden, and only then receives focus.
		Window window = loadingDialog.getWindow();
		if(window != null)
			window.setFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE, WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
		loadingDialog.show();
		if(window != null) {
			WindowInsetsController controller = window.getInsetsController();
			if(controller != null) {
				controller.hide(WindowInsets.Type.systemBars());
				controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
			}
			window.clearFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
		}
	}

	/**
	 * Performance counter (turned on with a three-finger touch), updated by the
	 * native thread once per second. It sits in its own window on top of the
	 * game, because the NativeActivity draws straight to the surface and does not show
	 * regular views. It receives no touches. An empty text closes it.
	 */
	public void setStats(final String text) {
		runOnUiThread(() -> {
			if(text == null || text.isEmpty()) {
				if(statsPopup != null) {
					statsPopup.dismiss();
					statsPopup = null;
					statsText = null;
				}
				return;
			}
			if(statsPopup == null) {
				if(isFinishing())
					return;
				int padding = (int)(4 * getResources().getDisplayMetrics().density);
				statsText = new TextView(this);
				statsText.setTextColor(0xFFFFFFFF);
				statsText.setBackgroundColor(0xA0000000);
				statsText.setTypeface(Typeface.MONOSPACE);
				statsText.setTextSize(12);
				statsText.setPadding(padding, padding, padding, padding);
				statsPopup = new PopupWindow(statsText, ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT, false);
				statsPopup.setTouchable(false);
				statsText.setText(text);
				statsPopup.showAtLocation(getWindow().getDecorView(), Gravity.TOP | Gravity.END, padding, padding);
				return;
			}
			statsText.setText(text);
		});
	}

	/** Short message on screen (e.g.: "Creating the scenario..."). */
	public void showMessage(final String message) {
		runOnUiThread(() -> Toast.makeText(this, message, Toast.LENGTH_LONG).show());
	}

	private void showFolderDialog(String reason) {
		if(folderDialog != null && folderDialog.isShowing())
			folderDialog.dismiss();
		String message = "Project Alice by Alex needs the Victoria 2 files (with the expansions).\n\n"
			+ "Copy the game folder to the device and choose it on the next screen "
			+ "(the folder that contains \"common\", \"map\" and \"gfx\").";
		if(reason != null && !reason.isEmpty())
			message = reason + "\n\n" + message;
		folderDialog = new AlertDialog.Builder(this)
			.setTitle("Victoria 2 folder")
			.setMessage(message)
			.setCancelable(false)
			.setPositiveButton("Choose folder", (d, w) -> ensureAccessThenPick())
			.setNegativeButton("Quit", (d, w) -> finish())
			.show();
	}

	private void ensureAccessThenPick() {
		// minSdk 31: direct access (file paths, which C++ uses) to
		// folders outside the app requires "All files access"
		if(Environment.isExternalStorageManager()) {
			launchFolderPicker();
			return;
		}
		waitingForAllFilesAccess = true;
		try {
			startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
				Uri.parse("package:" + getPackageName())));
		} catch(Exception e) {
			startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
		}
	}

	private void launchFolderPicker() {
		Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
		try {
			startActivityForResult(intent, REQUEST_PICK_FOLDER);
		} catch(Exception e) {
			showFolderDialog("Could not open this device's folder picker.");
		}
	}

	@Override
	protected void onActivityResult(int requestCode, int resultCode, Intent data) {
		super.onActivityResult(requestCode, resultCode, data);
		if(requestCode != REQUEST_PICK_FOLDER)
			return;
		if(resultCode != RESULT_OK || data == null || data.getData() == null) {
			showFolderDialog("No folder was chosen.");
			return;
		}
		String path = treeUriToPath(data.getData());
		if(path == null) {
			showFolderDialog("Choose a folder from the device storage or the SD card (not from a cloud service).");
			return;
		}
		nativeOnGameFolderPicked(path);
	}

	/**
	 * content://com.android.externalstorage.documents/tree/primary%3AGames%2FVictoria%202
	 *   -> /storage/emulated/0/Games/Victoria 2
	 * SD card ("XXXX-XXXX:folder") -> /storage/XXXX-XXXX/folder
	 */
	static String treeUriToPath(Uri uri) {
		if(!"com.android.externalstorage.documents".equals(uri.getAuthority()))
			return null;
		String docId;
		try {
			docId = DocumentsContract.getTreeDocumentId(uri);
		} catch(Exception e) {
			return null;
		}
		int colon = docId.indexOf(':');
		if(colon < 0)
			return null;
		String volume = docId.substring(0, colon);
		String relative = docId.substring(colon + 1);
		String base = "primary".equalsIgnoreCase(volume)
			? Environment.getExternalStorageDirectory().getAbsolutePath()
			: "/storage/" + volume;
		return relative.isEmpty() ? base : base + "/" + relative;
	}
}
