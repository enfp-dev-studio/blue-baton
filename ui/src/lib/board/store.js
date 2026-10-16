// SPDX-License-Identifier: GPL-3.0-only
/*
 * UI state. The board is the source of truth, so state changes only through board replies and events
 * (no optimistic updates).
 *
 * Devices form a single list:
 *   devices: paired devices plus the device currently connecting (pairing). Keyed by identity address
 *   found:   devices found by a scan that are not paired yet. Keyed by over-the-air address. Shown only in "Add device"
 */

const LOG_MAX = 400;

export class Store {
  constructor() {
    this.state = {
      link: 'disconnected',        /* disconnected | connecting | connected */
      linkError: '',
      transport: '',
      fw: null,
      fwChip: null,                /* chip the board's firmware was built for (hello) */
      fwBundled: null,             /* firmware version the app carries (app/src-tauri/src/firmware.rs), null without one */
      appBoard: '',                /* the app's board link: searching | busy | open | released | other (no Blue Baton on it) */
      fwJob: null,                 /* firmware being written {stage: connecting|writing|done|failed, percent, error} */
      radio: 'idle',
      scanning: false,
      scanUntil: 0,
      bonds: 0,
      bondsMax: 0,
      inputs: 0,                   /* paired input devices, and how many may be */
      inputsMax: 2,
      activeHost: 1,               /* 1-based */
      hosts: new Map(),            /* number (1 to 3) -> {host, addr, name, state, connected} (host_list is the source) */
      hostWindow: { open: false, until: 0 },
      hostCandidate: null,         /* host that connected through the pairing window and has no number yet {addr, state} */
      hostPrompt: null,            /* host passkey entry {addr, len} */
      combos: [],                  /* switch combo string per host */
      combosDefault: true,
      devices: new Map(),
      found: new Map(),
      showAll: false,
      shown: true,
      prompt: null,                /* {type: numcmp|passkey_request|passkey_display, addr, code} */
      notice: null,                /* {kind: error|info, text} */
      log: [],
    };
    this.listeners = new Set();
    this.version = 0;
    this.scheduled = false;
  }

  subscribe(fn) {
    this.listeners.add(fn);
    return () => this.listeners.delete(fn);
  }

  /*
   * However many changes one batch of events makes, listeners run once, right after it. Not on an animation frame:
   * a hidden window gets none, and the tray menu follows the store while the settings window is closed
   */
  changed() {
    if (this.scheduled) return;
    this.scheduled = true;
    queueMicrotask(() => {
      this.scheduled = false;
      this.version++;   /* React reads this (useSyncExternalStore): the state object itself is updated in place */
      for (const fn of this.listeners) fn(this.state);
    });
  }

  set(patch) {
    Object.assign(this.state, patch);
    this.changed();
  }

  notice(kind, text) {
    this.state.notice = { kind, text, at: Date.now() };
    this.changed();
  }

  log(dir, text) {
    const s = this.state;
    s.log.push({ at: new Date(), dir, text });
    if (s.log.length > LOG_MAX) s.log.splice(0, s.log.length - LOG_MAX);
    this.changed();
  }

  /* ---- Device list ---- */

  deviceByAddr(addr) {
    for (const d of this.state.devices.values()) if (d.addr === addr || d.id === addr) return d;
    return null;
  }

  #device(key, init = {}) {
    let d = this.state.devices.get(key);
    if (!d) {
      d = { id: key, addr: key, name: '', kinds: [], state: 'offline', paired: false, auto: true, lastSeq: 0, ...init };
      this.state.devices.set(key, d);
    }
    return d;
  }

  /* paired_list reply: the source of paired devices. Connection state comes from events and status */
  applyPaired(list) {
    const s = this.state;
    const keep = new Set(list.map((p) => p.addr));
    for (const [key, d] of s.devices) {
      /* Not in the list: keep it only if it is a connection that is pairing right now */
      if (!keep.has(key) && !(d.state !== 'offline' && !d.paired)) s.devices.delete(key);
    }
    for (const p of list) {
      const d = this.#device(p.addr);
      d.paired = true;
      d.name = p.name || d.name;
      d.kinds = p.kinds?.length ? p.kinds : d.kinds;
      d.auto = p.auto;
      d.lastSeq = p.last_seq;
      if (p.connected) {
        if (d.state === 'offline') d.state = p.state;
      } else {
        d.state = 'offline';
      }
    }
    this.changed();
  }

  /* host_list reply */
  applyHosts(list) {
    const s = this.state;
    s.hosts = new Map(list.map((x) => [x.host, { ...x, nick: x.nick || '', state: x.connected ? x.state : 'offline' }]));
    this.changed();
  }

  applyStatus(st) {
    const s = this.state;
    s.radio = st.radio || s.radio;
    s.scanning = !!st.scanning;
    s.bonds = st.bonds ?? s.bonds;
    s.bondsMax = st.bonds_max ?? s.bondsMax;
    s.inputs = st.inputs ?? s.inputs;
    s.inputsMax = st.inputs_max ?? s.inputsMax;
    s.activeHost = st.active_host ?? s.activeHost;
    for (const p of st.peers || []) {
      const d = this.deviceByAddr(p.addr) || (p.id_addr && this.state.devices.get(p.id_addr));
      /* The board reports a pairing connection as securing too. For a new device the UI keeps showing "Pairing" */
      if (d && !(d.state === 'pairing' && p.state === 'securing')) d.state = p.state;
    }
    this.changed();
  }

  /* ---- Board events ---- */

  applyEvent(ev) {
    const s = this.state;
    switch (ev.event) {
      case 'ble_ready':
        s.bonds = ev.bonds;
        s.bondsMax = ev.max;
        break;
      case 'scan_started':
        s.found.clear();   /* A scan lists what is advertising now (the board starts its list over too) */
        s.scanning = true;
        s.scanPending = false;
        s.radio = 'scan';
        s.scanUntil = Date.now() + 60000;
        break;
      case 'scan_stopped':
        s.scanning = false;
        s.scanPending = false;
        if (s.radio === 'scan') s.radio = 'idle';
        break;
      case 'device_found':
        if (!this.deviceByAddr(ev.addr)) s.found.set(ev.addr, { ...ev, state: 'found', hid: true });
        break;
      case 'connecting': {
        const f = s.found.get(ev.addr);
        if (f) f.state = 'connecting';
        const d = this.deviceByAddr(ev.addr);
        if (d) d.state = 'connecting';
        s.radio = 'connect';
        break;
      }
      case 'connect_failed': {
        const f = s.found.get(ev.addr);
        if (f) f.state = 'found';
        const d = this.deviceByAddr(ev.addr);
        if (d && d.paired) d.state = 'offline';
        break;
      }
      case 'connected': {
        const f = s.found.get(ev.addr);
        s.found.delete(ev.addr);
        const key = ev.id_addr || ev.addr;
        const d = this.#device(key, { name: f?.name || '', appearance: f?.appearance || 0 });
        if (f) d.foundEntry = f;   /* if the new pairing fails, it goes back to the found list */
        d.addr = ev.addr;
        d.paired = d.paired || ev.bonded;
        d.state = ev.bonded ? 'securing' : 'pairing';
        d.user = ev.user;
        d.error = '';
        break;
      }
      case 'encrypted': {
        const d = this.deviceByAddr(ev.addr);
        if (d) {
          d.bond = ev.bond;
          d.authenticated = ev.authenticated;
          d.state = ev.ok ? 'discovering' : 'failed';
        }
        if (s.prompt && s.prompt.addr === ev.addr) s.prompt = null;
        break;
      }
      case 'paired': {
        let d = this.deviceByAddr(ev.addr);
        if (d && d.id !== ev.id_addr) {
          /* A new device was keyed by its over-the-air address on connect. Move it to the identity address */
          s.devices.delete(d.id);
          d.id = ev.id_addr;
          s.devices.set(d.id, d);
        }
        d = d || this.#device(ev.id_addr);
        d.paired = true;
        d.name = ev.name || d.name;
        break;
      }
      case 'hid_ready': {
        const d = this.deviceByAddr(ev.addr);
        if (d) {
          d.state = 'ready';
          d.kinds = ev.kinds;
        }
        break;
      }
      case 'device_name': {
        const d = this.deviceByAddr(ev.addr);
        if (d) d.name = ev.name;
        break;
      }
      case 'failed': {
        const d = this.deviceByAddr(ev.addr);
        if (d) {
          d.state = 'failed';
          d.error = ev.stage;
        }
        break;
      }
      case 'disconnected': {
        const d = this.deviceByAddr(ev.addr);
        if (d) {
          if (d.paired) {
            d.state = 'offline';
            d.lastReason = ev.reason;
          } else {
            s.devices.delete(d.id);
            /* A device that did not finish pairing goes back to the found list so it can be retried (the board lists it again too) */
            if (d.foundEntry) s.found.set(d.addr, { ...d.foundEntry, state: 'found' });
          }
        }
        if (s.prompt && s.prompt.addr === ev.addr) s.prompt = null;
        break;
      }
      case 'autoconnect': {
        /* The auto-connect targets were recomputed: a device the user turned back on gets auto = true again (confirmed by paired_list) */
        s.radio = 'auto';
        break;
      }
      case 'unpaired':
        s.devices.delete(ev.id_addr);
        break;
      case 'unpaired_all':
        for (const [k, d] of s.devices) if (d.paired) s.devices.delete(k);
        break;
      case 'numcmp':
      case 'passkey_request':
      case 'passkey_display':
        s.prompt = { type: ev.event, addr: ev.addr, code: ev.code || '' };
        break;
      case 'host_switch':
        s.activeHost = ev.host;
        break;
      case 'host_window':
        s.hostWindow = { open: ev.open, until: ev.open ? Date.now() + ev.seconds * 1000 : 0 };
        if (!ev.open) {
          s.hostPrompt = null;
          s.hostCandidate = null;
        }
        break;
      case 'host_connected':
        if (ev.host >= 1) {
          const h = s.hosts.get(ev.host);
          if (h) {
            h.connected = true;
            h.state = 'connected';
          }
        } else {
          s.hostCandidate = { addr: ev.addr, state: 'pairing' };
        }
        break;
      case 'host_passkey_request':
        s.hostPrompt = { addr: ev.addr, len: 0 };
        break;
      case 'host_passkey_progress':
        if (s.hostPrompt) s.hostPrompt.len = ev.len;
        break;
      case 'host_passkey_sent':
        s.hostPrompt = null;
        if (s.hostCandidate) s.hostCandidate.state = 'securing';
        break;
      case 'host_paired':
        s.hostCandidate = null;
        s.hosts.set(ev.host, { host: ev.host, addr: ev.addr, name: '', connected: true, state: 'secured' });
        break;
      case 'host_ready': {
        const h = s.hosts.get(ev.host);
        if (h) h.state = 'ready';
        break;
      }
      case 'host_name': {
        const h = s.hosts.get(ev.host);
        if (h) h.name = ev.name;
        break;
      }
      case 'host_pair_failed':
        s.hostPrompt = null;
        break;
      case 'host_disconnected':
        if (ev.host >= 1) {
          const h = s.hosts.get(ev.host);
          if (h) {
            h.connected = false;
            h.state = 'offline';
          }
        } else if (s.hostCandidate?.addr === ev.addr) {
          s.hostCandidate = null;
        }
        if (s.hostPrompt?.addr === ev.addr) s.hostPrompt = null;
        break;
      case 'host_removed':
        s.hosts.delete(ev.host);
        break;
      case 'ble_reset':
        for (const d of s.devices.values()) d.state = 'offline';
        break;
      default:
        return;
    }
    this.changed();
  }
}
