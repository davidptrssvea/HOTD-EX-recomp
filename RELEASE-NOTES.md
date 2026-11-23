# HOTD EX v1.1.2 — V-Sync

Full Windows local builder. **No game assets, original arcade executables or generated game code are included.** Setup builds locally using your own supported extracted arcade dump.

- **V-Sync Off / On:** choose it in Settings > Graphics, save, and restart the game. Off remains the default. The active driver setting is recorded in the game log.

All previous features are retained, including the built-in launcher updater and remembered dump/installation paths with Browse. Existing settings, bindings, cabinet saves are preserved when updating.

The desktop display options do not change OpenXR headset timing or its view. This release does not claim a new physical-headset or CRT hardware test.

## Updating

Open **Updates** in the launcher and choose **Download and install**. The updater rebuilds locally using your installed dump and reopens the menu afterward.

Alternatively, extract the new full builder ZIP, run **Setup.cmd**, select the same supported dump and choose your existing installation folder. Your existing settings and saves are kept. Setup remembers the last successful folders and still offers Browse.

## Verification

Real Windows/OpenGL runs accepted both V-Sync settings and read back the active driver interval. Complete WPF menu checks cover saving/reopening preferences, visible controls, bottom buttons and updater dependencies.
