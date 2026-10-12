# Blue Baton

English | [한국어](README.ko.md)

One Bluetooth LE keyboard and mouse for three computers (for example two Macs and a Windows PC), built on a single ESP32-S3 SuperMini. Input devices must support Bluetooth LE: the chip has no Bluetooth Classic.
The router stays connected to the input devices and to every host at the same time, and only changes which host receives the input. Switching does not reconnect anything.

```text
BLE keyboard ─┐                       ┌─ BLE HID → host 1
BLE mouse ────┴─→ ESP32-S3 (NimBLE) ─┼─ BLE HID → host 2
                    │                 └─ BLE HID → host 3
                    └─ USB: menu bar app (settings, quick switch)
```

Status: version 0.7. Up to two input devices (a keyboard and a mouse, or any two) and three hosts. Tested on real
hardware with a keyboard and a mouse connected at the same time, and with two Macs, a Windows PC and an iPhone as
hosts: input goes to the active host, and both the key combo and the settings window switch hosts. Not yet tested on
hardware: typing a host's passkey on the keyboard (entering it in the app was tested), system keys (power, sleep,
wake), Linux as a host, and the Windows and Linux builds of the app.

## What you need

- An ESP32-S3 board with at least 4 MB of flash, connected by the chip's own USB port (on boards with two ports, the
  one marked USB, not UART). Tested with an ESP32-S3 SuperMini.
- Bluetooth LE keyboard and mouse. Bluetooth Classic-only devices do not work: the ESP32-S3 has only Bluetooth LE.
- Up to three computers with Bluetooth LE.
- One of them runs the Blue Baton app, with the board plugged into it by USB. The cable powers the board, and the
  app is where you pair devices and change settings.

## Getting started

1. **Install the app** from the [latest release](https://github.com/enfp-dev-studio/blue-baton/releases/latest):
   - macOS (Apple silicon): `.dmg`, signed and notarized.
   - Windows: `.exe` or `.msi`. Not code-signed, so SmartScreen may warn: More info → Run anyway.
   - Linux: `.deb`, `.rpm` or `.AppImage`. To reach the board's serial port, your user needs to be in the `dialout`
     group (`sudo usermod -aG dialout $USER`, then log out and back in).
2. **Plug the board in** and open the app. A board without Blue Baton shows "This board doesn't run Blue Baton
   yet": click **Install Blue Baton**. The app carries the firmware, so nothing else needs installing. If the board
   is not found, hold its BOOT button while plugging it in.
3. **Add your keyboard and mouse**: Input devices → Add Device…, put the device into pairing mode, and click Connect
   when it shows up.
4. **Add each computer**: Hosts → Add on an empty row. The board is discoverable for 3 minutes. On the computer:
   - macOS: System Settings → Bluetooth, Connect next to "Blue Baton".
   - Windows: Settings → Bluetooth & devices → Add device → Bluetooth → "Blue Baton".

   When the computer shows a 6-digit code, enter it in the app (or type it on a keyboard connected to the board).
5. **Switch** with `Ctrl+Alt+1`, `2`, `3` (on a Mac keyboard: Control+Option), or from the menu bar icon. The combos
   can be changed in the app.

## Updates

The app updates itself from GitHub Releases: it checks a minute after it starts and then once a day, and the menu has
Check for Updates…. Each app release carries the firmware of the same version. When the board runs an older one, the
settings window shows Firmware → Update. Updating keeps the paired devices and computers.

## Limits and known issues

- Two input devices and three computers at most.
- Bluetooth LE only, for the input devices and for the computers.
- Links sometimes drop (connection timeout, several times a day on our setup) and reconnect by themselves. The cause
  is not known yet.
- Until a USB ID is assigned from [pid.codes](https://pid.codes), the board reports a temporary vendor and product ID
  to computers.

## Installing the firmware without the app

Each release also has the firmware as files, for [esptool](https://docs.espressif.com/projects/esptool/)
(`pip install esptool`):

```bash
esptool --chip esp32s3 write-flash 0x0 blue-baton-firmware-v0.7.1-esp32s3.bin
```

writes the whole image (a first install; it also clears pairings), and

```bash
esptool --chip esp32s3 write-flash 0x20000 blue-baton-firmware-v0.7.1-esp32s3-app.bin
```

updates the firmware and keeps pairings.

## Repository layout

| Path | Contents |
|---|---|
| [firmware/main/](firmware/main/) | Firmware (ESP-IDF v6.1, NimBLE). Input side `ble_pair.c`, report decoding `hid_input.c`, switching `router.c`, host side `host.c` |
| [firmware/PROTOCOL.md](firmware/PROTOCOL.md) | USB control protocol (one JSON object per line) |
| [app/](app/) | Menu bar app (Tauri): owns the board's USB port, quick host switching, settings window, writes the bundled firmware to the board |
| [ui/](ui/) | Settings UI (React, Tailwind CSS, shadcn/ui; also runs in a browser via Web Serial or the dev bridge). Korean when the browser language is Korean |
| [firmware/tools/](firmware/tools/) | Release files `package.sh`, dev bridge `bridge.py`, flash helper `flash.sh`, board watcher `watch.py` (connections, latency stats, soak log, alerts) |
| [firmware/test/](firmware/test/) | Tests that run without the board: firmware C tests; `run.sh` also runs the UI's JS tests (`ui/test/`) |

## Build, flash, test

Release build (developer diagnostics off). `firmware/tools/package.sh <version> <dir>` also writes the two
release files:

```bash
. ~/esp/esp-idf/export.sh && cd firmware && idf.py build
```

Developer build and flash (diagnostics on: detailed `stats`, `selftest`):

```bash
firmware/tools/flash.sh
```

`flash.sh` builds into `firmware/build-dev`, then flashes the first `/dev/cu.usbmodem*` port (macOS; `KM_PORT` sets another port). It expects ESP-IDF in `~/esp/esp-idf`.

```bash
firmware/test/run.sh
```

The menu bar app: [app/README.md](app/README.md). The settings UI on its own: [ui/README.md](ui/README.md).

## License

[GNU GPL v3](LICENSE) (GPL-3.0-only). Copyright (C) 2026 enfpdev. A commercial license, for use without the GPL's
terms, is available on request: contact@enfpdev.com. Contributions need the [CLA](CLA.md) (see
[CONTRIBUTING.md](CONTRIBUTING.md)). `ui/src/components/ui/` comes from [shadcn/ui](https://ui.shadcn.com) (MIT). The
app writes firmware with [espflash](https://github.com/esp-rs/espflash) (MIT or Apache-2.0).
