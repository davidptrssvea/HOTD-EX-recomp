# Windows local builder verification â€” 2026-11-23

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

## v1.1.0: priority features and existing updater integration

The pause, input and operator checks are detailed in `PRIORITY-CHANGES.md`. These include operator difficulty/life persistence, native Raw Input packet tests, launcher transitions and an assisted 14,000-frame co-op run reaching the first boss. They do not certify every minigame or physical lightgun/Parsec hardware.

The v1.0.1 updater was retained and merged with these changes, rather than replaced. Additional Windows PowerShell 5.1 checks on 2026-11-23:

- Real Setup, with the user's supported dump and an existing installation, built/installed both desktop and optional arcade operator. Existing INI, cabinet save, menu image and its provenance remained byte-identical. The installed updater and v1.1.0 version stamp matched the source.
- Real `-StageUpdate` Setup built both programs, runtime DLLs, launcher, UI, licenses and locally generated operator font without copying the game dump into the staging output.
- The updater's actual managed-file promotion installed that staged desktop/operator/font/UI output and preserved ten existing settings/profile/save/background/dump files byte-for-byte.
- Fixture tests exercised download/build/install coordination for all three supported updater editions, version stamping, reopening the menu, partial-install rollback and ZIP path traversal rejection. A corrupt-download test confirmed SHA-256 rejection before Setup or installed program replacement. The fixture tests do not substitute for game compilation; the real EX Setup checks above cover that separately.
- The merged WPF UI passed real binding capture, duplicate rejection, saved device/Sinden choices and visible-button bounds including the new Updates button. The updater is absent from the in-game pause UI. Its asynchronous live GitHub release lookup returned valid EX release metadata without blocking the UI.
- Repeated native pause/resume with the merged production UI: F9 opened the real owned WPF child menu during gameplay, the menu saved a pedal binding and Sinden option, and Resume returned at exactly the same native frame. The border applied and gameplay continued to frame 2,800, exit 0.
- The updater checks for a running EX operator as well as the game before replacing programs. Source-only manifest and deterministic ZIP checks exclude game code generated from the dump, original executables/libraries, textures, images, movies, saves and compiled game binaries.

The new release remains a desktop Windows build. No EX VR/headset test, physical two-lightgun certification, full all-stage playthrough or new Windows 10 LTSC test is claimed.
