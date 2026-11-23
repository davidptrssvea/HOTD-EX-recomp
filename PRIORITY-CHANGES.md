# HOTD EX priority update â€” local, not yet released

This release adds the following to the desktop EX port and retains the verified updater introduced in v1.0.1.

## Pause menu

Esc pauses at a completed frame boundary and opens an owned, translucent EX menu over the frozen game. The guest clock and game audio remain paused while the menu and its Settings pages are open. Resume, Restart Arcade, Arcade Test Menu, Main Menu and Quit are available. The menu uses the same visual style as the launcher and does not use Windows screen-recording/capture APIs.

Saved key bindings, extra-crosshair visibility, audio and the Sinden border reload on Resume. Restart Arcade applies graphics/display and input-mode/device ownership changes. Keyboard capture does not leak its clicks/held keys into gameplay on Resume. EX does not enable HOTD4-specific score/trophy/photo-memory hooks or change its existing extra-crosshair appearance.

## Controls and lightguns

Settings > Key Bindings captures keyboard or allowed mouse buttons, rejects duplicate assignments and shows the active controls in the main menu. The default extra keyboard pedal bindings are Space for P1 and Right Ctrl for P2. P2 also has Numpad 0 / Numpad decimal as additional keyboard Shoot / Reload bindings. These do not create a second mouse aim by themselves: select the appropriate co-op/lightgun mode and device for P2.

Settings > Lightguns / Sinden offers absolute mouse, raw relative mouse, separate Raw Input lightguns and mixed Gun P1 + Windows/Parsec Mouse P2 modes. Select distinct physical devices for two guns; Auto assigns distinct absolute devices. Explicit device selection also supports relative devices. Unplugged saved devices remain identifiable instead of silently changing assignments. Lightgun mouse buttons retain their standard left=shoot, right=reload, middle=pedal mapping; keyboard pedal bindings provide additional controls.

The Sinden border can be toggled and has Extra Thin / Thin / Medium / Original widths, with Thin (half the original 3% height) as the default. It also appears on the launcher and pause/settings overlay so those menus remain trackable. A legacy MSAA 8x profile is capped to EX's supported 4x mode. Off-screen reload settings are loaded when the game starts, not only after the first pause.

## Original arcade Test Menu

The optional `elf/hodextestR.elf` must have SHA-256 `3B9EEF550528D6DB8F092E947990AFC3A414A900FE3648866F9D4E9B98F952A8`. Setup recompiles it locally as hotdex-testmenu.exe. The menu button and mapped Test key (default T) open the actual arcade operator program. Service (default S) selects; Test confirms. Game Assignments exposes difficulty, initial/max life and advertise sound. Both game and operator use the same installed cabinet save.

The operator's required boot font/logo is read from the user's dump if present. A local Windows-font fallback is generated when those separate boot resources are absent. No font atlas or game logo is bundled. Without the supported optional operator input, the launcher explains why the button is disabled; pressing Test keeps the game running instead of exiting into a missing program.

## Validation

- Real WPF key capture, duplicate/cancel behavior, saved device choices, duplicate-device rejection, Sinden widths, main-menu control guide, settings reopen and page/button bounds.
- Native pause with the remapped F9 key during a playable minigame. The game opened and closed the menu at the identical frame number while two UI timer ticks elapsed. A pedal remap and Sinden setting were saved in the real child menu, Resume applied the border, and the game continued to frame 2,800. This also covered an inherited GL-thread request; EX keeps the pause-compatible event/render path.
- Private native tests fed distinct Raw Input packets through the production input implementation: separate P1/P2 aim, trigger/release/pedal state, automatic/explicit relative-device assignment, mixed-coop keyboard pedals and off-screen reload isolation. Private test hooks are not in the installed executables or builder source.
- The original operator rendered successfully; difficulty/life changes modified cabinet NVRAM and rendered identically when reopened in a new process. A subsequent assisted co-op run used that save and displayed three initial hearts per player.
- A 14,000-frame assisted co-op run loaded 46 asset banks and reached the first boss through apple/spider shooting and pedal minigames. Fourteen scene captures were checked. This is not a completed run through every minigame/stage/ending.
- The native Test button requests the operator transition when installed. With the optional operator missing, it leaves the game running. Launcher process-routing tests cover Restart, operator entry/return, shared save paths and return to the main menu.
- Cached setup rebuilt/installed the updated desktop and operator programs from the user's own verified files. The input/menus and source changes remain asset-free.

No physical pair of lightguns, Parsec session or complete all-minigame playthrough has been verified in this update. Those device/session tests still need real hardware/user feedback. The current work targets desktop Windows, not EX VR.
