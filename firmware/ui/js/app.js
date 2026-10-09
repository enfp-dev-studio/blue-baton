/*
 * KM 라우터 설정 화면.
 * 통신 방식은 주소로 고른다: 기본은 USB(Web Serial), ?mock이면 모의 보드.
 * 화면 코드는 통신 방식을 모른다 (GUI 제안서 5.1).
 */

import { mount } from './dom.js';
import { FAIL_STAGE, errorText } from './i18n.js';
import { InputMonitor } from './inputs.js';
import { BoardClient } from './protocol.js';
import { Store } from './store.js';
import { BridgeTransport } from './transport/bridge.js';
import { MockTransport } from './transport/mock.js';
import { WebSerialTransport } from './transport/webserial.js';
import { renderAdd, renderDevices, renderHosts, renderLink, renderLock, renderLog, renderNotice, secondsLeft } from './views.js';

/* 통신 방식: ?mock 모의 보드, ?bridge 로컬 브리지(tools/bridge.py), 그 밖에는 USB(Web Serial) */
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

/* ---- 보드에서 오는 것 ---- */

const REFRESH_ON = new Set(['ble_ready', 'paired', 'hid_ready', 'disconnected', 'unpaired', 'unpaired_all',
  'autoconnect', 'autoconnect_paused', 'device_name', 'failed', 'ble_reset',
  'host_paired', 'host_ready', 'host_disconnected', 'host_removed', 'host_name']);
let refreshTimer = null;

client.on((ev) => {
  switch (ev.event) {
    case '_closed':
      store.set({ link: 'disconnected', unlocked: false, prompt: null });
      if (ev.reason !== 'closed') {
        store.notice('error', ev.reason === 'unplugged' ? '보드 USB가 뽑혔어요.' : '보드 연결이 끊겼어요. 다시 연결을 시도해요…');
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
      monitor.onEvent(ev);   /* 너무 많아서 로그에는 남기지 않는다 */
      return;
    default:
      break;
  }
  store.log('<', JSON.stringify(ev).slice(0, 300));
  monitor.onEvent(ev);
  store.applyEvent(ev);
  if (ev.event === 'bond_full') {
    store.notice('error', `페어링 저장 공간이 가득 찼어요 (${ev.bonds}/${ev.max}). 안 쓰는 기기를 삭제한 뒤 다시 시도하세요.`);
  } else if (ev.event === 'failed') {
    const d = store.deviceByAddr(ev.addr) || store.state.found.get(ev.addr);
    const why = ev.stage === 'security' && !d?.paired
      ? '페어링에 실패했어요. 번호가 틀렸거나 기기가 거절했어요. 다시 시도할 수 있어요.'
      : FAIL_STAGE[ev.stage] || '연결에 실패했어요.';
    store.notice('error', `${d?.name || ev.addr}: ${why}`);
  } else if (ev.event === 'connect_failed') {
    /* cancelled: 사용자가 다른 동작(스캔 등)으로 바꿨다. 알릴 필요가 없다 */
    if (ev.status === 'no_slot') store.notice('error', errorText({ code: 'no_slot' }));
    else if (ev.status !== 'cancelled') store.notice('error', '연결하지 못했어요. 기기가 페어링 모드인지, 가까이 있는지 확인하세요.');
  } else if (ev.event === 'paired' && ev.bond === 'new') {
    store.notice('info', `${ev.name || '새 기기'}를 페어링했어요. 다음부터는 자동으로 연결돼요.`);
  } else if (ev.event === 'host_paired') {
    store.notice('info', `호스트 ${ev.host}를 추가했어요. 다음부터는 컴퓨터가 켜지면 자동으로 연결돼요.`);
  } else if (ev.event === 'host_pair_failed') {
    store.notice('error', '호스트 페어링에 실패했어요. 숫자가 틀렸거나 시간이 지났어요. 컴퓨터에서 다시 연결을 누르세요.');
  } else if (ev.event === 'host_repair_blocked') {
    store.notice('error', `호스트 ${ev.host}가 다시 페어링하려 해요. 그 컴퓨터에서 보드를 지웠다면, 여기서 호스트 추가 창을 연 뒤 다시 연결하세요.`);
  } else if (ev.event === 'host_online' && !ev.online) {
    store.notice('info', `호스트 ${ev.host}가 연결되어 있지 않아요. 연결될 때까지 입력은 보내지 않아요.`);
  }
  if (REFRESH_ON.has(ev.event)) {
    clearTimeout(refreshTimer);
    refreshTimer = setTimeout(refresh, 150);
  }
});

async function refresh() {
  if (store.state.link !== 'connected' || !store.state.unlocked) return;
  try {
    const [st, pl, hl] = await Promise.all([
      client.request('status'),
      client.request('paired_list'),
      client.request('host_list').catch(() => null),   /* 0.4 이전 펌웨어에는 없다 */
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

/* ---- 동작 ---- */

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
    store.notice('info', 'USB 포트를 놓았어요. 다른 프로그램(플래시, 모니터)이 보드를 쓸 수 있어요.');
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
    /* 보드가 scan_started를 보낼 때까지 버튼을 "시작하는 중"으로 둔다 (누른 게 먹혔는지 보이게) */
    if (on) store.set({ scanPending: true });
    const ok = await run(() => client.request('pair_scan', { on }));
    if (!ok) store.set({ scanPending: false });
  },
  async connectDevice(addr) {
    const d = store.deviceByAddr(addr);
    await run(() => client.request('pair_connect', { addr }));
    if (d?.paired) store.notice('info', `${d.name || addr}: 자동 연결을 켰어요. 기기를 움직이거나 키를 누르면 연결돼요.`);
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
    const name = hostDev.name || `호스트 ${n}`;
    if (!confirm(`${name}를 삭제할까요?\n그 컴퓨터의 Bluetooth 설정에서도 "KM Router"를 지워야 다시 추가할 수 있어요.`)) return;
    await run(() => client.request('host_remove', { host: n }));
  },
  async disconnectDevice(addr) {
    await run(() => client.request('disconnect', { addr }));
  },
  async removeDevice(d) {
    const name = d.name || d.id;
    if (!confirm(`${name}의 페어링을 삭제할까요?\n다시 쓰려면 기기를 페어링 모드로 켜고 새로 추가해야 해요.`)) return;
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

/* ---- 그리기 ---- */

/*
 * 영역마다 관련 상태가 바뀔 때만 다시 그린다. 로그 한 줄 때문에 PIN 칸이 다시 만들어지면
 * 포커스, 한글 조합, 비밀번호 관리자 입력이 끊긴다.
 */
const lastKey = new Map();

function region(id, key, build) {
  const el = $(id);
  /* HTML과 JS가 어긋난 경우(예전 HTML이 캐시됨)에도 나머지 화면은 그린다 */
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

/* 남은 초는 숫자만 고친다. 라디오 상태처럼 이벤트가 없는 값은 5초마다 맞춘다 */
setInterval(() => {
  for (const el of document.querySelectorAll('[data-until]')) {
    el.textContent = String(secondsLeft(Number(el.dataset.until)));
  }
}, 1000);
setInterval(refresh, 5000);

/*
 * 끊긴 뒤 다시 붙기: 브리지는 플래시 뒤 포트를 다시 잡으므로 잠시 뒤 다시 시도한다.
 * USB는 이미 허용한 포트가 다시 꽂히면 선택 창 없이 연다.
 */
let reconnectTimer = null;
function reconnectSoon() {
  clearTimeout(reconnectTimer);
  reconnectTimer = setTimeout(async () => {
    if (store.state.link !== 'disconnected') return;
    try {
      await transport.open({ prompt: false });
      await afterOpen();
      store.notice('info', '보드에 다시 연결했어요.');
    } catch {
      reconnectSoon();
    }
  }, 2000);
}
if (needsSerial && WebSerialTransport.supported()) {
  navigator.serial.addEventListener('connect', () => reconnectSoon());
}

/* 처음 열 때: 모의 보드와 브리지는 바로, USB는 이미 허용한 포트가 있으면 선택 창 없이 연결한다 */
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
