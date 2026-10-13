// SPDX-License-Identifier: GPL-3.0-only
/*
 * Blue Baton settings UI: the controller. It owns the board link, the store, and every action; the React
 * views (src/components) only read the store and call actions.
 *
 * Transport: inside the menu bar app (Tauri) the app's own serial link; otherwise from the URL:
 * ?mock mock board, ?bridge local bridge (tools/bridge.py), anything else USB through Web Serial.
 */

import { FAIL_STAGE, errorText, t } from './i18n.js';
import { InputMonitor } from './inputs.js';
import { BoardClient } from './protocol.js';
import { Store } from './store.js';
import { BridgeTransport } from './transport/bridge.js';
import { MockTransport } from './transport/mock.js';
import { TauriTransport } from './transport/tauri.js';
import { WebSerialTransport } from './transport/webserial.js';

const REFRESH_ON = new Set(['ble_ready', 'paired', 'hid_ready', 'disconnected', 'unpaired', 'unpaired_all',
  'autoconnect', 'device_name', 'failed', 'ble_reset',
  'host_paired', 'host_ready', 'host_disconnected', 'host_removed', 'host_name', 'host_moved', 'host_nick']);

/* Events that need the user while the window may be hidden (menu bar app): bring the window forward */
const NEEDS_USER = new Set(['host_passkey_request', 'passkey_request', 'numcmp', 'passkey_display']);

/*
 * role: 'main' is the settings window (it also drives the tray menu and brings itself forward for pairing
 * prompts); 'panel' is the small panel under the menu bar icon, a second client of the same board.
 */
export function createController(params = new URLSearchParams(location.search), { role = 'main' } = {}) {
  const inApp = TauriTransport.available();
  const useMock = !inApp && params.has('mock');
  const useBridge = !inApp && params.has('bridge');
  const transport = inApp ? new TauriTransport() : useMock ? new MockTransport(params) : useBridge ? new BridgeTransport() : new WebSerialTransport();
  const needsSerial = !inApp && !useMock && !useBridge;
  const client = new BoardClient(transport);
  const store = new Store();
  /* The monitor keeps decoding even while Diagnostics is closed (it needs the Report Maps sent at connect time) */
  const monitorRoot = document.createElement('div');
  const monitor = new InputMonitor(monitorRoot);
  const app = inApp ? window.__TAURI__ : null;
  const isMain = role === 'main';

  store.state.transport = transport.label;
  store.state.addOpen = false;

  /* ---- From the board ---- */

  let refreshTimer = null;

  client.on((ev) => {
    switch (ev.event) {
      case '_closed':
        store.set({ link: 'disconnected', prompt: null });
        if (ev.reason !== 'closed') {
          store.notice('error', ev.reason === 'unplugged' ? t('notice.unplugged') : t('notice.lost'));
          reconnectSoon();
        }
        return;
      case '_log':
        store.log('·', ev.line);
        return;
      case '_sent':
        store.log('>', ev.line);
        return;
      case '_reply':
        store.log('<', JSON.stringify(ev.msg));
        return;
      case '_bad_line':
        store.log('!', ev.line);
        return;
      case 'report':
        monitor.onEvent(ev);   /* too many to log */
        return;
      case 'activity':
        livePulse(ev.addr);
        return;
      default:
        break;
    }
    store.log('<', JSON.stringify(ev).slice(0, 300));
    monitor.onEvent(ev);
    store.applyEvent(ev);
    if (app && isMain && NEEDS_USER.has(ev.event)) {
      app.core.invoke('window_show');
      app.core.invoke('notify', { title: t('app.name'), body: t(ev.event === 'host_passkey_request' ? 'hosts.passkeyHelp' : 'add.title') });
    }
    if (ev.event === 'bond_full') {
      store.notice('error', t('notice.bondFull', { bonds: ev.bonds, max: ev.max }));
    } else if (ev.event === 'failed') {
      const d = store.deviceByAddr(ev.addr) || store.state.found.get(ev.addr);
      const why = ev.stage === 'security' && !d?.paired
        ? t('notice.pairFailed')
        : FAIL_STAGE[ev.stage] || t('notice.failed');
      store.notice('error', `${d?.name || ev.addr}: ${why}`);
    } else if (ev.event === 'connect_failed') {
      /* cancelled: the user switched to something else (a scan, for example). Nothing to report */
      if (ev.status === 'no_slot') store.notice('error', errorText({ code: 'no_slot' }));
      else if (ev.status !== 'cancelled') store.notice('error', t('notice.connectFailed'));
    } else if (ev.event === 'paired' && ev.bond === 'new') {
      store.notice('info', t('notice.paired', { name: ev.name || t('notice.newDevice') }));
    } else if (ev.event === 'host_paired') {
      store.notice('info', t('notice.hostPaired', { host: ev.host }));
    } else if (ev.event === 'host_pair_failed') {
      store.notice('error', t('notice.hostPairFailed'));
    } else if (ev.event === 'host_repair_blocked') {
      store.notice('error', t('notice.hostRepairBlocked', { host: hostLabel(store.state, ev.host) }));
    } else if (ev.event === 'host_online' && !ev.online) {
      store.notice('info', t('notice.hostOffline', { host: hostLabel(store.state, ev.host) }));
    }
    if (REFRESH_ON.has(ev.event)) {
      clearTimeout(refreshTimer);
      refreshTimer = setTimeout(refresh, 150);
    }
  });

  /*
   * Live input: the sending device and the host receiving it light up while input arrives. The board sends
   * activity at most every 250 ms per device, so the light outlasts that gap. It sets a data attribute directly
   * instead of re-rendering (React does not own data-live).
   */
  const pulseTimers = new WeakMap();

  function pulse(el) {
    if (!el) return;
    el.setAttribute('data-live', '');
    clearTimeout(pulseTimers.get(el));
    pulseTimers.set(el, setTimeout(() => el.removeAttribute('data-live'), 400));
  }

  function livePulse(addr) {
    pulse(document.querySelector(`[data-device="${CSS.escape(addr)}"]`));
    const n = store.state.activeHost;
    if (store.state.hosts.get(n)?.state === 'ready') pulse(document.querySelector(`[data-host="${n}"]`));
  }

  async function refresh() {
    if (store.state.link !== 'connected') return;
    try {
      const [st, pl, hl] = await Promise.all([
        client.request('status'),
        client.request('paired_list'),
        client.request('host_list').catch(() => null),   /* not in firmware before 0.5 */
      ]);
      store.applyPaired(pl.paired || []);
      store.applyStatus(st);
      if (hl) store.applyHosts(hl.hosts || []);
      if (!store.state.combos.length) await loadCombos();
    } catch (e) {
      if (e.code !== 'closed') console.warn(e);
    }
  }

  async function loadCombos() {
    const r = await client.request('combo_get');
    store.set({ combos: (r.combo || '').split(',').map((x) => x.trim()), combosDefault: r.default });
  }

  /* ---- Actions ---- */

  async function run(fn, { quiet = false } = {}) {
    try {
      return await fn();
    } catch (e) {
      if (!quiet) store.notice('error', errorText(e));
      return null;
    }
  }

  async function afterOpen() {
    store.set({ link: 'connected', linkError: '' });
    const hello = await run(() => client.request('hello'));
    if (!hello) return;
    store.set({ fw: hello.version });
    refresh();
    if (monitorWanted) renewMonitor();
  }

  /*
   * Input monitor: the board sends what was typed only under a lease (PROTOCOL.md "Input monitor").
   * Renewed while the monitor is on screen; if this window goes away the lease ends by itself.
   */
  let monitorWanted = false;
  let monitorTimer = null;

  async function renewMonitor() {
    clearTimeout(monitorTimer);
    if (!monitorWanted || store.state.link !== 'connected') return;
    const r = await run(() => client.request('monitor', { on: true }), { quiet: true });
    if (monitorWanted) monitorTimer = setTimeout(renewMonitor, (r?.lease_ms || 60000) / 3);
  }

  const actions = {
    async connect() {
      if (needsSerial && !WebSerialTransport.supported()) {
        store.notice('error', errorText({ code: 'no_serial' }));
        return;
      }
      store.set({ link: 'connecting' });
      try {
        await transport.open({ prompt: true });
        await afterOpen();
      } catch (e) {
        store.set({ link: 'disconnected' });
        store.notice('error', errorText(e));
        if (inApp || useBridge) reconnectSoon();
      }
    },
    /* Write the app's firmware to the board: update (keeps pairings) or install (clears the board) */
    async writeFirmware(keepPairings) {
      store.set({ fwJob: { stage: 'connecting', percent: 0 } });
      if (app) {
        try {
          await app.core.invoke('firmware_write', { keepPairings });
        } catch (e) {
          store.set({ fwJob: { stage: 'failed', percent: 0, error: String(e) } });
        }
        return;
      }
      for (let p = 0; p <= 100; p += 20) {
        await new Promise((r) => setTimeout(r, 200));
        store.set({ fwJob: { stage: 'writing', percent: p } });
      }
      store.set({ fwJob: null, appBoard: 'open' });
      store.notice('info', t('fw.done'));
      if (store.state.link === 'disconnected') await actions.connect();
      else store.set({ fw: store.state.fwBundled });
    },
    dismissFirmware() {
      store.set({ fwJob: null });
    },
    async disconnectLink() {
      await transport.close();
      store.notice('info', t('notice.released'));
    },
    monitor(on) {
      if (on === monitorWanted) return;
      monitorWanted = on;
      if (on) {
        renewMonitor();
      } else {
        clearTimeout(monitorTimer);
        if (store.state.link === 'connected') run(() => client.request('monitor', { on: false }), { quiet: true });
      }
    },
    dismiss() {
      store.set({ notice: null });
    },
    openAdd() {
      store.set({ addOpen: true });
      actions.scan(true);
    },
    closeAdd() {
      store.set({ addOpen: false, prompt: null });
      if (store.state.scanning) actions.scan(false);
    },
    async scan(on) {
      /* Keep the button at "Starting…" until the board sends scan_started (so the click visibly registered) */
      if (on) store.set({ scanPending: true });
      const ok = await run(() => client.request('pair_scan', { on }));
      if (!ok) store.set({ scanPending: false });
    },
    async connectDevice(addr) {
      const d = store.deviceByAddr(addr);
      await run(() => client.request('pair_connect', { addr }));
      if (d?.paired) store.notice('info', t('notice.autoOn', { name: d.name || addr }));
    },
    async showAll(all) {
      store.set({ showAll: all });
      const r = await run(() => client.request('pair_list', { all }));
      if (!r) return;
      for (const d of r.devices || []) {
        if (!store.deviceByAddr(d.addr)) store.state.found.set(d.addr, { ...store.state.found.get(d.addr), ...d, state: 'found' });
      }
      store.changed();
    },
    async selectHost(n) {
      await run(() => client.request('host_select', { host: n }));
    },
    async openHostWindow() {
      if (store.state.addOpen) actions.closeAdd();
      await run(() => client.request('host_pair', { on: true }));
    },
    async closeHostWindow() {
      await run(() => client.request('host_pair', { on: false }));
    },
    /* One combo per host, in host order ('' leaves that host without one). The board validates and saves */
    async setCombos(list) {
      const ok = await run(() => client.request('combo_set', { combo: list.join(',') }));
      if (ok) await run(loadCombos);
      return !!ok;
    },
    async hostPasskey(code) {
      await run(() => client.request('host_passkey', { code }));
    },
    /* The views ask for confirmation first (dialog) */
    async removeHost(n) {
      await run(() => client.request('host_remove', { host: n }));
    },
    /* Host n takes number to; the host there takes n. The links stay up */
    async moveHost(n, to) {
      await run(() => client.request('host_move', { host: n, to }));
    },
    /* '' shows the number again */
    async renameHost(n, nick) {
      return !!(await run(() => client.request('host_nick', { host: n, nick })));
    },
    async disconnectDevice(addr) {
      await run(() => client.request('disconnect', { addr }));
    },
    async removeDevice(d) {
      await run(() => client.request('paired_remove', { addr: d.id }));
    },
    async accept(yes) {
      await run(() => client.request('pair_accept', { accept: yes }));
      store.set({ prompt: null });
    },
    async passkey(code) {
      await run(() => client.request('passkey', { code }));
      store.set({ prompt: null });
    },
  };

  /* Values without events (such as the radio state) are resynced every 5 s, only while the window is shown */
  const shown = () => typeof document === 'undefined' || document.visibilityState === 'visible';
  setInterval(() => shown() && refresh(), 5000);
  if (typeof document !== 'undefined') document.addEventListener('visibilitychange', () => shown() && refresh());

  /*
   * Reconnect after a drop: the bridge and the app grab the port again after flashing, so retry after a
   * short delay. For USB, a previously allowed port that is plugged back in opens without the picker.
   */
  let reconnectTimer = null;
  function reconnectSoon() {
    clearTimeout(reconnectTimer);
    reconnectTimer = setTimeout(async () => {
      if (store.state.link !== 'disconnected') return;
      try {
        await transport.open({ prompt: false });
        await afterOpen();
        store.notice('info', t('notice.reconnected'));
      } catch {
        reconnectSoon();
      }
    }, 2000);
  }
  if (needsSerial && WebSerialTransport.supported()) {
    navigator.serial.addEventListener('connect', () => reconnectSoon());
  }

  /* ---- Menu bar app: keep the tray menu in step with the store, and take its clicks ---- */

  if (app && isMain) {
    let last = '';
    store.subscribe((s) => {
      const model = trayModel(s);
      const key = JSON.stringify(model);
      if (key === last) return;
      last = key;
      app.core.invoke('tray_update', { model }).catch(() => {});
    });
    app.event.listen('tray-action', (e) => {
      const a = e.payload || {};
      if (a.kind === 'select_host') actions.selectHost(a.host);
      else if (a.kind === 'add_host') actions.openHostWindow();
    });
  }

  /* ---- Firmware that comes with the app (app/src-tauri/src/firmware.rs). The mock board fakes it (?mock&fw=0.8) ---- */

  if (app && isMain) {
    app.core.invoke('firmware_bundled').then((v) => store.set({ fwBundled: v || null })).catch(() => {});
    app.core.invoke('board_status').then((st) => store.set({ appBoard: st.state })).catch(() => {});
    app.event.listen('board-state', (e) => store.set({ appBoard: e.payload?.state || '' }));
    app.event.listen('firmware-progress', (e) => {
      const job = e.payload || {};
      store.set({ fwJob: job.stage === 'done' ? null : job });
      if (job.stage === 'done') store.notice('info', t('fw.done'));
    });
  } else if (useMock && params.has('fw')) {
    store.set({ fwBundled: params.get('fw'), appBoard: params.has('blank') ? 'other' : 'open' });
  }

  /* First load: the app, mock board, and bridge connect right away; USB connects without the picker if a port was allowed before */
  (async () => {
    if (useMock && store.state.appBoard === 'other') return; /* waits for the firmware to be installed */
    if (inApp || useMock || useBridge) {
      await actions.connect();
      return;
    }
    if (!WebSerialTransport.supported()) {
      store.notice('error', errorText({ code: 'no_serial' }));
      return;
    }
    try {
      store.set({ link: 'connecting' });
      await transport.open({ prompt: false });
      await afterOpen();
    } catch (e) {
      store.set({ link: 'disconnected' });
      if (e.message !== 'no_known_port') store.notice('error', errorText(e));
    }
  })();

  return { store, actions, monitorRoot, inApp, needsSerial, useMock, role };
}

/* What a host is called on screen: the name the user gave it, otherwise its number */
export function hostLabel(s, n) {
  return s.hosts.get(n)?.nick || String(n);
}

/* What the tray menu shows. Labels come from the UI language so the menu matches the window */
function trayModel(s) {
  const board = s.link === 'connected' ? 'connected' : 'disconnected';
  const hosts = [1, 2, 3].map((n) => {
    const h = s.hosts.get(n);
    return { n, label: hostLabel(s, n), name: h ? h.name || t('hosts.unnamed') : '', state: h ? h.state : 'empty' };
  });
  const inputs = [...s.devices.values()]
    .filter((d) => d.state !== 'offline')
    .map((d) => ({ name: d.name || t('common.unnamedDevice'), state: t(`state.${d.state}`), ready: d.state === 'ready' }));
  const active = s.hosts.get(s.activeHost);
  return {
    active: s.activeHost,
    hosts,
    inputs,
    board,
    tooltip: active ? t('hosts.sendingTo', { name: active.name || t('hosts.unnamed') }) : t('app.name'),
    labels: {
      open: t('tray.open'),
      add_host: t('tray.addHost'),
      autostart: t('tray.autostart'),
      dev_api: t('tray.devApi'),
      quit: t('tray.quit'),
      empty: t('hosts.empty'),
      offline: t('hostState.offline'),
      inputs: t('devices.title'),
      no_inputs: t('tray.noInputs'),
      board_off: t('link.disconnected'),
      check_updates: t('tray.checkUpdates'),
      install_update: t('tray.installUpdate'),
      update_ready: t('tray.updateReady'),
      up_to_date: t('tray.upToDate'),
      update_failed: t('tray.updateFailed'),
    },
  };
}
