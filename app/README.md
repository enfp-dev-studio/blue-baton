# Blue Baton menu bar app (app/)

A small menu bar app built with [Tauri 2](https://tauri.app). Only the macOS build has been made; the code also has a Windows tray path, not built yet.

- Lives in the menu bar only, never in the Dock (LSUIElement), like Google Drive.
- Left click on the icon opens a panel under it: the paired hosts (click to switch, the baton slides), the
  connected input devices, Add Host… (while a host slot is free), Settings…, Quit. It closes when you click
  anywhere else.
- Right click opens a menu: the hosts, input devices, Open Blue Baton…, Add Host…, Launch at Login,
  Developer API, Quit. On macOS the icon shows the active host number (with `?` while that host is not
  connected).
- The settings window is the full UI (`../ui`); the panel is the same UI with `?view=panel`. Closing the window keeps the app running.
- The app owns the board's USB serial port (`src-tauri/src/board.rs`) and reconnects after an unplug. It looks
  for USB ID 0x303A:0x1001, which every ESP32-S3/C3 board shares, and takes a port only once it answers `hello` as
  Blue Baton firmware; a port that does not is left alone for a minute.
- No PIN: like other USB accessories, the board trusts the computer it is plugged into. It sends what is typed only
  while the input monitor in Diagnostics is open (`firmware/PROTOCOL.md`, "Access and input content").
- Pairing prompts (host passkey, device codes) bring the window forward and post a notification.
- Developer API (off by default): the same HTTP interface as `firmware/tools/bridge.py` on 127.0.0.1:8766,
  so `firmware/tools/flash.sh` and monitoring tools work while the app holds the port. Its `/events` stream
  carries board lines as they are (typed passkeys too). Requests must name `127.0.0.1:8766` or `localhost:8766`
  as Host, and turning it off also ends open streams. While it is on, board lines are also written to
  `~/Library/Logs/com.enfpdev.bluebaton/board.log`, with pairing codes masked and without input content
  (`report`/`route`); at 1 MB it moves to `board.log.1`.

## Build

Needs Rust (rustup) and Node. `npm run build` also builds the UI, so install the packages here and in `../ui`.

```bash
npm install
```

```bash
npm --prefix ../ui install
```

```bash
npm run build
```

The app is at `src-tauri/target/release/bundle/macos/Blue Baton.app`. `npm run dev` runs it against the UI dev
server (`../ui`, port 5180) with live reload.

Signed release for macOS (Developer ID, notarized and stapled): `./release-macos.sh`. It needs a "Developer ID
Application" certificate in the keychain and notarytool credentials stored as `blue-baton-notary` (see the script).
The Windows build is not signed.

Icons: `npm run icons` writes `assets/icon.svg` and `assets/tray.svg` (`assets/make_icons.py`), rasterizes them with
headless Chrome (`CHROME` overrides the path) into `assets/icon-1024.png` and the menu bar template icon, and
regenerates the platform icon sizes. The mark is a hollow relay baton passed at speed.

## Distribution (later)

Personal builds run unsigned. For distribution, macOS needs a Developer ID Application certificate and
notarization, and Windows an Authenticode certificate.
