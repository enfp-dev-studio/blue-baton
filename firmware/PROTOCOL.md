# USB Control Protocol (firmware 0.7.1)

The board connects over USB to the main machine, and that machine's settings app (`app/`, or the UI in `ui/` via the local bridge or Web Serial) controls the board with this protocol.

Changes from earlier versions are collected at the end under "0.7 → 0.7.1 changes", "0.6 → 0.7 changes", "0.5 → 0.6 changes", "0.4 → 0.5 changes" and "0.3 → 0.4 changes".

## Basic rules

- Host → board: one JSON object per line, terminated by `\n`. A line is at most 255 bytes.
  ```text
  {"id":1,"cmd":"hello"}
  ```
- Board → host: one JSON line with the `KM< ` prefix
  ```text
  KM< {"id":1,"ok":true,"fw":"blue-baton","version":"0.7","combo_set":false}
  ```
- A response carries the same `id` as its request. Notifications the board sends on its own (events) have an `event` field and no `id`.
- Board logs (`I (123) km: ...`) come out on the same port. The host should read only lines that start with `KM< `. Locate `KM< `, then parse from there.
- A failure response carries both `code` (for machines) and `error` (a human-readable sentence). The UI picks its wording from `code`.
  ```text
  {"id":5,"ok":false,"code":"not_found","error":"No device with that address"}
  ```
- Addresses use the `AA:BB:CC:DD:EE:FF` format.
  - `addr`: the on-air address used by the current connection.
  - `id_addr`: the device's identity address. This is the persistent key that identifies a paired device.
  - For devices with a static address the two are the same. For devices that use an RPA they differ.

## Access and input content

- USB is not locked. The computer the board is plugged into is trusted, as with other USB accessories (receivers,
  KVM switches, keyboard setup apps).
- What is typed does not go out on USB by default. Any program on the computer can open the serial port, while the
  OS asks the user before an app may read keystrokes (macOS: Input Monitoring). Only `activity` (which device, no
  content) is sent, at most every 250 ms per device.
- `report` and `route` (input content) are sent only while a client holds the input monitor lease: `monitor`
  `{"on":true}` turns it on for `lease_ms` (60 s) and must be renewed; `{"on":false}` or a reboot ends it. The
  settings UI renews it only while its input monitor is open and visible.

## Commands

| Command | Arguments | Description |
|---|---|---|
| `hello` | none | Firmware info and whether a switch combo is set |
| `status` | none | See "status response" below |
| `monitor` | `on` | Input monitor lease (see "Access and input content"). Reply: `lease_ms` (60000 when on, 0 when off) |
| `stats` | none | Latency statistics since the last `stats` (`stats`): `mouse_sent`, `notify_avg_us`, `notify_max_us` (time inside the notify call that hands a report to the controller), `hosts` (`host`, `itvl`, `latency` of each host link). Developer builds (`CONFIG_KM_DIAG`; `tools/flash.sh` builds them, release builds leave it off) add `mbufs` and `mbufs_free_min` (the stack's buffer count, and the fewest free at a notify), `nvs_writes` and `nvs_max_us` (settings flash writes and the longest one, µs), `usb_lines`, `usb_avg_us`, `usb_max_us` and `usb_dropped` (lines the board sent on this USB channel, the average and longest time handing one to the output buffer took, µs, and lines dropped because the buffer was full), `hosts` (`host`, `itvl`, `latency`, `rssi` in dBm as the board receives it, `mouse_reports` handed to the stack, `nobuf_mouse`/`nobuf_other` reports the stack could not take, `superseded` queued changes dropped for a later one of the same report, `ack_ms` how long reports took from the input reaching the board until the controller reported them completed (HCI Number Of Completed Packets): counts for <15, 15–30, 30–45, 45–60, 60–90, 90–150, 150–300, ≥300 ms, `ack_max_ms`, of each host link). Reading resets the counters |
| `host_itvl` | `min`, `max` (1.25 ms units) | Developer builds only. The interval asked of the active host from now on (and of the current one at once), until restart. The answer comes as `host_conn_params` |
| `selftest` | `host` (1–3, 0 stops), `minutes` (default 30) | Developer builds only. Sends mouse reports to that host every 8.75 ms (16 counts right, then 16 left: the cursor stays within a few pixels) through the normal delivery path, so `stats` measures delivery without anyone moving the mouse. The host must not be the active one; it stops when that host becomes active, when its link drops, or when time is up. Events `selftest_started` (`host`, `minutes`), `selftest_stopped` (`host`, `reason`: `done`/`active`/`link`/`stopped`/`restarted`, `reports`) |
| `combo_get` | none | The host switch combos actually in effect (`combo`). If nothing is stored or the stored value is malformed, the default applies and `default:true`; if an invalid value is stored, `stored_invalid:true` |
| `combo_set` | `combo` | Comma-separated list of combos in host order (e.g. `ctrl+alt+1,ctrl+alt+2,ctrl+alt+3`, up to 95 characters). Validated, then applied and saved immediately. If invalid, `code:"bad_combo"` |
| `host_select` | `host` (1–3) | Select the active host directly. Same handling as a combo switch (release-all to the previous host). A host that is not `ready` cannot be selected: `code:"host_offline"` |
| `host_pair` | `on` | Open or close the host pairing window. When opened, it accepts connections and pairing from new hosts for 180 seconds. It closes by itself once one host pairs. If 3 hosts are already paired, `code:"hosts_full"`. Opening it stops input device scanning and user connection attempts |
| `host_list` | none | List of paired hosts (`hosts`). See "host_list entries" below |
| `host_remove` | `host` (1–3) | Delete the bond of the host at that number. Disconnects it if connected. "Blue Baton" must also be removed on that computer before it can be added again |
| `host_move` | `host` (1–3), `to` (1–3) | The host at `host` takes number `to`, and the host at `to`, if any, takes `host`. Links stay up. The switch combos stay with the numbers (the combo for 1 goes to whichever host is 1); the active host stays the same computer under its new number. Event `host_moved` |
| `host_nick` | `host` (1–3), `nick` | The name shown for that host instead of its number, up to 8 characters of UTF-8 (no control characters); `""` clears it. Saved on the board. If invalid, `code:"bad_nick"`. Event `host_nick` |
| `host_passkey` | `code` (6-digit string) | Answer `host_passkey_request` from the GUI (instead of typing on a keyboard attached to the router) |
| `pair_scan` | `on` | Turn new-device scanning on/off. Once on, it stops by itself after 60 seconds (`scan_stopped` `reason:"timeout"`). Auto-reconnect pauses while scanning. If a user connection attempt is in progress, it is abandoned (`connect_failed` `status:"cancelled"`) and the scan starts right away |
| `pair_list` | `all` (optional, default false) | Found devices list (`devices`). HID devices only by default; with `all:true`, every connectable device. Currently connected devices are excluded |
| `pair_connect` | `addr` | Newly found device: connect and pair (10-second attempt). A running scan pauses for the attempt and then resumes. Paired device: not connected directly, but put back on the auto-connect list (auto-connect is re-enabled if it was off). It connects when the device wakes and advertises. A running scan stops |
| `disconnect` | `addr` | Disconnect a connected device. The pairing is kept, and the device is not auto-reconnected until the next `pair_connect` |
| `pair_accept` | `accept` | Accept/reject a numeric comparison (`numcmp`) request |
| `passkey` | `code` (6-digit string) | Answer `passkey_request` |
| `pair_cancel` | none | Stop scanning and user connection attempts, and reject any pending pairing input. **Devices that are already connected are not disconnected**, except a device whose pairing input was pending: its pairing does not finish and the connection is dropped (passkey entry cannot be rejected, so that one is disconnected directly) |
| `paired_list` | none | List of paired input devices (`paired`). The source is the NimBLE bond store. Bonds recorded as hosts are excluded |
| `paired_remove` | `addr` (identity) | Delete one pairing (bond keys, resolving list, name, etc.). Disconnects it if connected |
| `paired_clear` | none | Delete all pairings, hosts included |

Failure `code`

| code | Meaning |
|---|---|
| `busy` | Not possible right now (BLE not ready, already connected, or another connection attempt in progress) |
| `not_found` | No device with that address (in neither the found list nor the paired list), or no host at that number (`host_remove`, `host_move`, `host_nick`) |
| `no_slot` | All input device slots (2 concurrent) are in use |
| `inputs_full` | `pair_connect` to a new device while 2 input devices are paired already. Remove one first |
| `bad_addr` | Address missing or malformed |
| `no_request` | No pending pairing input request to answer |
| `host_window` | The host pairing window is open, so input device scanning/new connections are not accepted |
| `hosts_full` | All 3 host slots are in use |
| `host_offline` | That host is not connected (`host_select`) |
| `bad_host` | Host number is not 1–3 |
| `bad_code` | Passkey is not 6 digits |
| `bad_nick` | Host name is over 8 characters or not valid UTF-8 text |
| `bad_combo` | Switch combo missing or malformed |
| `bad_request` | Missing or malformed argument (`cmd`, `on`, `accept`) |
| `unknown_cmd` | Unknown command |
| `failed` | Any other failure (including NVS write errors) |

### status response

```text
{"id":9,"ok":true,"scanning":false,"connected":true,"hid_ready":true,"peer":"00:00:5E:00:53:17",
 "peers":[{"addr":"00:00:5E:00:53:17","id_addr":"00:00:5E:00:53:17","state":"ready","hid_ready":true,"itvl":7}],
 "radio":"auto","bonds":1,"bonds_max":8,"inputs":1,"inputs_max":2,"active_host":1,"heap_free":228688,"heap_min":224496}
```

- `radio`: what the radio is doing right now.
  - `idle`: nothing
  - `scan`: scanning for new devices
  - `connect`: trying to connect to a device the user chose
  - `auto`: waiting to auto-reconnect paired devices
  - `cancel`: canceling the previous attempt
- `peers[].state`: `connected`, `securing`, `discovering`, `ready`
- `bonds`: number of bonds stored in NimBLE (input devices and hosts). `bonds_max`: the store's size (room is left for
  security procedures in progress, which NimBLE counts toward it)
- `inputs`: paired input devices. `inputs_max`: how many may be paired (2)
- `active_host`: the host that receives input (1-based)

### pair_list and paired_list entries

```text
devices: [{"addr":"00:00:5E:00:53:18","name":"Xiaoxin Mplus 5.0","rssi":-43,"paired":false,"appearance":962,"hid":true}]
paired:  [{"addr":"00:00:5E:00:53:17","type":1,"name":"Xiaoxin Mplus 5.0","kinds":["mouse"],"connected":false,
           "state":"offline","auto":true,"last_seq":1}]
```

- `appearance`: the Appearance value from advertising (Bluetooth Assigned Numbers). 961 keyboard, 962 mouse, etc. 0 if absent
- `kinds`: kinds determined from the Report Map (`keyboard`, `mouse`, `consumer`, `system`, `game`, `other`). Known only after the device has connected once
- `auto`: whether the device is an auto-reconnect target. false if the user ran `disconnect` or the device disconnected 3 times in a row before becoming ready
- `last_seq`: order of the last connection. Larger means more recent. Used to clean up records that have not been used for a long time

### host_list entries

```text
hosts: [{"host":1,"addr":"00:00:5E:00:53:30","name":"Example’s MacBook Pro","nick":"Work","connected":true,"state":"ready","active":true}]
```

- `addr`: the host's identity address, saved at pairing (shown even when the host connects with an RPA, as Macs do)
- `name`: the host's GAP Device Name (the computer name on a Mac). Read each time the host connects and becomes ready
- `nick`: the name the user gave it (`host_nick`), `""` for none
- `state`: `offline`, `connected` (before encryption), `secured` (before subscription), `ready` (reports can be sent)

## Events (sent by the board on its own)

| Event | Fields | Meaning |
|---|---|---|
| `ble_ready` | `bonds`, `max` | BLE is ready (at boot, after a BLE host reset) |
| `ble_reset` | none | BLE host reset. All connections are gone |
| `scan_started` | none | New-device scan started |
| `scan_stopped` | `reason` | Scan stopped. `timeout` (60 seconds), `radio` (turned off, or the radio switched to another task), `preempted` (briefly interrupted by the stack; restarts soon), `error` (with `status`) |
| `device_found` | `addr`, `name`, `rssi`, `paired`, `appearance` | Found an HID device. If the name etc. is filled in later (scan response), it is sent again with the same `addr` |
| `autoconnect` | `devices`, `fast` | Put N paired devices on the accept list and started waiting for auto-connect. `fast`: listening 30 ms every 60 ms (for 30 s after a device drops, or at start); otherwise 11.25 ms every 1.28 s (HOGP 1.0 Table 5.5) |
| `autoconnect_failed` | `rc`, `retry_ms` | Starting auto-reconnect failed (for example `rc` 525 = HCI 0x0D, the controller refused for lack of resources). The board tries again after `retry_ms` |
| `connecting` | `addr` | Connection attempt to a user-chosen device in progress |
| `connect_failed` | `addr`, `status` | User connection failed (`status`: HCI/NimBLE code, `"no_slot"`, or `"cancelled"` when `pair_scan` replaced the attempt) |
| `connected` | `addr`, `id_addr`, `bonded`, `user`, `itvl`, `latency`, `timeout` | Connected. `bonded`: an already paired device, `user`: a connection the user chose. Connection parameters: `itvl` in units of 1.25 ms, `timeout` in units of 10 ms. The board requests 8.75 ms, latency 0, supervision timeout 4 s |
| `conn_params` | `addr`, `status`, `itvl`, `latency`, `timeout` | The device changed the connection parameters |
| `conn_params_req` | `addr`, `itvl_min`, `itvl_max`, `latency`, `timeout` | The device asked for other connection parameters. The board accepts as asked; the controller may still refuse (then no `conn_params` follows) |
| `numcmp` | `addr`, `code` (6-digit string) | Numeric comparison. Check that the number matches the device's and answer with `pair_accept` |
| `passkey_request` | `addr` | The 6 digits shown on the device must be entered. Answer with `passkey` |
| `passkey_display` | `addr`, `code`, `ok` | A number generated by the board. Type this number on the device (keyboard) |
| `encrypted` | `addr`, `ok`, `status`, `bond`, `authenticated`, `key_size` | Encryption result. `bond`: `new` (new pairing), `restored` (restored from stored keys), `none` |
| `paired` | `addr`, `id_addr`, `name`, `bond` | Confirmed as a paired device (both new pairing and restore) |
| `bond_full` | `stage`, `bonds`, `max` | The bond store is full, so the new pairing was rejected. Existing bonds are not deleted |
| `store_full` | `obj` | Not enough slots for non-bond stored items (subscription CCCDs etc.). A configuration problem |
| `hid_chr` | `addr`, `svc`, `val`, `props`, `uuid16` | Characteristics of the HID service (for diagnostics) |
| `report_ref` | `addr`, `val`, `id`, `type`, `svc` | Report ID and type of the input report with value handle `val` |
| `hid_info` | `addr`, `svc`, `bcd_hid`, `country`, `remote_wake`, `normally_connectable` | HID Information |
| `report_map` | `addr`, `svc`, `len`, `truncated`, `kinds`, `hex` | The full Report Map. One per service (`svc`). `truncated:true` if it exceeds 512 bytes |
| `subscribe_failed` | `addr`, `val`, `status` | Failed to enable notifications for one input report (the rest continue) |
| `hid_ready` | `addr`, `id_addr`, `kinds`, `reports`, `services`, `mtu` | Ready to receive input. `mtu`: ATT MTU agreed with the device (23 if it refused the exchange) |
| `device_name` | `addr`, `name` | Read the GAP Device Name (devices without a name in their advertising get their name here) |
| `activity` | `addr` | Input arrived from that device. No content; at most every 250 ms per device |
| `report` | `addr`, `handle`, `data` (hex) | Input report. `handle` is the `val` from `report_ref`. Only under the input monitor lease |
| `failed` | `addr`, `stage`, `status` | A connection ended in failure. The board disconnects it. `stage`: `security_start`, `security`, `not_bonded`, `mtu`, `services`, `no_hid_service`, `no_usable_input` (none of keys, pointer, media or system keys; checked once every Report Map is read), `characteristics`, `descriptors`. For `no_hid_service` and `no_usable_input` the bond is deleted too (`unpaired` follows) |
| `disconnected` | `addr`, `id_addr`, `reason` | Disconnected (HCI reason code. 531 = remote side disconnected) |
| `unpaired` | `id_addr` | One pairing was deleted |
| `unpaired_all` | none | All pairings were deleted |
| `host_switch` | `host`, `from` | The active host changed (combo or `host_select`). 1-based |
| `route` | `host`, `keys`, `buttons`, `consumer`, `system` | The state to send to the active host changed (keys, mouse buttons, media keys, system keys). `system` bits: 1 Power Down, 2 Sleep, 4 Wake Up. `keys` is a list of Keyboard page Usages (including modifiers 0xE0–0xE7). Mouse motion is not included. For diagnostics, only under the input monitor lease |
| `host_online` | `host`, `online` | Whether the host switched to is connected right now. If `false`, input is dropped until it connects (it is not sent later in a burst) |
| `host_window` | `open`, `seconds` | The host pairing window opened/closed |
| `host_connected` | `host`, `addr`, `bonded`, `itvl` | A host connected. For a bonded host `host` is its number; for a computer attaching for the first time through the window, `host:0` |
| `host_passkey_request` | `host`, `addr` | The 6 digits on the host's screen must be entered. Type them on a keyboard attached to the router and press Enter (meanwhile keystrokes go to no host), or use `host_passkey`. Esc cancels |
| `host_passkey_progress` | `host`, `addr`, `len` | Number of digits typed on the keyboard (the digits themselves are not included) |
| `host_passkey_sent` | `host`, `addr`, `ok` | The passkey was handed to the security procedure (SMP) |
| `host_paired` | `host`, `addr` | Saved the new host under that number, or a known host paired again (same number). The window closes |
| `host_pair_failed` | `host`, `addr`, `status`, `authenticated` | Host pairing (or key restore) failed. A new bond that is not authenticated is deleted and the connection dropped |
| `host_repair_blocked` | `host`, `addr` | A bonded host tried to pair again (the board was removed on that computer). Outside the window (or the new pairing was not authenticated), so it was rejected and disconnected. If it reconnects while the window is open, it is accepted (number and name are kept). With all 3 host slots in use the window does not open (`hosts_full`); remove that host and add it again |
| `host_ready` | `host`, `addr` | The host enabled notifications and can receive reports |
| `host_name` | `host`, `addr`, `name` | The host's GAP Device Name |
| `host_conn_params` | `host`, `addr`, `status`, `itvl`, `latency`, `timeout` | The host connection parameters changed. The board asks the active host for 7.5 ms with latency 4, and for 15 ms if its controller or the host refuses; idle hosts get 15 ms with latency 30; supervision timeout 6 s. The host has the final say (measured: Windows takes 7.5 ms; for a Mac link the board's controller refuses it, so a Mac stays at 15 ms) |
| `host_sec_request` | `host`, `addr`, `rc` | A bonded host did not start encryption within 1 s, so the board sent a Security Request (`rc` 0 = sent). A fallback; normally the host encrypts by itself |
| `host_disconnected` | `host`, `addr`, `reason` | A host disconnected (also when its link drops before it is fully connected; `reason` is then a NimBLE status). The board advertises fast (20 ms) for 30 seconds, then at 152.5 ms |
| `host_removed` | `host` | One host was removed |
| `host_moved` | `host`, `to` | The host at `host` is now `to`, and the host that was at `to`, if any, is now `host` |
| `host_nick` | `host`, `nick` | A host's name changed |

## Flows

### Adding a new device

1. `pair_scan` `{"on":true}` → watch for `device_found`
2. `pair_connect` `{"addr":"..."}` (the board pauses the scan for the attempt, then resumes it; `pair_scan` `{"on":false}` stops it)
3. Answer according to the events
   - No request at all: Just Works (most mice, some keyboards)
   - `numcmp` → `pair_accept` `{"accept":true}`
   - `passkey_request` → `passkey` `{"code":"123456"}`
   - `passkey_display` → type the number the board shows on the device
4. `encrypted` `bond:"new"` → `paired` → `hid_ready` → `device_name`

### Adding a host (Mac, Windows)

1. `host_pair` `{"on":true}` → `host_window` `open:true`
2. Connect to "Blue Baton" in the computer's Bluetooth settings → `host_connected` `host:0`
3. `host_passkey_request` → type the 6 digits from the computer's screen on the router's keyboard and press Enter, or `host_passkey` `{"code":"123456"}`
4. `host_passkey_sent` → `host_paired` `host:N` → `host_window` `open:false` → `host_ready` → `host_name`
5. From then on, the computer reconnects by itself when it powers on (`host_connected` `bonded:true` → `host_ready`)

Only while the window is open does the board present itself as a keyboard (IO capability KeyboardOnly), and it immediately drops any connection that is neither a bonded host nor a new computer within the window.

### Already paired devices

- At boot, or when a device disconnects, the board automatically waits for it to reconnect (`autoconnect`).
- The device connects when it advertises. The sequence is `connected` `bonded:true` → `encrypted` `bond:"restored"` → `paired` → `hid_ready`. No user action is needed.
- If the device has lost its own keys, the stack pairs again automatically. In that case `bond:"new"` arrives (NimBLE `BLE_RESTART_PAIR`, on by default).

## Known limitations

- Up to 2 paired input devices, of any kind (`inputs_max`), and 3 hosts.
- Input devices start the security procedure (pairing, key restore) one at a time. If two input devices attach at the same time, one waits briefly.
- Some devices create a new address (identity) every time they enter pairing mode. They then show up as a new device, and the user deletes the old record by looking at `last_seq`.
- The found list holds at most 16 devices; when it overflows, the device seen longest ago is pushed out. Names are at most 23 bytes.

## Host switch combos

- Format: one or more modifiers (`ctrl`/`control`, `shift`, `alt`/`option`/`opt`, `cmd`/`command`/`gui`/`win`/`meta`/`super`) + one non-modifier key. Non-modifier keys: `a`–`z`, `0`–`9`, `f1`–`f24`, `space`, `enter`/`return`, `esc`/`escape`, `tab`, `backspace`, `minus`, `equal`, `grave`/`backquote`, `home`, `end`, `pageup`, `pagedown`, `insert`, `delete`, arrow keys (`left` etc.), `scrolllock`, `pause`, `printscreen`, `capslock`, or a Usage value such as `0x3a`
- An empty entry in the list leaves that host without a combo (e.g. `ctrl+alt+1,,ctrl+alt+3`)
- A single key without a modifier is not accepted (it would steal normal input)
- Default: `ctrl+alt+1,ctrl+alt+2,ctrl+alt+3`
- Behavior (same as a typical KVM)
  - The switch happens at the moment the combo's key is pressed, if the set of held modifiers exactly matches the combo and no other non-modifier key is held. Left and right modifiers are not distinguished
  - Keys held at that moment are not sent to any host until each one is released (the combo does not leak)
  - The previous host receives a "release-all". Pressing the combo of the host already in use swallows the combo without switching
  - The combo of a host that is not connected is swallowed the same way, without switching
  - When an input device disconnects, the keys it was holding are treated as released
  - The active host is kept across restarts (power, firmware update)

## 0.7 → 0.7.1 changes

- The active host is asked for a 7.5 ms connection interval, and for 15 ms if that is refused (before: 15 ms for
  every host). At 15 ms a host gets reports at most once per 15 ms, so the cursor moved at most ~66 times a second.
  Developer builds add `host_itvl` to try other intervals.
- Only a connected (`ready`) host can be switched to: `host_select` answers `host_offline` for another one, and its
  switch combo does nothing (its keys still do not reach the active host).
- New commands `host_move` (hosts trade numbers without reconnecting) and `host_nick` (a short name for a host), with
  events `host_moved` and `host_nick` and the error `bad_nick`. `host_list` entries have `nick`.
- The paired-device records are stored in a new format (v4, adds the host name); records from 0.7 are converted at
  boot.
- Lines to the computer go through an 8 KB buffer that a task on the other core writes out, so the Bluetooth task
  never waits for the computer to read USB. If the computer stops reading and the buffer fills, new lines are
  dropped. Input also goes to the host before the board queues the matching lines (`activity`, `report`, `route`,
  `host_switch`). Developer builds add `usb_lines`, `usb_avg_us`, `usb_max_us` and `usb_dropped` to `stats`.

## 0.6 → 0.7 changes

- Wi-Fi is gone: no Wi-Fi setup API, no `pin_set` or `bad_pin`, and `hello` no longer has `wifi_feature`. A PIN, Wi-Fi
  credentials or AP password stored by earlier firmware are erased at boot.
- The console no longer prints a status line every 5 seconds.
- Developer diagnostics are one build option, `CONFIG_KM_DIAG`, off by default: the detailed `stats` fields and the new
  `selftest` command. The developer flash script (`tools/flash.sh`) builds with it on.
- When a firmware update changes the GATT table, bonded hosts are told once (Service Changed) so they read it again.
- Latency: a mouse report no longer waits for the next host interval (was at most 1 per host interval, which made
  motion wait up to an interval); see report delivery below. New command `stats`; `status` `peers` entries have
  `itvl`; `status` has `heap_free` and `heap_min` (lowest free heap since boot, bytes).
- Build: `-O2`, 240 MHz, 32 KB instruction cache, NimBLE log level WARNING (NimBLE logged every notification at
  INFO). Handing a mouse report to the controller went from 1.13 ms to 0.07 ms on average.
- Auto-reconnect retries after a failed start (new event `autoconnect_failed`); before, the radio stayed idle.
- Keyboard LEDs (Num Lock, Caps Lock, ...) follow the active host: the board writes the host's LED state to the
  keyboard's LED output report when it changes, after a host switch, and when the keyboard connects. `report_ref`
  events now also list output reports (`type` 2).
- Input devices are asked for 8.75 ms (was 7.5–15 ms, where the controller picked 15 ms). With a host connected
  the controller refuses a fixed 7.5 ms (HCI 0x0D).
- Discovery starts with an ATT MTU exchange (`hid_ready` `mtu`); the Report Map is read in far fewer requests.
- System keys (Generic Desktop Power Down, Sleep, Wake Up) from input devices reach the host (`route` `system`).
- Media keys: a report with every array slot empty now releases the key (before, it was ignored and the key stayed
  held on the host); Usages listed one by one are no longer limited to 6, and array values select from such a list.
- A report the stack could not take is no longer remembered as sent, so the next change carries the full state.
- The active host is kept across restarts (was always host 1 after a restart).
- Report delivery to hosts: at most 2 reports per host are in the controller at once (counted by its completion
  reports); further key, button, media and system changes wait in order (if 8 are already waiting, an older change gives way to a later one of the
  same report), and mouse motion adds up into the next mouse report. Before, every report was queued in the stack,
  and reports were dropped when the stack's buffers ran out (a key-up could have been among them).
- Idle hosts are asked for peripheral latency 30 and the active host for 4; a switch re-requests both.
- A device with no HID service, or none of the input the board forwards, is disconnected and its bond deleted
  (`failed` `stage:"no_usable_input"` is new).
- At most 2 input devices are paired (`inputs`, `inputs_max` in `status`; a new one gets `inputs_full`). Auto-reconnect no
  longer stops for a device that drops before it is ready (`autoconnect_paused` is gone).
- A report that finds no stack buffer is tried again after 10 ms when nothing else is in flight on that host (it
  waited up to 1 s for the next tick). A known host that pairs again inside the window closes it. The active host is
  saved to flash after the switch's reports have gone to the stack.
- Auto-reconnect listens 30 ms every 60 ms only for 30 s after a device drops, then 11.25 ms every 1.28 s (it
  listened half the time indefinitely). A sleeping device may take up to about 1.3 s longer to come back.

## 0.5 → 0.6 changes

- USB is no longer locked: `auth` and `lock` are gone, `hello` no longer has `pin_set` and `unlocked`, and the
  `locked`, `no_pin` and `wrong_pin` codes are gone. `pin_set` remains only in Wi-Fi builds (for the Wi-Fi setup API).
  A PIN stored by earlier firmware is erased at boot in builds without Wi-Fi.
- Input content (`report`, `route`) goes out only under the new `monitor` lease. New event `activity` (no content).

## 0.4 → 0.5 changes

- Added the output side: the board appears to 3 hosts as an HID keyboard + mouse (HOGP). All connections are kept at the same time, and reports go only to the active host.
- New commands: `host_pair`, `host_list`, `host_remove`, `host_passkey`
- New events: `host_online`, `host_window`, `host_connected`, `host_passkey_request`, `host_passkey_progress`, `host_passkey_sent`, `host_paired`, `host_pair_failed`, `host_repair_blocked`, `host_ready`, `host_name`, `host_conn_params`, `host_disconnected`, `host_removed`, `host_sec_request`
- New failure `code`s: `host_window`, `hosts_full`, `bad_code`, `locked`, `no_pin`, `wrong_pin`, `bad_pin`, `bad_request`, `unknown_cmd`. Every failure now has a specific `code`; the generic `code:"error"` is no longer sent
- While the host pairing window is open, `pair_scan` `on:true` and `pair_connect` to a new device are rejected with `code:"host_window"`.

## 0.3 → 0.4 changes

- The paired list now comes from the NimBLE bond store. Bond keys are stored persistently in NVS. The 0.3 `paired` records (v1) have no keys, so they are deleted at boot.
- Added auto-reconnect (accept list). Events: `autoconnect`, `autoconnect_paused`.
- New commands: `disconnect`, `paired_clear`, `all` for `pair_list`.
- `pair_cancel` no longer disconnects connected devices.
- Added `code` to failure responses.
- New event fields
  - `encrypted`: `bond`, `authenticated`, `key_size`
  - `connected`: `id_addr`, `bonded`, `user`
  - `report_ref`, `report_map`: `svc`
  - `report_map`: `kinds`, `truncated`
  - `hid_ready`: `kinds`, `reports`, `services`
- New events: `ble_ready`, `ble_reset`, `scan_started`, `hid_info`, `device_name`, `failed`, `bond_full`, `unpaired`, `unpaired_all`
- The `no_hid_service` event became `failed` `stage:"no_hid_service"`.
- The `code` of `numcmp` and `passkey_display` is a 6-digit string (leading zeros preserved).
- Added `radio`, `bonds`, `bonds_max` to `status`. Added `id_addr`, `state` to `peers` entries.
- Handles devices with multiple HID services (0.3 used only the first).
- Scanning merges advertising and scan response data to get names and UUIDs. It stops by itself after 60 seconds.
