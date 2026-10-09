# Blue Baton

English | [한국어](README.ko.md)

One Bluetooth keyboard and mouse for three computers (for example two Macs and a Windows PC), built on a single ESP32-S3 SuperMini.
The router stays connected to the input devices and to every host at the same time, and only changes which host receives the input. Switching does not reconnect anything.

```text
BLE keyboard ─┐                       ┌─ BLE HID → host 1
BLE mouse ────┴─→ ESP32-S3 (NimBLE) ─┼─ BLE HID → host 2
                    │                 └─ BLE HID → host 3
                    └─ USB: menu bar app (settings, quick switch)
```

Status: firmware 0.6. Two Macs verified on real hardware (input from a BLE mouse reaches the active Mac; hosts switch from the settings UI). Keyboard combo switching and Windows hosts are implemented but not yet verified on hardware.

License: not decided yet. Until a license is added, no rights are granted to reuse the code.

## Repository layout

| Path | Contents |
|---|---|
| [firmware/main/](firmware/main/) | Firmware (ESP-IDF v6.1, NimBLE). Input side `ble_pair.c`, report decoding `hid_input.c`, switching `router.c`, host side `host.c` |
| [firmware/PROTOCOL.md](firmware/PROTOCOL.md) | USB control protocol (one JSON object per line) |
| [app/](app/) | Menu bar / tray app (Tauri): owns the board's USB port, quick host switching, settings window |
| [ui/](ui/) | Settings UI (React, Tailwind CSS, shadcn/ui; also runs in a browser via Web Serial or the dev bridge) |
| [firmware/tools/](firmware/tools/) | Dev bridge `bridge.py`, flash helper `flash.sh` |
| [firmware/test/](firmware/test/) | Tests that run without the board (C and JS) |

## Build, flash, test

```bash
. ~/esp/esp-idf/export.sh && cd firmware && idf.py build
```

```bash
firmware/tools/flash.sh
```

```bash
firmware/test/run.sh
```

The menu bar app: [app/README.md](app/README.md). The settings UI on its own: [ui/README.md](ui/README.md).
