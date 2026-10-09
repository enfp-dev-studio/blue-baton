/* UI pieces. Each takes the state (store.state) and builds DOM. Actions are requested only through `actions` */

import { h, icon } from './dom.js';
import { FAIL_STAGE, RADIO, hostStateText, hostStateTone, kindIcon, kindText, reasonText, stateText, stateTone, t, tParts } from './i18n.js';

function badge(text, tone) {
  return h('span', { class: `badge ${tone}` }, text);
}

/*
 * Seconds left. Redrawing a whole region every second would recreate its inputs and lose focus and the
 * screen reader position, so only the number is updated (the 1 s timer in app.js reads data-until).
 */
export function secondsLeft(until) {
  return Math.max(0, Math.ceil((until - Date.now()) / 1000));
}

function countdown(until) {
  return h('span', { 'data-until': String(until) }, String(secondsLeft(until)));
}

/* ---- Top bar: board link and lock ---- */

export function renderLink(s, a) {
  if (s.link !== 'connected') {
    return h('div', { class: 'link' },
      h('span', { class: 'link-state' }, s.link === 'connecting' ? t('link.connecting') : t('link.disconnected')),
      h('button', { class: 'primary', onclick: a.connect, disabled: s.link === 'connecting' }, icon('usb', 16), ' ' + t('link.connect')),
    );
  }
  return h('div', { class: 'link' },
    h('span', { class: 'dot ok', title: t('link.connected') }),
    h('span', { class: 'link-state' }, t('link.info', { transport: s.transport, fw: s.fw || '?' })),
    s.unlocked ? badge(RADIO[s.radio] || s.radio, s.radio === 'idle' ? 'idle' : 'busy') : null,
    s.unlocked ? h('button', { class: 'ghost', onclick: a.lock, title: t('link.lockTitle') }, icon('lock', 16), ' ' + t('link.lock')) : null,
    h('button', { class: 'ghost', onclick: a.disconnectLink, title: t('link.releaseTitle') }, t('link.release')),
  );
}

export function renderLock(s, a) {
  if (s.link !== 'connected' || s.unlocked) return h('div');
  const setting = !s.pinSet;
  const submit = (e) => {
    e.preventDefault();
    const pin = e.target.querySelector('input').value;
    setting ? a.setPin(pin) : a.unlock(pin);
  };
  return h('form', { class: 'card lock', onsubmit: submit },
    h('h2', {}, setting ? t('lock.setTitle') : t('lock.lockedTitle')),
    h('p', { class: 'hint' }, setting ? t('lock.setHint') : t('lock.unlockHint')),
    h('div', { class: 'row' },
      h('input', { type: 'password', inputmode: 'numeric', autocomplete: 'off', placeholder: t('lock.pin'), 'data-keep': 'pin', 'aria-label': t('lock.pin'), maxlength: 8 }),
      h('button', { class: 'primary', type: 'submit' }, setting ? t('lock.set') : t('lock.unlock')),
    ),
  );
}

export function renderNotice(s, a) {
  if (!s.notice) return h('div');
  return h('div', { class: `notice ${s.notice.kind}`, role: s.notice.kind === 'error' ? 'alert' : 'status' },
    h('span', {}, s.notice.text),
    h('button', { class: 'ghost small', onclick: a.dismiss, 'aria-label': t('common.close') }, t('common.close')),
  );
}

/* ---- Output hosts (switch, add, remove) ---- */

function hostSlot(s, n, a) {
  const hostDev = s.hosts.get(n);
  const on = s.activeHost === n;
  return h('li', { class: `host${on ? ' on' : ''}` },
    h('button', {
      class: 'host-pick',
      role: 'radio',
      'aria-checked': on ? 'true' : 'false',
      onclick: () => a.selectHost(n),
      title: t('hosts.pickTitle'),
    },
    h('span', { class: 'host-title' },
      h('span', { class: 'host-num' }, String(n)),
      h('span', { class: 'host-name' }, hostDev ? hostDev.name || t('hosts.unnamed') : t('hosts.empty')),
    ),
    hostDev ? badge(hostStateText(hostDev.state), hostStateTone(hostDev.state)) : null,
    h('span', { class: 'host-combo mono' }, s.combos[n - 1] || t('hosts.noCombo'))),
    h('div', { class: 'host-actions' },
      hostDev
        ? h('button', { class: 'ghost danger small', onclick: () => a.removeHost(n, hostDev) }, t('common.remove'))
        : h('button', { class: 'small', onclick: a.openHostWindow, disabled: s.hostWindow.open }, t('hosts.add'))),
  );
}

function hostPasskeyBox(s, a) {
  const p = s.hostPrompt;
  if (!p) return null;
  const submit = (e) => {
    e.preventDefault();
    a.hostPasskey(e.target.querySelector('input').value);
  };
  const dots = Array.from({ length: 6 }, (_, i) => h('span', { class: `pk-dot${i < p.len ? ' on' : ''}` }));
  return h('form', { class: 'prompt', onsubmit: submit },
    h('p', {}, t('hosts.passkeyHelp')),
    h('div', { class: 'pk-dots', 'aria-label': t('hosts.passkeyDigits', { n: p.len }) }, dots),
    h('p', { class: 'hint' }, t('hosts.passkeyFallback')),
    h('div', { class: 'row' },
      h('input', { inputmode: 'numeric', maxlength: 6, pattern: '\\d{6}', placeholder: '123456', 'data-keep': 'host-passkey', 'aria-label': t('hosts.passkeyLabel'), required: true }),
      h('button', { class: 'primary', type: 'submit' }, t('common.send')),
    ),
  );
}

function hostWindowBox(s, a) {
  if (!s.hostWindow.open) return null;
  const c = s.hostCandidate;
  return h('div', { class: 'add' },
    h('div', { class: 'sec-head' },
      h('h3', {}, tParts('hosts.windowTitle', { seconds: countdown(s.hostWindow.until) })),
      h('button', { class: 'ghost small', onclick: a.closeHostWindow }, t('common.close')),
    ),
    h('ol', { class: 'steps' },
      h('li', {}, t('hosts.stepMac')),
      h('li', {}, t('hosts.stepWindows')),
      h('li', {}, t('hosts.stepCode')),
    ),
    c ? h('p', { class: 'hint' }, c.state === 'securing' ? t('hosts.verifying') : t('hosts.candidate', { addr: c.addr })) : null,
    hostPasskeyBox(s, a),
  );
}

export function renderHosts(s, a) {
  return h('div', {},
    h('div', { class: 'sec-head' },
      h('h2', {}, t('hosts.title')),
      h('span', { class: 'hint' }, t('hosts.hint')),
    ),
    h('ul', { class: 'hosts', role: 'radiogroup', 'aria-label': t('hosts.groupLabel') }, [1, 2, 3].map((n) => hostSlot(s, n, a))),
    hostWindowBox(s, a),
  );
}

/* ---- Input devices (a single list) ---- */

function deviceSubline(d) {
  if (d.state === 'failed') return FAIL_STAGE[d.error] || t('devices.failed');
  if (d.state === 'ready') return d.bond === 'restored' ? t('devices.restored') : d.bond === 'new' ? t('devices.newBond') : '';
  if (d.state === 'offline') {
    if (!d.auto) return t('devices.autoOff');
    return reasonText(d.lastReason) || t('devices.autoHint');
  }
  return '';
}

/*
 * The board reconnects paired devices by itself, so a waiting device has no button to press.
 * Only when auto-connect is off (the user disconnected it, or it failed repeatedly) do we offer a button to turn it back on.
 */
function deviceAction(d, connected, a) {
  if (connected) return h('button', { onclick: () => a.disconnectDevice(d.addr) }, t('devices.disconnect'));
  if (d.state === 'failed') return h('button', { onclick: () => a.connectDevice(d.id) }, t('devices.retry'));
  if (d.paired && !d.auto) return h('button', { onclick: () => a.connectDevice(d.id) }, t('devices.autoOn'));
  return null;
}

function deviceRow(d, a) {
  const connected = d.state !== 'offline' && d.state !== 'failed';
  return h('li', { class: 'device' },
    h('span', { class: `device-icon ${stateTone(d.state)}` }, icon(kindIcon(d.kinds, d.appearance), 22)),
    h('div', { class: 'device-main' },
      h('div', { class: 'device-title' },
        h('span', { class: 'device-name' }, d.name || t('common.unnamedDevice')),
        badge(stateText(d.state), stateTone(d.state)),
      ),
      h('div', { class: 'device-meta' },
        h('span', {}, kindText(d.kinds, d.appearance)),
        h('span', { class: 'mono' }, d.id),
      ),
      deviceSubline(d) ? h('div', { class: 'device-sub' }, deviceSubline(d)) : null,
    ),
    h('div', { class: 'device-actions' }, deviceAction(d, connected, a),
      d.paired ? h('button', { class: 'ghost danger', onclick: () => a.removeDevice(d) }, t('common.remove')) : null,
    ),
  );
}

export function renderDevices(s, a) {
  const list = [...s.devices.values()].sort((x, y) => (y.lastSeq || 0) - (x.lastSeq || 0));
  return h('div', {},
    h('div', { class: 'sec-head' },
      h('h2', {}, t('devices.title')),
      h('span', { class: 'hint' }, t('devices.summary', { bonds: s.bonds, max: s.bondsMax || '?' })),
    ),
    list.length
      ? h('ul', { class: 'devices' }, list.map((d) => deviceRow(d, a)))
      : h('div', { class: 'empty' },
          h('p', {}, t('devices.empty')),
          h('p', { class: 'hint' }, t('devices.emptyHint'))),
    s.addOpen
      ? null
      : h('button', {
        class: 'add-btn',
        onclick: a.openAdd,
        disabled: s.hostWindow.open,
        title: s.hostWindow.open ? t('devices.addBlocked') : '',
      }, icon('plus', 16), ' ' + t('devices.add')),
  );
}

/* ---- Add device (scan results show up only here) ---- */

function promptBox(s, a) {
  const p = s.prompt;
  if (!p) return null;
  if (p.type === 'numcmp') {
    return h('div', { class: 'prompt' },
      h('p', {}, t('prompt.numcmp')),
      h('div', { class: 'code' }, p.code),
      h('div', { class: 'row' },
        h('button', { class: 'primary', onclick: () => a.accept(true) }, t('prompt.match')),
        h('button', { onclick: () => a.accept(false) }, t('prompt.noMatch')),
      ),
    );
  }
  if (p.type === 'passkey_request') {
    const submit = (e) => {
      e.preventDefault();
      a.passkey(e.target.querySelector('input').value);
    };
    return h('form', { class: 'prompt', onsubmit: submit },
      h('p', {}, t('prompt.passkeyRequest')),
      h('div', { class: 'row' },
        h('input', { inputmode: 'numeric', maxlength: 6, pattern: '\\d{6}', placeholder: '123456', 'data-keep': 'passkey', 'aria-label': t('prompt.passkeyLabel'), required: true }),
        h('button', { class: 'primary', type: 'submit' }, t('common.send')),
      ),
    );
  }
  return h('div', { class: 'prompt' },
    h('p', {}, t('prompt.passkeyDisplay')),
    h('div', { class: 'code' }, p.code),
  );
}

function foundRow(f, a) {
  const busy = f.state === 'connecting';
  return h('li', { class: 'device found' },
    h('span', { class: 'device-icon idle' }, icon(kindIcon([], f.appearance), 20)),
    h('div', { class: 'device-main' },
      h('div', { class: 'device-title' },
        h('span', { class: 'device-name' }, f.name || t('common.unnamedDevice')),
        f.paired ? badge(t('add.paired'), 'idle') : null,
      ),
      h('div', { class: 'device-meta' },
        h('span', {}, kindText([], f.appearance)),
        h('span', {}, t('add.signal', { rssi: f.rssi })),
        h('span', { class: 'mono' }, f.addr),
      ),
    ),
    h('div', { class: 'device-actions' },
      f.hid === false
        ? h('span', { class: 'hint' }, t('add.notInput'))
        : h('button', { class: 'primary', disabled: busy, onclick: () => a.connectDevice(f.addr) }, busy ? t('add.connecting') : t('add.connect')),
    ),
  );
}

export function renderAdd(s, a) {
  if (!s.addOpen) return h('div');
  const found = [...s.found.values()].filter((f) => s.showAll || f.hid !== false).sort((x, y) => y.rssi - x.rssi);
  return h('div', { class: 'add' },
    h('div', { class: 'sec-head' },
      h('h3', {}, t('add.title')),
      h('button', { class: 'ghost small', onclick: a.closeAdd }, t('common.close')),
    ),
    h('ol', { class: 'steps' },
      h('li', {}, t('add.step1')),
      h('li', {}, t('add.step2')),
    ),
    promptBox(s, a),
    h('div', { class: 'row scan-row' },
      s.scanning
        ? h('button', { onclick: () => a.scan(false) }, tParts('add.scanning', { seconds: countdown(s.scanUntil) }))
        : s.scanPending
          ? h('button', { class: 'primary', disabled: true }, t('add.starting'))
          : h('button', { class: 'primary', onclick: () => a.scan(true) }, t('add.start')),
      h('label', { class: 'check' },
        h('input', { type: 'checkbox', checked: s.showAll, onchange: (e) => a.showAll(e.target.checked) }),
        ' ' + t('add.showAll')),
    ),
    found.length
      ? h('ul', { class: 'devices' }, found.map((f) => foundRow(f, a)))
      : h('p', { class: 'hint empty-found' }, s.scanning ? t('add.searching') : t('add.idleHint')),
  );
}

/* ---- Log (developer) ---- */

export function renderLog(s, opts) {
  const rows = s.log.filter((r) => opts.boardLines || r.dir !== '·').slice(-200);
  return h('div', { class: 'log mono' },
    rows.map((r) => h('div', { class: `log-row ${r.dir === '>' ? 'out' : r.dir === '·' ? 'board' : ''}` },
      `${r.at.toLocaleTimeString()} ${r.dir} ${r.text}`)),
  );
}
