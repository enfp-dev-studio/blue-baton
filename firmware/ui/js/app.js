/*
 * Blue Baton settings UI.
 * The transport is picked from the URL: USB (Web Serial) by default, the mock board with ?mock.
 * The UI code does not know which transport it is talking through.
 */

import { mount } from './dom.js';
import { FAIL_STAGE, errorText, localizePage, t } from './i18n.js';
import { InputMonitor } from './inputs.js';
import { BoardClient } from './protocol.js';
import { Store } from './store.js';
import { BridgeTransport } from './transport/bridge.js';
import { MockTransport } from './transport/mock.js';
import { WebSerialTransport } from './transport/webserial.js';
import { renderAdd, renderDevices, renderHosts, renderLink, renderLock, renderLog, renderNotice, secondsLeft } from './views.js';

localizePage();

/* Transport: ?mock mock board, ?bridge local bridge (tools/bridge.py), otherwise USB (Web Serial) */
const params = new URLSearchParams(location.search);
const useMock = params.has('mock');
const useBridge = params.has('bridge');
const transport = useMock ? new MockTransport(params) : useBridge ? new BridgeTransport() : new WebSerialTransport();
const needsSerial = !useMock && !useBridge;
const client = new BoardClient(transport);
const store = new Store();
const $ = (id) => document.getElementById(id);
const monitor = new InputMonitor($('inputs'));
const logOpts = { boardLines: false };

store.state.transport = transport.label;
store.state.addOpen = false;

/* ---- From the board ---- */

const REFRESH_ON = new Set(['ble_ready', 'paired', 'hid_ready', 'disconnected', 'unpaired', 'unpaired_all',
  'autoconnect', 'autoconnect_paused', 'device_name', 'failed', 'ble_reset',
  'host_paired', 'host_ready', 'host_disconnected', 'host_removed', 'host_name']);
let refreshTimer = null;

client.on((ev) => {
  switch (ev.event) {
    case '_closed':
      store.set({ link: 'disconnected', unlocked: false, prompt: null });
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
      livePulse(ev.addr);
      return;
    default:
      break;
  }
  store.log('<', JSON.stringify(ev).slice(0, 300));
  monitor.onEvent(ev);
  store.applyEvent(ev);
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
    store.notice('error', t('notice.hostRepairBlocked', { host: ev.host }));
  } else if (ev.event === 'host_online' && !ev.online) {
    store.notice('info', t('notice.hostOffline', { host: ev.host }));
  }
  if (REFRESH_ON.has(ev.event)) {
    clearTimeout(refreshTimer);
    refreshTimer = setTimeout(refresh, 150);
  }
});

/*
 * Live input: the sending device and the host receiving it light up briefly. Reports arrive dozens of times
 * a second, so this touches classes directly instead of redrawing a region.
 */
const pulseTimers = new WeakMap();

function pulse(el) {
  if (!el) return;
  el.classList.add('live');
  clearTimeout(pulseTimers.get(el));
  pulseTimers.set(el, setTimeout(() => el.classList.remove('live'), 160));
}

function livePulse(addr) {
  pulse(document.querySelector(`.device[data-addr="${CSS.escape(addr)}"]`));
  const n = store.state.activeHost;
  if (store.state.hosts.get(n)?.state === 'ready') pulse(document.querySelector(`.host[data-host="${n}"]`));
}

async function refresh() {
  if (store.state.link !== 'connected' || !store.state.unlocked) return;
  try {
    const [st, pl, hl] = await Promise.all([
      client.request('status'),
      client.request('paired_list'),
      client.request('host_list').catch(() => null),   /* not in firmware before 0.4 */
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
  store.set({ fw: hello.version, pinSet: hello.pin_set, unlocked: hello.unlocked });
  if (hello.unlocked) refresh();
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
    }
  },
  async disconnectLink() {
    await transport.close();
    store.notice('info', t('notice.released'));
  },
  async unlock(pin) {
    const ok = await run(() => client.request('auth', { pin }));
    if (ok) {
      store.set({ unlocked: true, notice: null });
      refresh();
    }
  },
  async setPin(pin) {
    const ok = await run(() => client.request('pin_set', { pin }));
    if (ok) {
      store.set({ unlocked: true, pinSet: true, notice: null });
      refresh();
    }
  },
  async lock() {
    await run(() => client.request('lock'));
    store.set({ unlocked: false, addOpen: false });
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
  async hostPasskey(code) {
    await run(() => client.request('host_passkey', { code }));
  },
  async removeHost(n, hostDev) {
    const name = hostDev.name || t('confirm.hostName', { n });
    if (!confirm(t('confirm.removeHost', { name }))) return;
    await run(() => client.request('host_remove', { host: n }));
  },
  async disconnectDevice(addr) {
    await run(() => client.request('disconnect', { addr }));
  },
  async removeDevice(d) {
    const name = d.name || d.id;
    if (!confirm(t('confirm.removeDevice', { name }))) return;
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

/* ---- Rendering ---- */

/*
 * Each region is redrawn only when the state it depends on changes. If one log line rebuilt the PIN field,
 * focus, IME composition (e.g. Korean input), and password manager autofill would be interrupted.
 */
const lastKey = new Map();

function region(id, key, build) {
  const el = $(id);
  /* Still draw the rest of the page if the HTML and JS disagree (e.g. an old cached HTML) */
  if (!el || lastKey.get(id) === key) return;
  lastKey.set(id, key);
  mount(el, build());
}

function devicesKey(s) {
  return JSON.stringify([s.bonds, s.bondsMax, s.addOpen, s.hostWindow.open, [...s.devices.values()]]);
}

function addKey(s) {
  return JSON.stringify([s.addOpen, s.scanning, s.scanUntil, s.scanPending, s.showAll, s.prompt, [...s.found.values()]]);
}

function render(s) {
  region('link', JSON.stringify([s.link, s.transport, s.fw, s.unlocked, s.radio]), () => renderLink(s, actions));
  region('notice', JSON.stringify(s.notice), () => renderNotice(s, actions));
  region('lock', JSON.stringify([s.link, s.unlocked, s.pinSet]), () => renderLock(s, actions));
  const ready = s.link === 'connected' && s.unlocked;
  $('main-sections').hidden = !ready;
  if (ready) {
    region('hosts', JSON.stringify([s.activeHost, s.combos, [...s.hosts.values()], s.hostWindow, s.hostCandidate, s.hostPrompt]),
      () => renderHosts(s, actions));
    region('devices', devicesKey(s), () => renderDevices(s, actions));
    region('add', addKey(s), () => renderAdd(s, actions));
  }
  if ($('log-sec').open) {
    region('log', `${s.log.length}|${s.log.at(-1)?.at?.getTime()}|${logOpts.boardLines}`, () => renderLog(s, logOpts));
  }
}

store.subscribe(render);
render(store.state);

$('log-board').addEventListener('change', (e) => {
  logOpts.boardLines = e.target.checked;
  store.changed();
});
$('log-sec').addEventListener('toggle', () => store.changed());

/* Remember whether Diagnostics is open (per browser; storage can be unavailable, e.g. private windows) */
try {
  if (localStorage.getItem('bb.diagOpen') === '1') $('diag-sec').open = true;
} catch { /* ignore */ }
$('diag-sec').addEventListener('toggle', () => {
  try {
    localStorage.setItem('bb.diagOpen', $('diag-sec').open ? '1' : '0');
  } catch { /* ignore */ }
});

/* Countdowns update only the number. Values without events (such as the radio state) are resynced every 5 s */
setInterval(() => {
  for (const el of document.querySelectorAll('[data-until]')) {
    el.textContent = String(secondsLeft(Number(el.dataset.until)));
  }
}, 1000);
setInterval(refresh, 5000);

/*
 * Reconnect after a drop: the bridge grabs the port again after flashing, so retry after a short delay.
 * For USB, a previously allowed port that is plugged back in opens without the picker.
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

/* On first load: the mock board and bridge connect right away; USB connects without the picker if a port was allowed before */
(async () => {
  if (useMock || useBridge) {
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
