# HOTD EX v1.1.1 — remembered Setup folders

Full Windows local builder, with the built-in launcher updater retained. No game assets, original arcade executables or generated game code are included; use your own supported dump.

- **Manual Setup remembers your last successful dump and installation folders.** Both remain editable, with a separate **Browse** button for each.
- **Separate history for each game:** HOTD4, Special and EX do not overwrite one another's folder selections.
- Cancelled/failed installations, validation-only runs and automatic staged updates leave the previous successful choices intact. Explicit command-line paths still work as before.
- Existing settings, bindings, cabinet saves and game assets are preserved during updates.

## Updating

Open **Updates** in the launcher and choose **Download and install**. The new release is built locally using the dump already in your installation, and the menu reopens afterward.

Alternatively, extract the new full builder ZIP and run **Setup.cmd** with your supported dump and existing installation folder. Select the folders once on your first manual run of this release; subsequent manual setups prefill them. The new manual path history is created only after a successful installation.

All three projects now include the launcher updater. Updating requires internet access, free disk space and time for the local build.
