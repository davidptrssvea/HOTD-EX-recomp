# HOTD EX Windows Local Builder v1.0.0

The first full Windows local-builder release for **The House of the Dead EX**.

Download **HOTDEX-Local-Builder-v1.0.0.zip**, extract the entire ZIP and run **Setup.cmd** with your own complete, supported EX arcade dump. Then open **HOTDEX-PC\Play HOTD EX.exe**. No game files are downloaded or included.

- **Local build:** verifies hodexRI.elf and both original sound libraries, generates their C code and compiles the Windows game on your own PC. Your original dump is left unchanged.
- **Native-resolution rendering:** the 3D scene uses the selected resolution, including 4K, with corrected viewport/scissor scaling. Original textures, HUD artwork and video keep their original source detail.
- **Matching launcher:** fullscreen menu in the HOTD4/Special style, current control guide, Settings and shortcuts to the other installed games when found. The background is extracted locally from your own title movie.
- **Settings:** resolution/custom sizes, fullscreen/windowed, MSAA Off/2x/4x, AF, Desktop Cursor/Raw Relative mouse input, sensitivity/capture, extra crosshair, off-screen reload and audio/volume.
- **Start-to-skip:** skip logos, attract/story movies and minigame instructions. An early press is queued while required loading completes; holding Start does not repeatedly skip later sequences.
- **Windowless/direct launch:** use Play HOTD EX.exe, Settings.cmd or Play HOTD EX (Direct).cmd. The EXE also accepts --no-launcher / --skip-launcher and --settings.
- **Setup improvements learned from HOTD4/Special:** pinned archive-size/SHA-256 verification, download retries/official mirror, isolated pyelftools installation, dependency/cache checks and repair, RAM-based compiler worker limits, progress/phase logs, path/free-space checks and concurrent-install locks. No system xz tool or separately installed Python/compiler is required. Dynamically loaded SDL3 dependencies are included when the current MSYS2 SDL2 compatibility package is used.
- **Updates preserve user data:** rerun Setup with the same dump and existing -InstallDir to retain settings, custom bindings/calibration, cabinet saves and the generated menu background.

**Supported input:** hodexRI.elf, 9,544,868 bytes, SHA-256 `6977E02F75DE33C51579AA43EBBDA104B05DC993F83CC98CAC745B8D023D0D14`, the complete fs tree and both verified sound libraries. See README for exact layout and requirements. A lone ELF is not sufficient; other patched/VGA/revision variants are not assumed compatible.

**Requirements:** Windows 10/11 x64, Windows PowerShell 5.1/.NET Framework 4.x compiler, internet for the initial tools/dependencies, approximately 12 GB free beyond the original dump, and a compatible desktop OpenGL GPU/driver for gameplay. This release is the desktop version; EX OpenXR/headset support is not advertised as tested.

**Validation:** clean tool-cache bootstrap and local generation of 45 game C units plus both sound libraries, all 90 native compilation/link steps, installed EXE launch and a 2,700-frame assisted run into the first playable minigame, menu/settings save and reopen, and an offline update preserving user data. Download/cache/path/lock/dependency error cases were also tested. See BUILD-VERIFICATION.md for coverage and limits; this was not a full playthrough or a separate Windows 10 LTSC/headset test.

The downloadable package contains **no arcade ELF/libraries, game assets, images from the game, movies, audio, textures, lifted game code, cabinet save or prebuilt game executable**. All game processing and background extraction happen on the user's PC.
