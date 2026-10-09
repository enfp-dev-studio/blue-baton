/* 화면 부품. 상태(store.state)를 받아 DOM을 만든다. 동작은 actions로만 요청한다 */

import { h, icon } from './dom.js';
import { FAIL_STAGE, RADIO, hostStateText, hostStateTone, kindIcon, kindText, reasonText, stateText, stateTone } from './i18n.js';

function badge(text, tone) {
  return h('span', { class: `badge ${tone}` }, text);
}

/*
 * 남은 초. 영역을 매초 다시 그리면 입력칸이 새로 만들어져 포커스, 화면 낭독기 위치가 끊기므로
 * 숫자만 따로 고친다 (app.js의 1초 타이머가 data-until을 보고 고친다).
 */
export function secondsLeft(until) {
  return Math.max(0, Math.ceil((until - Date.now()) / 1000));
}

function countdown(until) {
  return h('span', { 'data-until': String(until) }, String(secondsLeft(until)));
}

/* ---- 상단: 보드 연결과 잠금 ---- */

export function renderLink(s, a) {
  if (s.link !== 'connected') {
    return h('div', { class: 'link' },
      h('span', { class: 'link-state' }, s.link === 'connecting' ? '보드에 연결하는 중…' : '보드가 연결되지 않았어요'),
      h('button', { class: 'primary', onclick: a.connect, disabled: s.link === 'connecting' }, icon('usb', 16), ' 보드 연결'),
    );
  }
  return h('div', { class: 'link' },
    h('span', { class: 'dot ok', title: '연결됨' }),
    h('span', { class: 'link-state' }, `${s.transport} · 펌웨어 ${s.fw || '?'}`),
    s.unlocked ? badge(RADIO[s.radio] || s.radio, s.radio === 'idle' ? 'idle' : 'busy') : null,
    s.unlocked ? h('button', { class: 'ghost', onclick: a.lock, title: '설정 잠그기' }, icon('lock', 16), ' 잠그기') : null,
    h('button', { class: 'ghost', onclick: a.disconnectLink, title: 'USB 포트를 놓습니다. 다른 프로그램이 보드를 쓸 수 있어요' }, '연결 끊기'),
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
    h('h2', {}, setting ? '설정 PIN 정하기' : '설정이 잠겨 있어요'),
    h('p', { class: 'hint' }, setting
      ? '보드 설정을 보호할 PIN(숫자 4~8자리)을 정하세요. 보드에 저장돼요.'
      : '보드에 저장된 PIN을 입력하면 설정을 열 수 있어요.'),
    h('div', { class: 'row' },
      h('input', { type: 'password', inputmode: 'numeric', autocomplete: 'off', placeholder: 'PIN', 'data-keep': 'pin', 'aria-label': 'PIN', maxlength: 8 }),
      h('button', { class: 'primary', type: 'submit' }, setting ? '정하기' : '열기'),
    ),
  );
}

export function renderNotice(s, a) {
  if (!s.notice) return h('div');
  return h('div', { class: `notice ${s.notice.kind}`, role: s.notice.kind === 'error' ? 'alert' : 'status' },
    h('span', {}, s.notice.text),
    h('button', { class: 'ghost small', onclick: a.dismiss, 'aria-label': '닫기' }, '닫기'),
  );
}

/* ---- 출력 호스트 (전환, 추가, 삭제) ---- */

function hostSlot(s, n, a) {
  const hostDev = s.hosts.get(n);
  const on = s.activeHost === n;
  return h('li', { class: `host${on ? ' on' : ''}` },
    h('button', {
      class: 'host-pick',
      role: 'radio',
      'aria-checked': on ? 'true' : 'false',
      onclick: () => a.selectHost(n),
      title: '이 호스트로 입력을 보내요',
    },
    h('span', { class: 'host-title' },
      h('span', { class: 'host-num' }, String(n)),
      h('span', { class: 'host-name' }, hostDev ? hostDev.name || '이름 없는 호스트' : '비어 있음'),
    ),
    hostDev ? badge(hostStateText(hostDev.state), hostStateTone(hostDev.state)) : null,
    h('span', { class: 'host-combo mono' }, s.combos[n - 1] || '조합 없음')),
    h('div', { class: 'host-actions' },
      hostDev
        ? h('button', { class: 'ghost danger small', onclick: () => a.removeHost(n, hostDev) }, '삭제')
        : h('button', { class: 'small', onclick: a.openHostWindow, disabled: s.hostWindow.open }, '추가')),
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
    h('p', {}, '컴퓨터 화면에 6자리 숫자가 보이면, 보드에 연결된 키보드로 그 숫자를 치고 Enter를 누르세요.'),
    h('div', { class: 'pk-dots', 'aria-label': `${p.len}자리 입력됨` }, dots),
    h('p', { class: 'hint' }, '보드에 키보드가 없으면 여기에 입력하세요 (Esc는 취소)'),
    h('div', { class: 'row' },
      h('input', { inputmode: 'numeric', maxlength: 6, pattern: '\\d{6}', placeholder: '123456', 'data-keep': 'host-passkey', 'aria-label': '호스트 패스키', required: true }),
      h('button', { class: 'primary', type: 'submit' }, '보내기'),
    ),
  );
}

function hostWindowBox(s, a) {
  if (!s.hostWindow.open) return null;
  const c = s.hostCandidate;
  return h('div', { class: 'add' },
    h('div', { class: 'sec-head' },
      h('h3', {}, '호스트 추가 · ', countdown(s.hostWindow.until), '초'),
      h('button', { class: 'ghost small', onclick: a.closeHostWindow }, '닫기'),
    ),
    h('ol', { class: 'steps' },
      h('li', {}, 'Mac: 시스템 설정 → Bluetooth에서 근처 기기의 "KM Router" 옆 연결'),
      h('li', {}, 'Windows: 설정 → Bluetooth 및 장치 → 장치 추가 → Bluetooth → "KM Router"'),
      h('li', {}, '숫자 6자리가 나오면 아래 안내대로 입력하세요'),
    ),
    c ? h('p', { class: 'hint' }, c.state === 'securing' ? '확인하는 중…' : `컴퓨터(${c.addr})가 연결했어요. 페어링을 기다리는 중…`) : null,
    hostPasskeyBox(s, a),
  );
}

export function renderHosts(s, a) {
  return h('div', {},
    h('div', { class: 'sec-head' },
      h('h2', {}, '호스트'),
      h('span', { class: 'hint' }, '키 조합을 누르거나 여기서 골라 입력을 보낼 컴퓨터를 바꿔요'),
    ),
    h('ul', { class: 'hosts', role: 'radiogroup', 'aria-label': '활성 호스트' }, [1, 2, 3].map((n) => hostSlot(s, n, a))),
    hostWindowBox(s, a),
  );
}

/* ---- 입력 기기 (한 목록) ---- */

function deviceSubline(d) {
  if (d.state === 'failed') return FAIL_STAGE[d.error] || '연결에 실패했어요';
  if (d.state === 'ready') return d.bond === 'restored' ? '저장된 키로 다시 연결됨' : d.bond === 'new' ? '새로 페어링됨' : '';
  if (d.state === 'offline') {
    if (!d.auto) return '자동 연결 꺼짐 · 연결을 누르면 다시 켜져요';
    return reasonText(d.lastReason) || '기기를 움직이거나 키를 누르면 자동으로 연결돼요';
  }
  return '';
}

/*
 * 페어링된 기기는 보드가 자동으로 다시 연결한다. 그래서 대기 중이면 누를 버튼이 없고,
 * 자동 연결이 꺼졌을 때(사용자가 끊었거나 연속 실패)만 다시 켜는 버튼을 준다.
 */
function deviceAction(d, connected, a) {
  if (connected) return h('button', { onclick: () => a.disconnectDevice(d.addr) }, '끊기');
  if (d.state === 'failed') return h('button', { onclick: () => a.connectDevice(d.id) }, '다시 시도');
  if (d.paired && !d.auto) return h('button', { onclick: () => a.connectDevice(d.id) }, '자동 연결 켜기');
  return null;
}

function deviceRow(d, a) {
  const connected = d.state !== 'offline' && d.state !== 'failed';
  return h('li', { class: 'device' },
    h('span', { class: `device-icon ${stateTone(d.state)}` }, icon(kindIcon(d.kinds, d.appearance), 22)),
    h('div', { class: 'device-main' },
      h('div', { class: 'device-title' },
        h('span', { class: 'device-name' }, d.name || '이름 없는 기기'),
        badge(stateText(d.state), stateTone(d.state)),
      ),
      h('div', { class: 'device-meta' },
        h('span', {}, kindText(d.kinds, d.appearance)),
        h('span', { class: 'mono' }, d.id),
      ),
      deviceSubline(d) ? h('div', { class: 'device-sub' }, deviceSubline(d)) : null,
    ),
    h('div', { class: 'device-actions' }, deviceAction(d, connected, a),
      d.paired ? h('button', { class: 'ghost danger', onclick: () => a.removeDevice(d) }, '삭제') : null,
    ),
  );
}

export function renderDevices(s, a) {
  const list = [...s.devices.values()].sort((x, y) => (y.lastSeq || 0) - (x.lastSeq || 0));
  return h('div', {},
    h('div', { class: 'sec-head' },
      h('h2', {}, '입력 기기'),
      h('span', { class: 'hint' }, `페어링 ${s.bonds}/${s.bondsMax || '?'} · 동시 연결 최대 2대`),
    ),
    list.length
      ? h('ul', { class: 'devices' }, list.map((d) => deviceRow(d, a)))
      : h('div', { class: 'empty' },
          h('p', {}, '아직 페어링된 기기가 없어요'),
          h('p', { class: 'hint' }, '키보드나 마우스를 추가하면 보드에 저장되고, 다음부터는 자동으로 연결돼요.')),
    s.addOpen
      ? null
      : h('button', {
        class: 'add-btn',
        onclick: a.openAdd,
        disabled: s.hostWindow.open,
        title: s.hostWindow.open ? '호스트 추가 창을 닫은 뒤에 추가할 수 있어요' : '',
      }, icon('plus', 16), ' 기기 추가'),
  );
}

/* ---- 기기 추가 (스캔 결과는 여기서만 보인다) ---- */

function promptBox(s, a) {
  const p = s.prompt;
  if (!p) return null;
  if (p.type === 'numcmp') {
    return h('div', { class: 'prompt' },
      h('p', {}, '기기 화면에 보이는 숫자와 같은지 확인하세요'),
      h('div', { class: 'code' }, p.code),
      h('div', { class: 'row' },
        h('button', { class: 'primary', onclick: () => a.accept(true) }, '같아요, 연결'),
        h('button', { onclick: () => a.accept(false) }, '달라요'),
      ),
    );
  }
  if (p.type === 'passkey_request') {
    const submit = (e) => {
      e.preventDefault();
      a.passkey(e.target.querySelector('input').value);
    };
    return h('form', { class: 'prompt', onsubmit: submit },
      h('p', {}, '기기에 보이는 6자리 숫자를 입력하세요'),
      h('div', { class: 'row' },
        h('input', { inputmode: 'numeric', maxlength: 6, pattern: '\\d{6}', placeholder: '123456', 'data-keep': 'passkey', 'aria-label': '패스키', required: true }),
        h('button', { class: 'primary', type: 'submit' }, '보내기'),
      ),
    );
  }
  return h('div', { class: 'prompt' },
    h('p', {}, '이 번호를 그 키보드로 입력한 뒤 Enter를 누르세요'),
    h('div', { class: 'code' }, p.code),
  );
}

function foundRow(f, a) {
  const busy = f.state === 'connecting';
  return h('li', { class: 'device found' },
    h('span', { class: 'device-icon idle' }, icon(kindIcon([], f.appearance), 20)),
    h('div', { class: 'device-main' },
      h('div', { class: 'device-title' },
        h('span', { class: 'device-name' }, f.name || '이름 없는 기기'),
        f.paired ? badge('페어링됨', 'idle') : null,
      ),
      h('div', { class: 'device-meta' },
        h('span', {}, kindText([], f.appearance)),
        h('span', {}, `신호 ${f.rssi} dBm`),
        h('span', { class: 'mono' }, f.addr),
      ),
    ),
    h('div', { class: 'device-actions' },
      f.hid === false
        ? h('span', { class: 'hint' }, '입력 기기 아님')
        : h('button', { class: 'primary', disabled: busy, onclick: () => a.connectDevice(f.addr) }, busy ? '연결하는 중…' : '연결'),
    ),
  );
}

export function renderAdd(s, a) {
  if (!s.addOpen) return h('div');
  const found = [...s.found.values()].filter((f) => s.showAll || f.hid !== false).sort((x, y) => y.rssi - x.rssi);
  return h('div', { class: 'add' },
    h('div', { class: 'sec-head' },
      h('h3', {}, '새 기기 추가'),
      h('button', { class: 'ghost small', onclick: a.closeAdd }, '닫기'),
    ),
    h('ol', { class: 'steps' },
      h('li', {}, '추가할 키보드나 마우스를 페어링 모드로 켜세요 (기기마다 버튼이 달라요)'),
      h('li', {}, '아래 목록에 나타나면 연결을 누르세요. 보드는 고른 기기에만 연결해요'),
    ),
    promptBox(s, a),
    h('div', { class: 'row scan-row' },
      s.scanning
        ? h('button', { onclick: () => a.scan(false) }, '찾는 중 · ', countdown(s.scanUntil), '초 · 멈추기')
        : s.scanPending
          ? h('button', { class: 'primary', disabled: true }, '시작하는 중…')
          : h('button', { class: 'primary', onclick: () => a.scan(true) }, '찾기 시작'),
      h('label', { class: 'check' },
        h('input', { type: 'checkbox', checked: s.showAll, onchange: (e) => a.showAll(e.target.checked) }),
        ' 입력 기기가 아닌 것도 보기'),
    ),
    found.length
      ? h('ul', { class: 'devices' }, found.map((f) => foundRow(f, a)))
      : h('p', { class: 'hint empty-found' }, s.scanning ? '기기를 찾고 있어요…' : '찾기 시작을 누르세요. 60초 동안 찾아요.'),
  );
}

/* ---- 로그 (개발자) ---- */

export function renderLog(s, opts) {
  const rows = s.log.filter((r) => opts.boardLines || r.dir !== '·').slice(-200);
  return h('div', { class: 'log mono' },
    rows.map((r) => h('div', { class: `log-row ${r.dir === '>' ? 'out' : r.dir === '·' ? 'board' : ''}` },
      `${r.at.toLocaleTimeString()} ${r.dir} ${r.text}`)),
  );
}
