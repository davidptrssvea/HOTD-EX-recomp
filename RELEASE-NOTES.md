# HOTD EX v1.1.7 - Updater and launcher fixes

- Retry temporary Windows file replacement errors during updates. If the error persists, the updater attempts to restore any files it already replaced and records the affected path and Windows error code in `update-error.log`.
- Show the installed builder version directly in the main menu.
- Fix **Free Play (unlimited credits)** and **Mute Start and coin sounds** when the Windows settings path contains non-ASCII characters.

Use **Updates > Download and install** from a working launcher. If the previous updater failed, close the launcher and game, extract this release's ZIP into a separate folder, and run `Setup.cmd -InstallDir "D:\Games\HOTDEX-PC"` with your existing installation path. Select the same arcade dump to keep your settings and saves.
