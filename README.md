# Project Alice mobile

Android version of [Project Alice](https://github.com/schombert/Project-Alice), an open-source engine for playing **Victoria 2**. The game's C++ code runs natively on the phone (ARM64, OpenGL ES 3.2), with touch controls.

## ⬇️ Download

**[Download the latest version (APK)](https://github.com/Vitorio-Kuroski/Project-Alice-mobile/releases/latest/download/ProjectAlice-Android-arm64.apk)**

[What's new and previous versions](https://github.com/Vitorio-Kuroski/Project-Alice-mobile/releases)

> **Test version.** It already runs on a real device (Moto One Hyper, Android 11, Adreno 612), but expect problems.

## Requirements

- Android 11 or newer, **64-bit ARM** phone or tablet
- GPU with OpenGL ES 3.2
- The **Victoria 2 files with the expansions**, copied to the device. The original game content is not included.

## How to install

1. Download the APK from the link above.
2. Copy the Victoria 2 folder to the device, for example to `Jogos/Victoria 2`. The right folder is the one containing `common`, `map` and `gfx`. Avoid the `Download` folder and the root of the storage, which the Android picker does not let you choose.
3. Install the APK. You need to allow installation from unknown sources.
4. Open the app, tap **Choose folder**, turn on **All files access**, go back to the app and choose the game folder.
5. The first time, the app builds the scenario, which can take several minutes.

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

## More information

[**android/README.md**](android/README.md) has the rest: where saves and settings live, performance, known limitations, how to build, and how the port works internally.

## Problems?

Open an [issue](https://github.com/Vitorio-Kuroski/Project-Alice-mobile/issues) with a description. If you can, attach the output of `adb logcat -s Alice`.
