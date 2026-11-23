# Windows local builder verification — 2026-11-23

Tested on Windows 11 x64 with **Windows PowerShell 5.1**. The package is for the desktop EX port.

## Clean setup and local generation

- Started with an empty installation directory and a new isolated MSYS2 cache. Both builder and installation paths contained spaces.
- Downloaded the official 43,117,824-byte MSYS2 self-extracting archive and verified its pinned SHA-256. Installed the compiler, runtime and FFmpeg packages through MSYS2. No system tar/xz utility or preinstalled Python/compiler was used.
- Downloaded and verified pyelftools 0.33 automatically from the pinned wheel. No removed MINGW64 pyelftools package or manual pip workaround was needed.
- Generated **45 game C units** plus **81 functions from the two sound libraries** locally from the verified arcade files. All original game units and sound-library outputs matched the existing working EX build. The generated OpenGL wrappers include the viewport/scissor and AF adjustments.
- Completed all **90 native compile/link steps**. Fixed and retested Windows C# compiler path handling for paths containing spaces. Created the windowless EXE launcher and direct-start/settings shortcuts.
- Detected MSYS2's SDL2 compatibility runtime and included its dynamically loaded **SDL3.dll**, which is not listed in SDL2.dll's PE import table. Added a missing-SDL3 cache check and automatic dependency installation.
- Copied all **2,382** arcade data files; relative paths and byte sizes matched the source (3,376,883,269 bytes). Verified the source ELF and both original sound-library hashes remained unchanged.
- Extracted the title-movie background locally with the isolated FFmpeg tool. No GPU rendering or included screenshot was required to create the menu background.

## Installed game and menu

- Launched the installed **Play HOTD EX.exe --no-launcher**, through the common PowerShell launcher, into the native EX executable.
- Completed an assisted **2,700-frame** native run at **1280 x 720**, including the opening and the first playable apple-shooting minigame. Captured and visually checked frame 2,400; the scene, characters and HUD were visible. The queued Start skip was applied during the run.
- Loaded the installed WPF menu with its locally extracted background and current control guide. Checked navigation, settings saving and reopening, including a custom 3840 x 2160 selection, fullscreen, MSAA 2x, AF 8x and a non-default volume.
- Tested the real Setup.cmd entry point, including paths containing spaces and process-only execution-policy bypass.

## Updates and recovery

- Reran setup with **-NoDownload** against the existing installation, deliberately supplying invalid inherited PYTHONHOME, PYTHONPATH, CC and CFLAGS values. Setup cleared these build overrides and completed successfully.
- Verified unchanged generated-code timestamps/cache reuse and no unnecessary native rebuild.
- Verified byte-identical preservation of settings, custom bindings/calibration and an unknown user option, cabinet NVRAM, the local menu JPEG and its provenance file.
- Tested checksum rejection of a truncated HTTP response, fallback to a second server and offline reuse of an already verified archive using a local HTTP fixture under Windows PowerShell 5.1.
- Tested package-integrity failure, unsupported ELF hash rejection, missing tools, missing dynamically loaded SDL3, concurrent-install exclusion, protected/overlapping paths and RAM-based compiler worker limits.
- Checked additional-space estimates aggregated per volume, and source-only ZIP contents against the package manifest. Generated code, build outputs, arcade binaries, saves, screenshots and game assets are excluded.

## Coverage limits

This was an assisted startup/first-minigame test, not a complete playthrough or physical two-lightgun test. The new EX setup was not separately run inside Windows 10 LTSC. No headset/OpenXR validation or hardware minimum/FPS benchmark is claimed. Native 4K rendering was verified in the earlier EX renderer work; this installer verification tested native gameplay at 720p and menu persistence of the 4K settings.
