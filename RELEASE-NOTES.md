# HOTD EX v1.1.0 — Pause, controls and arcade settings

Full Windows local builder. No game assets, original executables, generated game code or prebuilt game binaries are included. Use your own supported, complete HOTD EX arcade dump.

- **In-game pause menu:** Esc freezes the game clock and audio. Resume, change settings, restart, return to the launcher or open the Arcade Test Menu.
- **Configurable controls:** keyboard/mouse bindings, duplicate-key checks, separate P2 pedal/shoot/reload keys and an up-to-date controls guide.
- **Lightgun settings:** separate Raw Input devices, mixed Gun P1 + Mouse P2 modes and configurable Sinden border widths, including on the menus.
- **Original Arcade Test Menu:** locally builds the optional `hodextestR.elf`. Change difficulty, initial/max life and other cabinet settings; these use the game's existing save. Local Windows fonts provide a fallback for missing boot resources.
- **Existing menu updater included:** checks for stable GitHub releases, verifies the ZIP, builds locally from the installed dump, backs up replaced program files and retains settings, device profiles, saves and assets. Updates are hidden in the pause overlay.
- **Windowless start files:** use `Play HOTD EX.exe`, or the Direct shortcut to bypass the launcher.

## Updating

If you already have v1.0.1, open **Updates** in the start menu and choose **Download and install**. Keep the menu open while the local build runs; it reopens when installation is complete.

For older installations, extract `HOTDEX-Local-Builder-v1.1.0.zip`, run `Setup.cmd` with the same supported arcade dump, and choose your existing installation folder. Existing settings, cabinet saves and local menu background are preserved. An internet connection, disk space and time for the local build are required.

The optional arcade Test Menu requires `elf/hodextestR.elf`, SHA-256 `3B9EEF550528D6DB8F092E947990AFC3A414A900FE3648866F9D4E9B98F952A8`. If an older installation does not contain it, rerun Setup with your complete original dump to add it. Without it, gameplay and updates still work; the Test Menu button explains the missing input.

## Validation and limits

Tested native pause/resume, configurable bindings and device routing, cabinet setting persistence, launcher transitions, cached local installation and preservation of existing user data. An assisted 14,000-frame co-op run reached the first boss through multiple shooting and pedal minigames. This is not a complete all-minigame playthrough or a physical two-lightgun/Parsec test. This release targets desktop Windows; EX VR is not advertised as tested. See `BUILD-VERIFICATION.md` and `PRIORITY-CHANGES.md`.
