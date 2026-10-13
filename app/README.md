# Blue Baton menu bar app (app/)

A small menu bar app built with [Tauri 2](https://tauri.app), for macOS, Windows and Linux. The macOS build is tested on hardware; the Windows and Linux builds are built by CI and not yet tested.

- Lives in the menu bar only, never in the Dock (LSUIElement), like Google Drive.
- Left click on the icon opens a panel under it: the paired hosts (click to switch, the baton slides), the
  connected input devices, Add Host… (while a host slot is free), Settings…, Quit. It closes when you click
  anywhere else.
- Right click opens a menu: the hosts, input devices, Open Blue Baton…, Add Host…, Launch at Login,
  Developer API, Quit. On macOS, while the board is connected, the icon is three host cells: the active host's is filled, a
  host that is not connected is faded.
- The settings window is the full UI (`../ui`); the panel is the same UI with `?view=panel`. Closing the window keeps the app running.
- The app owns the board's USB serial port (`src-tauri/src/board.rs`) and reconnects after an unplug. It looks
  for USB ID 0x303A:0x1001, which every ESP32-S3/C3 board shares, and takes a port only once it answers `hello` as
  Blue Baton firmware; a port that does not is left alone for a minute.
- Firmware (`src-tauri/src/firmware.rs`): a release build carries the firmware of the same version. When the board
  runs an older one, the settings window offers an update that keeps the pairings (it rewrites the bootloader,
  partition table and app, not the pairing store). A board that does not answer as Blue Baton is offered an
  install of the whole image. The writing is done with espflash while the board link lets go of the port.
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

Needs Rust (rustup), Node and pnpm. The repository is one pnpm workspace (`ui`, `app`): install once at the top, then
build here (it builds the UI too).

```bash
pnpm install
```

```bash
pnpm tauri build
```

The app is at `src-tauri/target/release/bundle/macos/Blue Baton.app`. `pnpm tauri dev` runs it against the UI dev
server (`../ui`, port 5180) with live reload.

## Releases and updates

The app updates itself from GitHub Releases (`tauri-plugin-updater`): it checks a minute after it starts and then
once a day, and the menu has Check for Updates…. Update files are signed with the project's Tauri update key; the
public key is in `src-tauri/tauri.conf.json`, the private key stays outside the repository.

- Push a tag such as `v0.7.0`: `.github/workflows/release.yml` builds macOS (Apple silicon, signed and notarized),
  Windows and Linux, and creates a draft release with `latest.json`. Repository secrets it needs:
  `TAURI_SIGNING_PRIVATE_KEY` (and `TAURI_SIGNING_PRIVATE_KEY_PASSWORD` if the key has one), and for macOS
  `APPLE_CERTIFICATE` (the Developer ID Application certificate exported as .p12, base64), `APPLE_CERTIFICATE_PASSWORD`,
  `APPLE_ID`, `APPLE_PASSWORD` (an app-specific password) and `APPLE_TEAM_ID`. `APPLE_SIGNING_NAME`
  ("Developer ID Application: <name>", the certificate's name without the team ID) is masked in the build log.
  `./set-release-secrets.sh` sets all of them in one go (it asks for the .p12, its password, the Apple ID and an
  app-specific password, and reads the rest from this Mac).
- The release also builds the firmware (`firmware/tools/package.sh` in the `espressif/idf:v6.1` container), uploads
  it, and puts it inside each app build: `src-tauri/build.rs` bundles `src-tauri/firmware/blue-baton-esp32s3.bin` when
  it is there, with its version from `firmware/main/version.h`. The tag must match the app and firmware versions.
  For a local build with firmware, run `firmware/tools/package.sh <version> <dir>` and copy the whole image
  (`*-esp32s3.bin`) to that path; without it the app builds as before, with no firmware to offer.
- `.github/workflows/build.yml` (run by hand) is a test build of the firmware and of the app for Windows and Linux.
- On a Mac, `./release-macos.sh` builds, signs (Developer ID), notarizes and staples the app. It needs a "Developer
  ID Application" certificate in the keychain and notarytool credentials stored as `blue-baton-notary` (see the
  script). The Windows build is not signed.

Icons: `pnpm icons` writes `assets/icon.svg` and `assets/tray.svg` (`assets/make_icons.py`), rasterizes them with
headless Chrome (`CHROME` overrides the path) into `assets/icon-1024.png` and the menu bar template icon, and
regenerates the platform icon sizes. The mark is a hollow relay baton passed at speed.
