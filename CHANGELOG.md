# Changelog

All notable changes to IR Cloner for the M5Stack Cardputer ADV.

## v1.2.0

### Added

- **Per-button hotkeys.** Give any button a keyboard key (`Enter`, the arrow keys, letters, digits, `- = [ ] \ '`, `Space`) and send it by pressing that key. `Esc`, `Tab` and `Del` are reserved.
- **Compact key list remote screen** with up to 21 buttons per page, `Tab` to change page, and a short flash when a button is sent. Remotes with hotkeys open here. Remotes without hotkeys open in the original button grid.
- **Hotkeys stored in the `.ir` file** as a `# hotkey: KEY` comment under the button's `name:` line. Files stay Flipper compatible and can be edited by hand.
- **Automatic keys** for buttons in a hotkey remote that have none (`1`-`9`, `0`, then letters), shown in grey and never written to the file.
- **Edit button screen** with a name field and a hotkey field. After typing the name, `Enter` moves straight to the hotkey and waits for a key press, and `Enter` again saves and returns to the list. A key already in use can be taken over from the other button.
- **Edit saved remotes.** Press `E` on a remote in the Remotes list to rename buttons and set or change hotkeys. Saving overwrites the file in place, or writes a new file if the remote is renamed.
- **Raw capture** for remotes whose protocol IRremote cannot decode. Raw timings are recorded and saved as Flipper `type: raw`, including remotes that send a frame twice (such as Dyson fans).
- **More protocols:** Apple, Denon, Sharp, Samsung48, Kaseikyo Denon / Sharp / JVC / Mitsubishi, BoseWave and FAST.
- **Scrolling and truncated names.** Long remote names scroll in the title and on the selected row of the file lists, and are shortened with `..` elsewhere.

### Changed

- The Remotes list footer now reads `[ENTER] Load [E] Edit [D] Delete`.
- Hotkeys are recognised by the physical key, so Shift and caps no longer change which key is meant.
- `README.md` rewritten for the new features, with build instructions and notes for installing through Launcher or flashing a bare device.

### Notes

- Requires IRremote 4.3 or newer. Built and tested with IRremote 4.7.1, M5Cardputer 1.1.1, M5Unified 0.2.20, M5GFX 0.2.27 and the M5Stack esp32 board package 3.3.9.
- Raw capture and the new protocols are new in this release. Please report remotes that do not capture or replay.
- Editing and re-saving a file from another tool rewrites it with the signals the app reads. Keep a copy of files that contain other signal types.

## v1.1.0 and earlier

See the [Releases](https://github.com/fr4nx40/IRCloner-Cardputer-ADV/releases) page.
