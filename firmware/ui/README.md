# Settings UI (firmware/ui)

A web page for pairing input devices and hosts with the board (Blue Baton) and watching its state. Plain ES modules, no build step.
Layers: transport (USB, bridge, mock) → protocol client → store → views.

## Opening it (three transports)

| Transport | URL | When |
|---|---|---|
| Local bridge (recommended) | `http://localhost:8766/` (the bridge redirects to `?bridge`) | Development. The UI and a monitor can attach at the same time, and flashing doesn't break the connection. Works in any browser |
| Direct USB (Web Serial) | `http://localhost:8765/` | Without the bridge. Chrome/Edge only. This tab holds the port exclusively |
| Mock board | `http://localhost:8766/?mock` (bridge) or `http://localhost:8765/?mock` | UI work without a board. `&unlocked` opens without a PIN, `&fast` makes scans 10 s |

Start the bridge (the ESP-IDF Python environment has pyserial):

```bash
~/.espressif/python_env/idf6.1_py3.14_env/bin/python firmware/tools/bridge.py
```

Static server only (direct USB, mock board):

```bash
python3 -m http.server 8765 --bind 127.0.0.1 --directory firmware/ui
```

Flash (if the bridge is running, it releases the port and grabs it again afterwards; the UI reconnects by itself):

```bash
firmware/tools/flash.sh
```

Send commands and watch events through the bridge (dev tools):

```bash
curl -sN http://127.0.0.1:8766/events
```

```bash
curl -s -X POST --data '{"id":1,"cmd":"status"}' http://127.0.0.1:8766/send
```

## Language

The UI is in English by default, with Korean as a second language. Add `?lang=en` or `?lang=ko` to the URL to pick one (it combines with the other options, e.g. `?mock&lang=ko`). Without it, a browser language starting with `ko` selects Korean and anything else selects English.

All UI text lives in `js/i18n.js`: one table per language with the same keys, read through `t(key, params)` (`{name}` placeholders). Static text in `index.html` carries `data-i18n="key"` and is filled in at startup. To add a language, add a table with the same keys; `test/i18n.test.mjs` checks that keys and placeholders match.

## Files

| File | Role |
|---|---|
| `js/transport/webserial.js` | Direct USB link. A port allowed once opens without the picker next time |
| `js/transport/bridge.js` | Local bridge link (server-sent events + POST) |
| `js/transport/mock.js` | Mock board. Imitates the mouse and keyboard captured from real hardware, and the pairing methods we could not try on real hardware (numeric comparison, passkey entry and display) |
| `js/protocol.js` | Line-JSON commands, replies, and events (`firmware/PROTOCOL.md`) |
| `js/store.js` | UI state. Changes only through board replies and events. Devices form a single list |
| `js/views.js` | Top bar, lock, hosts, device list, add device, pairing prompts, log |
| `js/inputs.js` | Input monitor (decodes input with the Report Map) |
| `js/hid/report-map.js` | HID Report Map parsing |
| `js/i18n.js` | UI strings (English and Korean) and language selection |

## Tests (no board needed)

```bash
firmware/test/run.sh
```

- Firmware C: Report Map kind detection, input normalization and host switching, text helpers (built with ASan/UBSan)
- UI JS: Report Map parsing (9, with Report Maps from real hardware), command/event flows against the mock board (8), string tables (5)

Only the UI tests: `node --test firmware/ui/test/`

## Known limitations

- Only devices that connect after the page is opened deliver their Report Map. Input from an already connected device is decoded from its next reconnect onward (there is no command yet to ask the board to resend the Report Map).
- The built-in browser of the Claude desktop app has no Web Serial port picker, so direct USB does not work there. The bridge does.
