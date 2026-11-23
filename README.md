# The House of the Dead EX Windows Local Builder

Build the Windows desktop port on your own PC using your own extracted arcade dump. This package contains source, scripts and launcher UI only. It contains no arcade executable, generated game code, game assets, screenshots, movies, audio, textures, cabinet save or prebuilt game executable.

**v1.1.1:** remembers the last successful dump and installation folders in manual Setup, with editable paths and Browse buttons. Includes the pause menu, control/lightgun settings, optional arcade Test Menu and verified launcher updater from v1.1.0.

**[Download the Windows builder](https://github.com/davidptrssvea/HOTD-EX-recomp/releases/latest)** Â· [Release notes](RELEASE-NOTES.md) Â· [Test coverage](BUILD-VERIFICATION.md) Â· [Report an issue](https://github.com/davidptrssvea/HOTD-EX-recomp/issues)

Other projects: [HOTD4](https://github.com/davidptrssvea/HOTD4-recomp) and [HOTD4 Special](https://github.com/davidptrssvea/HOTD-4-Special-recomp). Each game requires its own supported dump and installation.

## Install

1. Extract the entire builder ZIP into a short, writable folder, for example `D:\HOTDEX-Builder`. Do not run it inside the ZIP.
2. Run **Setup.cmd** and select your extracted HOTD EX dump (`disk0`, its parent, or `disk0\elf`). A lone ELF is insufficient: the complete `fs` data and both original sound libraries are required.
3. Leave Setup open while it verifies files, obtains its isolated build tools, generates the game code and compiles it locally. Progress and the full setup-log path are shown in the console.
4. Open **HOTDEX-PC\Play HOTD EX.exe**. The fullscreen menu includes Play Arcade, Settings, Arcade Test Menu, Updates and Other Games when installed HOTD4/Special launchers are found.

To choose a different installation or tool-cache location:

```bat
Setup.cmd -DumpPath "D:\My arcade dump" -InstallDir "D:\Games\HOTDEX-PC" -ToolchainPath "D:\HOTDEX-Tools\msys64"
```

The default installation is `HOTDEX-PC` beside Setup. Build tools default to `%LOCALAPPDATA%\HOTDEX-Builder\msys64`. No administrator access, system Python, Git, Visual Studio or separate xz tool is required. The original dump remains unchanged.

## Supported input

The currently verified HOTD EX executable is **hodexRI.elf**, 9,544,868 bytes, with SHA-256:

```text
6977E02F75DE33C51579AA43EBBDA104B05DC993F83CC98CAC745B8D023D0D14
```

Expected layout:

```text
disk0/
  elf/hodexRI.elf
  elf/librnalindbergh_jr.so
  elf/libcri_soundoutput_lindbergh_jr.so
  fs/compiled/...
  fs/afs/...
  fs/sfd/title_bg.sfd
  ...the rest of the complete fs tree...
```

Both sound libraries are also hash-verified; exact SHA-256 values are in `tools/dump-files.json`. Other ELF revisions or VGA/patched variants are not assumed compatible. Setup reports the actual and expected hash if a required file differs. No game files are downloaded.

## Requirements and setup reliability

- Windows 10/11 **x64**, Windows PowerShell 5.1 and the Windows .NET Framework 4.x C# compiler. Start with Setup.cmd so the correct PowerShell host and process-only execution policy are used.
- Internet for initial official MSYS2/compiler/dependency downloads and the pinned pyelftools wheel. Subsequent runs reuse verified tools and unchanged generated code. FFmpeg is installed in the isolated tool cache to extract a menu background from your own original title movie; no image is bundled.
- Allow roughly **12 GB free space beyond your original dump**, across the tool cache, extracted builder and installed game. The installed data copy alone is approximately 3.4 GB. Leave additional RAM/disk headroom for Windows; compiler workers are limited automatically by available RAM and CPU. Use `-Jobs 1` on a constrained machine.
- Gameplay needs a GPU driver with desktop OpenGL compatibility support. Rendering up to 4K is supported; no minimum CPU/GPU or frame-rate guarantee has been benchmarked. This builder targets the Windows desktop version; it does not advertise tested VR support for EX.
- Short writable local paths work best. Setup checks conflicting paths, write access and available space, and locks against simultaneous builds using the same builder, tool cache or install folder.

The official self-extracting MSYS2 archive is checked against its pinned size and SHA-256. Failed downloads are retried using an official mirror; checks are never bypassed. Missing compiler/header/Python dependencies are checked and repaired. pyelftools is obtained from a pinned, hash-verified wheel without relying on the removed MINGW64 pyelftools package or modifying system Python. DLL dependencies are resolved recursively from the chosen toolchain, including the dynamically loaded SDL3 runtime when MSYS2 selects sdl2-compat. See BUILD-VERIFICATION.md for tested coverage.

## Menu, controls and direct start

The launcher matches the HOTD4/Special menu style. Settings cover resolution (including custom sizes), fullscreen/windowed mode, MSAA Off/2x/4x, anisotropic filtering, mouse input/sensitivity/capture, extra crosshair, off-screen reload, sound and volume. The 3D scene renders at the selected resolution; original textures, video and HUD retain their original source detail. MSAA 8x is capped to 4x because it produced a black image in the tested EX configuration.

Default controls: **5 / 1** coin/start P1, **6 / 2** coin/start P2, **left mouse** shoot, **right mouse** reload/offscreen, **Space** pedal/action. **Start** skips logos, attract/story movies and minigame instructions, with an early press queued while loading finishes. **F1** releases/captures the mouse; **F2** toggles the extra mouse crosshair; **Alt+Enter / F11** toggles fullscreen. Game-native aiming indicators are separate.

Use **Settings.cmd** to open settings directly. **Play HOTD EX (Direct).cmd**, or `"D:\Games\HOTDEX-PC\Play HOTD EX.exe" --no-launcher`, starts with saved settings without the menu. `--skip-launcher` is an alias; `--settings` opens Settings. English and existing Swedish app-folder start files are included.

## Updating and keeping your data

Extract the new builder and rerun Setup with the same original dump and your **existing installation folder**:

```bat
Setup.cmd -InstallDir "D:\Games\HOTDEX-PC"
```

Your `HOTDEX_Recomp\lindbergh.ini`, control/calibration values, cabinet save and existing local menu background are preserved. Game data and program files are refreshed locally. Close the game before updating. Keep the whole installed HOTDEX-PC folder together.

After a successful build you may delete the extracted builder (including its local `build` and `generated` folders) and `%LOCALAPPDATA%\HOTDEX-Builder` tool cache. Gameplay does not need them. Keep your original complete arcade dump and installed game folder for future updates. A new builder can download the tools again.

## Troubleshooting

- Full setup logs: `%LOCALAPPDATA%\HOTDEX-Builder\Logs`. Game launch log: `HOTDEX-PC\HOTDEX_Recomp\hotdex-last-run.log`. Share the error and relevant log when reporting a problem; do not upload your dump or generated game code.
- Retry Setup after a network failure. Valid cached downloads are reused; an HTML/error page or truncated archive is rejected by size/hash checks.
- `Setup.cmd -RepairTools` preserves the previous MSYS2 cache and creates a fresh cache. It requires downloads. `-NoDownload` is available once all required tools and the verified Python wheel/module cache are present.
- `Setup.cmd -DumpPath "D:\My dump" -ValidateOnly` verifies the source package and required dump files without building/installing. It still writes a diagnostic setup log.
- Missing optional background: the installation remains usable with a plain dark menu. Rerun Setup to retry extraction; see `menu-background.log`.
- Do not change the system execution policy. The supplied launchers use `-ExecutionPolicy Bypass` for their own PowerShell process only.

## Source and licenses

The Lindbergh runtime, pcrecomp lifter and port source/licenses are under `source`. Setup generates game-specific C and library images from your verified files locally; generated code belongs to your installation/build and must not be redistributed. Dependency license notices are copied into the installed `HOTDEX_Recomp\licenses` folder. Package integrity checks detect accidental changes; they are not a substitute for obtaining this builder from a trusted source.

## Menu updates

The main menu checks GitHub for the latest stable release in the background. Open **Updates** to read the release notes and choose whether to download and install a newer version. Setup verifies the package and builds the update locally from the arcade dump in your installation. The menu reopens after installation.

The update replaces the launcher and runtime files. It keeps settings, control/device profiles, cabinet saves and the existing game dump. The update folder beside the installation holds the Setup log and the previous versions of replaced files. Internet access and free disk space for a local build are required.

To add the updater to an installation from an earlier release, run this builder's Setup and select the existing installation folder. Close the game before updating. The updater is available from the launcher; it is hidden while the game is paused.

## Remembered Setup folders

Manual Setup remembers your last successfully used **arcade dump** and **installation** folders, separately for HOTD4, Special and EX. Both paths are shown in the opening dialog; edit them or use **Browse** beside either field. Cancelled/failed builds and validation-only runs do not replace the last successful choices. Explicit command-line paths still take precedence, and the launcher updater remains automatic and does not change this history.

Folder history is stored locally in `%LOCALAPPDATA%\HOTD-Setup\Normal.json`, `Special.json` or `EX.json`. It contains paths only. A deleted/moved dump can be replaced using Browse; a corrupt history file falls back to the usual defaults. Settings and cabinet saves remain in your installed game folder.
