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

Status: firmware 0.7. Up to two input devices (a keyboard and a mouse, or any two) and three hosts. Tested on real hardware with a keyboard and a mouse connected at the same time, and with two Macs, a Windows PC and an iPhone as hosts: input goes to the active host, and both the key combo and the settings UI switch hosts. Not yet tested on hardware: typing a host's passkey on the keyboard (entering it in the app was tested) and system keys (power, sleep, wake). Only the macOS app has been built.

License: not decided yet.

## Repository layout

| Path | Contents |
|---|---|
| [firmware/main/](firmware/main/) | Firmware (ESP-IDF v6.1, NimBLE). Input side `ble_pair.c`, report decoding `hid_input.c`, switching `router.c`, host side `host.c` |
| [firmware/PROTOCOL.md](firmware/PROTOCOL.md) | USB control protocol (one JSON object per line) |
| [app/](app/) | Menu bar app (Tauri; only the macOS build has been made): owns the board's USB port, quick host switching, settings window |
| [ui/](ui/) | Settings UI (React, Tailwind CSS, shadcn/ui; also runs in a browser via Web Serial or the dev bridge). Korean when the browser language is Korean |
| [firmware/tools/](firmware/tools/) | Dev bridge `bridge.py`, flash helper `flash.sh`, board watcher `watch.py` (connections, latency stats, soak log, alerts) |
| [firmware/test/](firmware/test/) | Tests that run without the board: firmware C tests; `run.sh` also runs the UI's JS tests (`ui/test/`) |

## Build, flash, test

Release build (developer diagnostics off):

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
