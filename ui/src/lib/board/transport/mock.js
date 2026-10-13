// SPDX-License-Identifier: GPL-3.0-only
/*
 * Mock board. Lets us build and check the UI without hardware.
 * Imitates the firmware 0.6 commands, replies, and event formats (firmware/PROTOCOL.md).
 *
 * Devices
 * - The mouse and keyboard captured from real hardware: their Report Maps, handles, and report IDs as-is.
 * - Pairing methods we could not test on real hardware are virtual devices: numeric comparison, passkey entry,
 *   passkey display, a device whose name is only in the scan response, and a non-HID device.
 *
 * Hosts: host 1 is a bonded mock Mac (connects after boot). Opening the host pairing window makes a mock PC
 * connect 2 s later and request a passkey. The code shown on that PC is printed as a board log line.
 *
 * URL options: ?mock turns it on, &fast (10 s scan)
 */

import { t } from '../i18n.js';

const MOUSE_MAP =
  '05010902A10185010901A1000509190129051500250195057501810295017503810105010930093116018026FF7F75109502' +
  '810609381581257F750895018106C0C0';
const KB_MAP =
  '05010902A10185010901A100050919012905150025019505750181029501750381010501093009311581257F750895028106' +
  '09381581257F750895018106050C0A38021581257F750895018106C0C0050C0901A101850275109501150126FF0319012AFF' +
  '038160C005010906A1018503050719E029E71500250175019508810295017508810195057501050819012905910295017503' +
  '910195067508150025E70507190029E78100C005010906A101850475019578050715002501190029788102C0050C0901A101' +
  '85051500250175019503810175019501050C09B8810206FF0009038102750195038101C0';
/* Virtual keyboard: report ID 1, 8 modifier bits + 1 reserved byte + 6-key array */
const SIMPLE_KB_MAP =
  '05010906A1018501050719E029E715002501750195088102950175088101950675081500256505071900296581' + '00C0';

function catalog() {
  return [
    {
      addr: '00:00:5E:00:53:17', name: 'Xiaoxin Mplus 5.0', appearance: 962, io: 'none', bonded: true,
      map: MOUSE_MAP, kinds: ['mouse'], reports: [{ val: 25, id: 1 }, { val: 29, id: 18 }], role: 'mouse',
      wakeAfter: 1500, lastSeq: 1,
    },
    {
      addr: '00:00:5E:00:53:22', name: 'ABKO AK84BT-1', appearance: 961, io: 'none', bonded: true,
      map: KB_MAP, kinds: ['keyboard', 'mouse', 'consumer'],
      reports: [{ val: 64, id: 1 }, { val: 68, id: 2 }, { val: 72, id: 3 }, { val: 80, id: 4 }, { val: 84, id: 5 }],
      role: 'nkro', wakeAfter: 3500, lastSeq: 2,
    },
    {
      addr: '00:00:5E:00:53:41', name: 'Mock keyboard (numeric comparison)', appearance: 961, io: 'numcmp',
      map: SIMPLE_KB_MAP, kinds: ['keyboard'], reports: [{ val: 20, id: 1 }], role: 'simple', foundAfter: 1200,
    },
    {
      addr: '00:00:5E:00:53:42', name: 'Mock keyboard (passkey entry)', appearance: 961, io: 'passkey_request',
      map: SIMPLE_KB_MAP, kinds: ['keyboard'], reports: [{ val: 20, id: 1 }], role: 'simple', foundAfter: 2000,
      shownCode: '123456',
    },
    {
      addr: '00:00:5E:00:53:43', name: 'Mock keyboard (passkey display)', appearance: 961, io: 'passkey_display',
      map: SIMPLE_KB_MAP, kinds: ['keyboard'], reports: [{ val: 20, id: 1 }], role: 'simple', foundAfter: 2800,
    },
    {
      addr: '00:00:5E:00:53:44', name: 'Mock mouse (name only in scan response)', appearance: 962, io: 'none',
      map: MOUSE_MAP, kinds: ['mouse'], reports: [{ val: 25, id: 1 }], role: 'mouse', foundAfter: 3500,
      nameLate: true,
    },
    { addr: '00:00:5E:00:53:51', name: 'Mock earbuds (not HID)', appearance: 0, hid: false, foundAfter: 900 },
  ];
}

/* Same as the firmware (ble_pair.c) */
const MONITOR_LEASE_MS = 60000;
const ACTIVITY_MS = 250;

const hex = (bytes) => bytes.map((b) => (b & 0xff).toString(16).padStart(2, '0').toUpperCase()).join('');
const i16 = (v) => [v & 0xff, (v >> 8) & 0xff];

class MockBoard {
  constructor(emit, opts) {
    this.emit = emit;
    this.opts = opts;
    this.devices = catalog();
    this.monitorUntil = 0;     /* input monitor lease (performance.now() ms) */
    this.activityAt = new Map();   /* addr -> last activity event */
    this.combo = '';
    this.activeHost = 1;
    this.scanning = false;
    this.radio = 'idle';
    this.peers = new Map();   /* addr -> {dev, state, user} */
    this.timers = new Set();
    this.seq = 3;
    /* Hosts: number -> {addr, name, connected, state}. Host 1 is a bonded mock Mac */
    this.hosts = new Map([[1, { addr: '00:00:5E:00:53:31', name: 'Mock MacBook Pro', connected: false, state: 'offline' }]]);
    this.hostWindow = null;   /* timer that closes the window */
    this.candidate = null;    /* {addr, code} mock PC that connected through the window */
    this.boot();
  }

  later(ms, fn) {
    const t = setTimeout(() => {
      this.timers.delete(t);
      fn();
    }, ms);
    this.timers.add(t);
    return t;
  }

  every(ms, fn) {
    const t = setInterval(fn, ms);
    this.timers.add(t);
    return t;
  }

  stop() {
    for (const t of this.timers) {
      clearTimeout(t);
      clearInterval(t);
    }
    this.timers.clear();
  }

  bonded() {
    return this.devices.filter((d) => d.bonded);
  }

  boot() {
    this.later(50, () => {
      this.ev({ event: 'ble_ready', bonds: this.bonded().length, max: 8 });
      this.radioUpdate();
      for (const d of this.bonded()) this.later(d.wakeAfter, () => this.wake(d));
      this.later(800, () => this.hostConnect(1));
    });
    this.every(5000, () => this.emit(`I (${Math.round(performance.now())}) km: alive (mock)`));
    this.every(40, () => this.tickInputs());
  }

  ev(obj) {
    this.emit('KM< ' + JSON.stringify(obj));
  }

  reply(id, fields = {}) {
    this.ev({ id, ok: true, ...fields });
  }

  fail(id, code, error) {
    this.ev({ id, ok: false, code, error });
  }

  freeSlots() {
    return 2 - this.peers.size;
  }

  autoCandidates() {
    return this.bonded().filter((d) => !this.peers.has(d.addr) && d.auto !== false);
  }

  radioUpdate() {
    if (this.scanning) {
      this.radio = 'scan';
      return;
    }
    const n = this.autoCandidates().length;
    if (this.freeSlots() > 0 && n > 0) {
      if (this.radio !== 'auto') this.ev({ event: 'autoconnect', devices: n });
      this.radio = 'auto';
    } else {
      this.radio = 'idle';
    }
  }

  /* A bonded device wakes up and advertises: it connects if the board is waiting to auto-reconnect */
  wake(d) {
    if (this.radio !== 'auto' || this.peers.has(d.addr) || d.auto === false || !d.bonded) return;
    this.connect(d, false);
  }

  connect(d, user) {
    this.peers.set(d.addr, { dev: d, state: 'connected', user });
    this.ev({ event: 'connected', addr: d.addr, id_addr: d.addr, bonded: !!d.bonded, user });
    this.radio = 'idle';
    this.radioUpdate();
    if (d.bonded) {
      this.later(100, () => this.secured(d, 'restored'));
      return;
    }
    const p = this.peers.get(d.addr);
    p.state = 'securing';
    if (d.io === 'none') {
      this.later(700, () => this.secured(d, 'new'));
    } else if (d.io === 'numcmp') {
      p.code = String(100000 + Math.floor(Math.random() * 900000));
      this.later(400, () => this.ev({ event: 'numcmp', addr: d.addr, code: p.code }));
      this.emit(`I (mock) code shown on the mock device: ${p.code}`);
    } else if (d.io === 'passkey_request') {
      this.later(400, () => this.ev({ event: 'passkey_request', addr: d.addr }));
      this.emit(`I (mock) passkey shown on the mock device: ${d.shownCode}`);
    } else if (d.io === 'passkey_display') {
      const code = String(Math.floor(Math.random() * 1e6)).padStart(6, '0');
      this.later(400, () => this.ev({ event: 'passkey_display', addr: d.addr, code, ok: true }));
      /* time the user takes to type the code on the keyboard */
      this.later(5000, () => this.peers.has(d.addr) && this.secured(d, 'new'));
    }
  }

  secured(d, bond) {
    const p = this.peers.get(d.addr);
    if (!p) return;
    p.state = 'discovering';
    this.ev({ event: 'encrypted', addr: d.addr, ok: true, status: 0, bond, authenticated: d.io !== 'none', key_size: 16 });
    if (bond === 'new') {
      d.bonded = true;
      d.lastSeq = d.lastSeq || 0;
    }
    this.ev({ event: 'paired', addr: d.addr, id_addr: d.addr, name: bond === 'new' && d.nameLate ? '' : d.name, bond });
    let t = 150;
    for (const r of d.reports) {
      this.later(t, () => this.ev({ event: 'report_ref', addr: d.addr, val: r.val, id: r.id, type: 1, svc: 0 }));
      t += 100;
    }
    this.later(t, () => this.ev({ event: 'hid_info', addr: d.addr, svc: 0, bcd_hid: 273, country: 0, remote_wake: true, normally_connectable: true }));
    this.later(t + 300, () => this.ev({ event: 'report_map', addr: d.addr, svc: 0, len: d.map.length / 2, truncated: false, kinds: d.kinds, hex: d.map }));
    this.later(t + 400, () => {
      if (!this.peers.has(d.addr)) return;
      p.state = 'ready';
      p.readyAt = performance.now();
      d.lastSeq = this.seq++;
      this.ev({ event: 'hid_ready', addr: d.addr, id_addr: d.addr, kinds: d.kinds, reports: d.reports.length, services: 1 });
      this.ev({ event: 'device_name', addr: d.addr, name: d.name });
    });
  }

  failSecurity(d, status) {
    this.ev({ event: 'encrypted', addr: d.addr, ok: false, status, bond: 'none', authenticated: false, key_size: 0 });
    this.ev({ event: 'failed', addr: d.addr, stage: 'security', status });
    this.drop(d, 534);
  }

  drop(d, reason) {
    if (!this.peers.delete(d.addr)) return;
    this.ev({ event: 'disconnected', addr: d.addr, id_addr: d.bonded ? d.addr : '', reason });
    this.radioUpdate();
  }

  /* Input from a device: activity (throttled, no content) always; the report itself only while the monitor is on */
  input(addr, handle, data) {
    const now = performance.now();
    if (now - (this.activityAt.get(addr) ?? -Infinity) >= ACTIVITY_MS) {
      this.activityAt.set(addr, now);
      this.ev({ event: 'activity', addr });
    }
    if (now < this.monitorUntil) this.ev({ event: 'report', addr, handle, data });
  }

  /* Ready devices send input: the mouse moves in circles, the keyboard types text */
  tickInputs() {
    const now = performance.now();
    for (const p of this.peers.values()) {
      if (p.state !== 'ready') continue;
      const d = p.dev;
      const t = (now - p.readyAt) / 1000;
      if (d.role === 'mouse') {
        const phase = t % 6;
        if (phase < 1.6) {
          const a = phase * 4;
          const btn = phase > 1.4 ? 1 : 0;
          this.input(d.addr, 25, hex([btn, ...i16(Math.round(Math.cos(a) * 6)), ...i16(Math.round(Math.sin(a) * 6)), 0]));
        }
      } else if (d.role === 'nkro' || d.role === 'simple') {
        const text = 'hello km ';
        const step = Math.floor((t % 9) / 0.15);   /* one step every 0.15 s: press, release */
        if (step === p.lastStep || step >= text.length * 2) continue;
        p.lastStep = step;
        const ch = text[step >> 1];
        const usage = ch === ' ' ? 0x2c : 0x04 + ch.charCodeAt(0) - 97;
        const down = (step & 1) === 0;
        if (d.role === 'nkro') {
          const bits = new Array(15).fill(0);
          if (down) bits[usage >> 3] |= 1 << (usage & 7);
          this.input(d.addr, 80, hex(bits));
        } else {
          this.input(d.addr, 20, hex([0, 0, down ? usage : 0, 0, 0, 0, 0, 0]));
        }
      }
    }
  }

  startScan() {
    if (this.scanning) return;
    this.scanning = true;
    this.radio = 'scan';
    this.ev({ event: 'scan_started' });
    for (const d of this.devices) {
      if (d.bonded || !d.foundAfter) continue;
      this.later(d.foundAfter, () => {
        if (!this.scanning || this.peers.has(d.addr) || d.hid === false) return;
        const paired = !!d.bonded;
        this.ev({ event: 'device_found', addr: d.addr, name: d.nameLate ? '' : d.name, rssi: -40 - Math.floor(Math.random() * 40), paired, appearance: d.appearance });
        if (d.nameLate) {
          /* Name only in the scan response: the same address comes again with the name filled in */
          this.later(900, () => this.scanning && this.ev({ event: 'device_found', addr: d.addr, name: d.name, rssi: -45, paired, appearance: d.appearance }));
        }
      });
    }
    this.scanTimer = this.later(this.opts.fast ? 10000 : 60000, () => {
      this.scanning = false;
      this.ev({ event: 'scan_stopped', reason: 'timeout' });
      this.radioUpdate();
    });
  }

  stopScan(reason) {
    if (!this.scanning) return;
    this.scanning = false;
    clearTimeout(this.scanTimer);
    this.ev({ event: 'scan_stopped', reason });
    this.radioUpdate();
  }

  find(addr) {
    return this.devices.find((d) => d.addr === addr);
  }

  /* ---- Hosts ---- */

  hostConnect(n) {
    const h = this.hosts.get(n);
    if (!h || h.connected) return;
    h.connected = true;
    h.state = 'connected';
    this.ev({ event: 'host_connected', host: n, addr: h.addr, bonded: true, itvl: 24 });
    this.later(100, () => {
      h.state = 'ready';
      this.ev({ event: 'host_ready', host: n, addr: h.addr });
    });
  }

  freeHost() {
    for (let n = 1; n <= 3; n++) if (!this.hosts.has(n)) return n;
    return 0;
  }

  openHostWindow(open) {
    if (open && !this.freeHost()) return false;
    if (!!this.hostWindow === open) return true;
    if (open) {
      this.stopScan('radio');
      this.hostWindow = this.later(180000, () => this.openHostWindow(false));
      /* time the user takes to pick "Blue Baton" on the computer */
      this.later(2000, () => {
        if (!this.hostWindow || this.candidate) return;
        this.candidate = { addr: '00:00:5E:00:53:6' + this.freeHost(), code: String(100000 + Math.floor(Math.random() * 900000)) };
        this.ev({ event: 'host_connected', host: 0, addr: this.candidate.addr, bonded: false, itvl: 24 });
        this.later(500, () => {
          if (!this.candidate) return;
          this.emit(`I (mock) code shown on the mock PC: ${this.candidate.code}`);
          this.ev({ event: 'host_passkey_request', host: 0, addr: this.candidate.addr });
        });
      });
    } else {
      clearTimeout(this.hostWindow);
      this.timers.delete(this.hostWindow);
      this.hostWindow = null;
      if (this.candidate) {
        this.ev({ event: 'host_disconnected', host: 0, addr: this.candidate.addr, reason: 534 });
        this.candidate = null;
      }
    }
    this.ev({ event: 'host_window', open, seconds: open ? 180 : 0 });
    return true;
  }

  hostPasskey(code) {
    const c = this.candidate;
    this.ev({ event: 'host_passkey_sent', host: 0, addr: c.addr, ok: true });
    if (code !== c.code) {
      this.later(300, () => {
        this.ev({ event: 'host_pair_failed', host: 0, addr: c.addr, status: 1284, authenticated: false });
        this.ev({ event: 'host_disconnected', host: 0, addr: c.addr, reason: 534 });
        this.candidate = null;
      });
      return;
    }
    this.later(300, () => {
      const n = this.freeHost();
      this.candidate = null;
      this.hosts.set(n, { addr: c.addr, name: '', connected: true, state: 'secured' });
      this.ev({ event: 'host_paired', host: n, addr: c.addr });
      this.openHostWindow(false);
      this.later(400, () => {
        const h = this.hosts.get(n);
        if (!h) return;
        h.state = 'ready';
        this.ev({ event: 'host_ready', host: n, addr: c.addr });
        h.name = 'Mock Windows PC';
        this.ev({ event: 'host_name', host: n, addr: c.addr, name: h.name });
      });
    });
  }

  handle(req) {
    const { id, cmd } = req;
    switch (cmd) {
      case 'hello':
        return this.reply(id, { fw: 'blue-baton', version: '0.7-mock', combo_set: !!this.combo });
      case 'monitor':
        if (typeof req.on !== 'boolean') return this.fail(id, 'bad_request', 'on (true or false) is missing');
        this.monitorUntil = req.on ? performance.now() + MONITOR_LEASE_MS : 0;
        return this.reply(id, { lease_ms: req.on ? MONITOR_LEASE_MS : 0 });
      case 'combo_get':
        return this.reply(id, { combo: this.combo || 'ctrl+alt+1,ctrl+alt+2,ctrl+alt+3', default: !this.combo, stored_invalid: false });
      case 'host_select': {
        if (!(req.host >= 1 && req.host <= 3)) return this.fail(id, 'bad_host', 'host must be 1 to 3');
        const from = this.activeHost;
        if (req.host !== from && this.hosts.get(req.host)?.state !== 'ready') return this.fail(id, 'host_offline', 'That host is not connected');
        this.reply(id);
        if (req.host !== from) {
          this.activeHost = req.host;
          this.ev({ event: 'host_switch', host: req.host, from });
          this.ev({ event: 'host_online', host: req.host, online: this.hosts.get(req.host)?.state === 'ready' });
          if (performance.now() < this.monitorUntil) this.ev({ event: 'route', host: req.host, keys: [], buttons: 0, consumer: [] });
        }
        return;
      }
      case 'host_pair':
        if (typeof req.on !== 'boolean') return this.fail(id, 'bad_request', 'on (true or false) is missing');
        if (!this.openHostWindow(req.on)) return this.fail(id, 'hosts_full', 'All 3 host slots are in use. Remove one, then open the window again');
        return this.reply(id);
      case 'host_list': {
        const hosts = [...this.hosts.entries()].sort((x, y) => x[0] - y[0]).map(([n, h]) => ({
          host: n, addr: h.addr, name: h.name, nick: h.nick || '', connected: h.connected, state: h.connected ? h.state : 'offline', active: this.activeHost === n,
        }));
        return this.reply(id, { hosts });
      }
      case 'host_remove': {
        if (!(req.host >= 1 && req.host <= 3)) return this.fail(id, 'bad_host', 'host must be 1 to 3');
        const h = this.hosts.get(req.host);
        if (!h) return this.fail(id, 'not_found', 'No host is paired at that number');
        this.reply(id);
        if (h.connected) this.ev({ event: 'host_disconnected', host: req.host, addr: h.addr, reason: 534 });
        this.hosts.delete(req.host);
        return this.ev({ event: 'host_removed', host: req.host });
      }
      case 'host_move': {
        const ok = (x) => x >= 1 && x <= 3;
        if (!ok(req.host) || !ok(req.to)) return this.fail(id, 'bad_host', 'host and to must be 1 to 3');
        const h = this.hosts.get(req.host);
        if (!h) return this.fail(id, 'not_found', 'No host is paired at that number');
        this.reply(id);
        if (req.host === req.to) return;
        const other = this.hosts.get(req.to);
        this.hosts.delete(req.host);
        this.hosts.delete(req.to);
        this.hosts.set(req.to, h);
        if (other) this.hosts.set(req.host, other);
        if (this.activeHost === req.host) this.activeHost = req.to;
        else if (this.activeHost === req.to) this.activeHost = req.host;
        return this.ev({ event: 'host_moved', host: req.host, to: req.to });
      }
      case 'host_nick': {
        if (!(req.host >= 1 && req.host <= 3)) return this.fail(id, 'bad_host', 'host must be 1 to 3');
        const nick = typeof req.nick === 'string' ? req.nick : null;
        if (nick === null || [...nick].length > 8 || /[\u0000-\u001f\u007f]/.test(nick)) {
          return this.fail(id, 'bad_nick', 'nick must be up to 8 characters');
        }
        const h = this.hosts.get(req.host);
        if (!h) return this.fail(id, 'not_found', 'No host is paired at that number');
        h.nick = nick;
        this.reply(id);
        return this.ev({ event: 'host_nick', host: req.host, nick });
      }
      case 'host_passkey':
        if (!/^\d{6}$/.test(req.code || '')) return this.fail(id, 'bad_code', 'Passkey must be 6 digits');
        if (!this.candidate) return this.fail(id, 'no_request', 'No host is waiting for a passkey');
        this.reply(id);
        return this.hostPasskey(req.code);
      case 'combo_set':
        this.combo = req.combo || '';
        return this.reply(id);
      case 'status': {
        const peers = [...this.peers.values()].map((p) => ({ addr: p.dev.addr, id_addr: p.dev.bonded ? p.dev.addr : '', state: p.state === 'securing' ? 'securing' : p.state, hid_ready: p.state === 'ready' }));
        return this.reply(id, {
          scanning: this.scanning, connected: peers.length > 0, hid_ready: peers.some((p) => p.hid_ready),
          peer: peers[0]?.addr || '', peers, radio: this.radio, bonds: this.bonded().length, bonds_max: 8,
          inputs: this.bonded().length, inputs_max: 2,
          active_host: this.activeHost,
        });
      }
      case 'pair_scan':
        if (req.on && this.hostWindow) return this.fail(id, 'host_window', 'The host pairing window is open. Close it before adding an input device');
        if (req.on) this.startScan();
        else this.stopScan('radio');
        return this.reply(id);
      case 'pair_list': {
        const list = this.devices
          .filter((d) => d.foundAfter && !this.peers.has(d.addr) && (req.all || d.hid !== false))
          .map((d) => ({ addr: d.addr, name: d.name, rssi: -55, paired: !!d.bonded, appearance: d.appearance, hid: d.hid !== false }));
        return this.reply(id, { devices: list });
      }
      case 'pair_connect': {
        const d = this.find(req.addr);
        if (!d || d.hid === false) return this.fail(id, 'not_found', 'No device with that address');
        if (this.peers.has(d.addr)) return this.fail(id, 'busy', 'Not possible right now (BLE not ready, already connected, or another request in progress)');
        if (this.freeSlots() <= 0) return this.fail(id, 'no_slot', 'All input device slots are in use');
        if (d.bonded) {
          /* Same as the firmware: a bonded device is not connected directly but put back on the auto-connect list. It connects when it wakes */
          d.auto = true;
          this.stopScan('radio');
          this.reply(id);
          this.radio = 'idle';
          this.radioUpdate();
          return this.later(1500, () => this.wake(d));
        }
        if (this.bonded().length >= 2 && !d.bonded) {
          return this.fail(id, 'inputs_full', 'Two input devices are paired already. Remove one before adding another');
        }
        d.auto = true;
        this.stopScan('radio');
        this.reply(id);
        this.ev({ event: 'connecting', addr: d.addr });
        this.radio = 'connect';
        return this.later(300, () => this.connect(d, true));
      }
      case 'disconnect': {
        const p = this.peers.get(req.addr);
        if (!p) return this.fail(id, 'not_found', 'No device with that address');
        p.dev.auto = false;
        this.reply(id);
        return this.drop(p.dev, 534);
      }
      case 'pair_accept': {
        const p = [...this.peers.values()].find((x) => x.code);
        if (!p) return this.fail(id, 'no_request', 'No request to accept right now');
        p.code = null;
        this.reply(id);
        if (req.accept) return this.later(300, () => this.secured(p.dev, 'new'));
        return this.failSecurity(p.dev, 1288);
      }
      case 'passkey': {
        const p = [...this.peers.values()].find((x) => x.dev.io === 'passkey_request' && x.state === 'securing');
        if (!p) return this.fail(id, 'no_request', 'No request is waiting for a passkey');
        this.reply(id);
        if (req.code === p.dev.shownCode) return this.later(300, () => this.secured(p.dev, 'new'));
        return this.failSecurity(p.dev, 1284);
      }
      case 'pair_cancel':
        this.stopScan('radio');
        for (const p of [...this.peers.values()]) if (p.state === 'securing') this.failSecurity(p.dev, 1288);
        return this.reply(id);
      case 'paired_list': {
        const paired = this.bonded().map((d) => {
          const p = this.peers.get(d.addr);
          return { addr: d.addr, type: 1, name: d.name, kinds: d.kinds, connected: !!p, state: p ? p.state : 'offline', auto: d.auto !== false, last_seq: d.lastSeq || 0 };
        });
        return this.reply(id, { paired });
      }
      case 'paired_remove': {
        const d = this.find(req.addr);
        if (!d || !d.bonded) return this.fail(id, 'not_found', 'No device with that address');
        this.reply(id);
        this.drop(d, 534);
        d.bonded = false;
        this.ev({ event: 'unpaired', id_addr: d.addr });
        return this.radioUpdate();
      }
      case 'paired_clear':
        for (const d of this.bonded()) {
          this.drop(d, 534);
          d.bonded = false;
        }
        this.reply(id);
        this.ev({ event: 'unpaired_all' });
        return this.radioUpdate();
      default:
        return this.fail(id, 'unknown_cmd', 'Unknown command');
    }
  }
}

export class MockTransport {
  /* Shown in the header, in the current language */
  get label() {
    return t('transport.mock');
  }

  constructor(params = new URLSearchParams()) {
    this.opts = { fast: params.has('fast') };
    this.board = null;
    this.onLine = () => {};
    this.onClose = () => {};
  }

  isOpen() {
    return !!this.board;
  }

  async open() {
    this.board = new MockBoard((line) => this.onLine(line), this.opts);
  }

  async send(line) {
    const req = JSON.parse(line);
    /* Like a real board, the reply arrives a little later */
    setTimeout(() => this.board?.handle(req), 20);
  }

  async close() {
    this.board?.stop();
    this.board = null;
    this.onClose('closed');
  }
}
