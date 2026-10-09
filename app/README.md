# Blue Baton menu bar app (app/)

A small menu bar (macOS) / tray (Windows) app built with [Tauri 2](https://tauri.app). About 5 MB.

- Lives in the menu bar only, never in the Dock (LSUIElement), like Google Drive.
- Left click on the icon opens a panel under it: the three hosts (click to switch, the baton slides), the
  connected input devices, Add Host…, Settings…, Quit. It closes when you click anywhere else.
- Right click opens a menu: the hosts, input devices, Open Blue Baton…, Add Host…, Launch at Login,
  Developer API, Quit. The icon shows the active host number.
- The settings window is the full UI (`../ui`); the panel is the same UI with `?view=panel`. Closing the window keeps the app running.
- The app owns the board's USB serial port (`src-tauri/src/board.rs`), finds it by USB ID (0x303A:0x1001),
  and reconnects after an unplug.
- The settings PIN is remembered in the system keychain, so the menu works after a restart without unlocking.
- Pairing prompts (host passkey, device codes) bring the window forward and post a notification.
- Developer API (off by default): the same HTTP interface as `firmware/tools/bridge.py` on 127.0.0.1:8766,
  so `firmware/tools/flash.sh` and monitoring tools work while the app holds the port. Board lines are also
  written to `~/Library/Logs/com.enfpdev.bluebaton/board.log` while it is on (they include typed passkeys).

## Build

Needs Rust (rustup) and Node.

```bash
npm install
```

```bash
npm run build
```

The app is at `src-tauri/target/release/bundle/macos/Blue Baton.app`. `npm run dev` runs it against the UI dev
server (`../ui`, port 5180) with live reload.

Icons: `npm run icons` redraws `assets/icon-1024.png` and the menu bar template icon (`assets/make_icons.py`,
no image libraries) and regenerates the platform icon sizes.

## Distribution (later)

Personal builds run unsigned. For distribution, macOS needs a Developer ID Application certificate and
notarization, and Windows an Authenticode certificate.
