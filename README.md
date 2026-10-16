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

Status: version 0.7.4. Up to two input devices (a keyboard and a mouse, or any two) and three hosts. Tested on real
hardware with a keyboard and a mouse connected at the same time, and with two Macs, a Windows PC and an iPhone as
hosts: input goes to the active host, and both the key combo and the settings window switch hosts. The app was tested
on macOS and Windows. Not yet tested on hardware: typing a host's passkey on the keyboard (entering it in the app was
tested), system keys (power, sleep, wake), Linux as a host, and the Linux build of the app.

## What you need

- An ESP32-S3 board with at least 4 MB of flash, connected by the chip's own USB port (on boards with two ports, the
  one marked USB, not UART). Tested with an ESP32-S3 SuperMini. Other chips: [Boards](#boards).
- Bluetooth LE keyboard and mouse. Bluetooth Classic-only devices do not work: the ESP32-S3 has only Bluetooth LE.
- Up to three computers with Bluetooth LE.
- One of them runs the Blue Baton app, with the board plugged into it by USB. The cable powers the board, and the
  app is where you pair devices and change settings.

## Boards

| Board | Chip | Releases and the app | Builds | Run on a real board |
|---|---|---|---|---|
| ESP32-S3 SuperMini (4 MB flash) | ESP32-S3 | Yes | Yes | Yes (developed and measured on it) |
| M5Stack AtomS3 Lite (in a case) | ESP32-S3 | Yes, the same firmware | Yes | Not yet |
| M5Stack NanoC6 (in a case) | ESP32-C6 | No: build it yourself | Yes | Starts and works with the app; pairing not tested yet |
| Other ESP32-S3 boards (4 MB or more flash, the chip's own USB port) | ESP32-S3 | Yes, the same firmware | Yes | No |
| ESP32-C3 boards | ESP32-C3 | No: build it yourself | Yes | No |
| ESP32-C5 boards | ESP32-C5 | No: build it yourself | Yes | No |
| Other ESP32-C6 boards | ESP32-C6 | No: build it yourself | Yes | No |
| ESP32-H2 boards | ESP32-H2 | No: build it yourself | Yes | No |
| ESP32 (original) boards | ESP32 | No | Yes | No |
| ESP32-S2 boards | ESP32-S2 | No | Yes, but the chip has no Bluetooth radio | No |
| nRF52840, Raspberry Pi Pico W, Raspberry Pi | — | No | No | No |

- ESP32-C3, C5, C6 and H2 use the same ESP-IDF, NimBLE and USB port as the S3, but have one core (clock: C3 and C6
  160 MHz, H2 96 MHz, C5 240 MHz like the S3). Latency and link stability have not been measured on them. Build and
  install (from `firmware/`): `idf.py -B build-esp32c3 -DIDF_TARGET=esp32c3 build`, then
  `idf.py -B build-esp32c3 -p <port> flash`. Reports from these boards are welcome.
- The original ESP32 has no USB Serial/JTAG, which is how the app reaches the board (its console is on the UART). The
  ESP32-S2 has no Bluetooth radio. nRF52840, Pico W and Raspberry Pi use other Bluetooth stacks: the firmware's
  Bluetooth part would have to be written again for them.

## Getting started

1. **Install the app** from the [latest release](https://github.com/enfp-dev-studio/blue-baton/releases/latest):
   - macOS (Apple silicon): `.dmg`, signed and notarized.
   - Windows: `_x64-setup.exe` (installs for your user, no admin rights). Not code-signed, so SmartScreen may
     warn: More info → Run anyway.
   - Linux (experimental): `.deb`, `.rpm` or `.AppImage`. To reach the board's serial port, your user needs to be in the `dialout`
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
5. **Switch** with `Alt+1`, `2`, `3` (on a Mac keyboard: Option), or from the menu bar icon. The combos
   can be changed in the app. A host's row menu renames it (up to 8 characters, shown instead of its number) and moves
   it up or down without reconnecting.

## Updates

The app updates itself from GitHub Releases: it checks a minute after it starts and then once a day, and the menu has
Check for Updates…. Each app release carries the firmware of the same version. When the board runs an older one, the
settings window shows Firmware → Update. Updating keeps the paired devices and computers.

## Limits and known issues

- Two input devices and three computers at most.
- Bluetooth LE only, for the input devices and for the computers.
- Bluetooth LE limits how often the cursor moves: the computer in use gets input once per connection interval. Windows
  took 7.5 ms (about 133 times a second). macOS keeps the link at 15 ms (about 66 times a second) and refused every
  shorter interval the board asked for. On a Mac with a 60 Hz display, apps got about 60 cursor updates a second from
  the board, the same as from a 2.4 GHz receiver, because macOS passes on at most one per display frame. A gaming
  mouse's 1,000 Hz receiver rate does not carry over: through the board it runs in its Bluetooth mode.
- Links sometimes drop (connection timeout, several times a day on our setup) and reconnect by themselves. The cause
  is not known yet.
- Until a USB ID is assigned from [pid.codes](https://pid.codes), the board reports a temporary vendor and product ID
  to computers.

## Installing the firmware without the app

Each release also has the firmware as files, for [esptool](https://docs.espressif.com/projects/esptool/)
(`pip install esptool`):

```bash
esptool --chip esp32s3 write-flash 0x0 blue-baton-firmware-v0.7.4-esp32s3.bin
```

writes the whole image (a first install; it also clears pairings), and

```bash
esptool --chip esp32s3 write-flash 0x20000 blue-baton-firmware-v0.7.4-esp32s3-app.bin
```

updates the firmware and keeps pairings.

## Repository layout

| Path | Contents |
|---|---|
| [firmware/main/](firmware/main/) | Firmware (ESP-IDF v6.1, NimBLE). Input side `ble_pair.c`, report decoding `hid_input.c`, switching `router.c`, host side `host.c` |
| [firmware/PROTOCOL.md](firmware/PROTOCOL.md) | USB control protocol (one JSON object per line) |
| [app/](app/) | Menu bar app (Tauri): owns the board's USB port, quick host switching, settings window, writes the bundled firmware to the board |
| [ui/](ui/) | Settings UI (React, Tailwind CSS, shadcn/ui; also runs in a browser via Web Serial or the dev bridge). In eight languages, following the system or picked in the window |
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

[GNU GPL v3](LICENSE) (GPL-3.0-only). Copyright (C) 2026 ENFPDEV (ENFP-Dev-Studio). A commercial license, for use without the GPL's
terms, is available on request: contact@enfpdev.com. Contributions need the [CLA](CLA.md) (see
[CONTRIBUTING.md](CONTRIBUTING.md)). `ui/src/components/ui/` comes from [shadcn/ui](https://ui.shadcn.com) (MIT). The
app writes firmware with [espflash](https://github.com/esp-rs/espflash) (MIT or Apache-2.0).
