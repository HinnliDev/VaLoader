<div align="center">

# Valoader

[![License](https://badgen.net/github/license/HinnliDev/VaLoader)](LICENSE)

<img width="1170" height="520" alt="example" src="https://github.com/user-attachments/assets/a54bcbf0-4dd7-4552-a77a-3617e03a80db" />

Valoader is a compact Android launcher with an ImGui skeleton ESP for the
arm64 build of Valorant Mobile (`com.tencent.tmgp.codev`). It runs the game in
the loader process while reusing the APK resources, assets and native
libraries from the installed game.
</div>

---

## Supported build

- Valorant Mobile 1.24.0 (`versionCode 2911`)
- Unreal Engine 4.26.1
- arm64-v8a
- Android 11 or newer

## Features

- Starts the already installed game from a separate loader package.
- Mounts the installed game's resources and native library path.
- Draws an ImGui skeleton ESP for living enemy characters only.
- Keeps overlay input separate from the game viewport while the menu is open.

## Project layout

- `app/src/main/java/.../loader` — installed-game context and startup sequence.
- `app/src/main/java/.../overlay` — Android surface and touch routing.
- `app/src/main/cpp/game` — read-only UE4 world and character discovery.
- `app/src/main/cpp/overlay` — ImGui menu and skeleton rendering.
- `app/src/main/cpp/platform` — JNI and EGL/OpenGL ES integration.
- `tools/assemble-valoader.ps1` — local APK assembly and signing.

## Build

Requirements:
- JDK 17
- Android SDK 36
- Android NDK 29.0.14206865
- CMake 3.22.1
- an unpacked copy of the matching game containing `classes.dex`,
  `classes2.dex`, `classes3.dex` and `lib/arm64-v8a/libtprt.so`

Build a debug APK:
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\assemble-valoader.ps1 `
    -GameDirectory "C:\path\to\unpacked-game"
```

The result is written to `dist/Valoader-debug.apk`.

For a release build, set `VALOADER_KEYSTORE`, `VALOADER_KEY_ALIAS`,
`VALOADER_KEYSTORE_PASSWORD` and `VALOADER_KEY_PASSWORD`, then pass
`-Configuration Release`.

## Version updates

Version-specific offsets are isolated in
`app/src/main/cpp/game/VersionProfile.hpp`. Revalidate the complete world,
camera, player-state and skeletal-mesh chains after every game update.

## License

See [LICENSE](LICENSE) for the license terms.

