Test version of **Project Alice for Android**: the open-source engine for playing Victoria 2, running natively on the phone.

> ⚠️ **Test version.** It already runs on a real device (Moto One Hyper, Android 11, Adreno 612), but expect problems. Reports are welcome.

## Requirements

- Android 11 or newer, **64-bit ARM** phone or tablet
- GPU with OpenGL ES 3.2
- The **Victoria 2 files with the expansions**, copied to the device. The original game content is not included.

## How to install

1. Download the `.apk` file below, under **Assets**.
2. Copy the Victoria 2 folder to the device, for example to `Jogos/Victoria 2`. The right folder is the one containing `common`, `map` and `gfx`. Avoid the `Download` folder and the root of the storage, which the Android picker does not let you choose.
3. Install the APK. You need to allow installation from unknown sources.
4. Open the app, tap **Choose folder**, turn on **All files access**, go back to the app and choose the game folder.
5. The first time, the app builds the scenario, which can take several minutes.

**Already have a previous version installed?** This version does not install over it: uninstall the previous one first. Uninstalling deletes saves and settings; to keep the saves, first copy the `Android/data/org.projectalice.mobile/files/.local/share/Alice/` folder over the USB cable and put it back after opening v0.6 once.

## Controls

| Gesture | Equivalent to |
|---|---|
| Quick tap | Left click |
| Press and hold still for ~0.5 s | Right click |
| Drag over the interface | Drag with the left button |
| Drag over the map | Move the map |
| Two-finger pinch | Zoom |
| Three-finger tap | Shows or hides the performance counter (FPS) |
| Back | ESC (menu) |

Physical mouse and keyboard also work.

## What's new in this version

- **"Loading" window during the whole startup:** the first time, building the scenario takes several minutes. Before, the message went away within seconds and the screen stayed blue and empty, looking frozen. Now the window stays open until the game appears and shows the current step (preparing the files, building the scenario, loading, starting).
- The loading window no longer brings Android's navigation bar back on top of the game.
- It keeps everything from v0.5: faster response to touch with the game running and the performance counter (three-finger tap).

## Known limitations

- No mod selection; base game only.
- It is not possible to type text (save name, chat).
- Box selection of several units with a finger does not exist yet.
- Entering a game takes a few seconds on a mid-range phone.

## Problems?

Open an issue with the description. If you can, attach the output of `adb logcat -s Alice`.
