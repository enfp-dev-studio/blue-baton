/*
 * 모의 보드. 하드웨어 없이 화면을 만들고 검증한다 (GUI 제안서 5.2).
 * 펌웨어 0.5의 명령, 응답, 이벤트 형식(firmware/PROTOCOL.md)을 흉내 낸다.
 *
 * 장치 구성
 * - 실기에서 받은 마우스와 키보드: Report Map, 핸들, 리포트 ID를 그대로 쓴다 (entries/007).
 * - 실기로 시험하지 못한 페어링 방식은 가상 장치로 둔다: 숫자 비교, 패스키 입력, 패스키 표시,
 *   이름이 스캔 응답에만 있는 장치, HID가 아닌 장치.
 *
 * 호스트: 1번은 본딩된 모의 Mac(부팅 뒤 붙는다). 호스트 추가 창을 열면 2초 뒤 모의 PC가 붙어
 * 패스키를 요청한다. 그 PC 화면의 숫자는 보드 로그 줄에 찍힌다.
 *
 * 주소창 옵션: ?mock (기본), &unlocked (PIN 없이 열림), &fast (스캔 10초)
 */

const MOUSE_MAP =
  '05010902A10185010901A1000509190129051500250195057501810295017503810105010930093116018026FF7F75109502' +
  '810609381581257F750895018106C0C0';
const KB_MAP =
  '05010902A10185010901A100050919012905150025019505750181029501750381010501093009311581257F750895028106' +
  '09381581257F750895018106050C0A38021581257F750895018106C0C0050C0901A101850275109501150126FF0319012AFF' +
  '038160C005010906A1018503050719E029E71500250175019508810295017508810195057501050819012905910295017503' +
  '910195067508150025E70507190029E78100C005010906A101850475019578050715002501190029788102C0050C0901A101' +
  '85051500250175019503810175019501050C09B8810206FF0009038102750195038101C0';
/* 가상 키보드: 리포트 ID 1, 수정 키 8비트 + 예약 1바이트 + 키 배열 6개 */
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
      addr: 'C0:FF:EE:00:00:01', name: '모의 키보드 (숫자 비교)', appearance: 961, io: 'numcmp',
      map: SIMPLE_KB_MAP, kinds: ['keyboard'], reports: [{ val: 20, id: 1 }], role: 'simple', foundAfter: 1200,
    },
    {
      addr: 'C0:FF:EE:00:00:02', name: '모의 키보드 (패스키 입력)', appearance: 961, io: 'passkey_request',
      map: SIMPLE_KB_MAP, kinds: ['keyboard'], reports: [{ val: 20, id: 1 }], role: 'simple', foundAfter: 2000,
      shownCode: '123456',
    },
    {
      addr: 'C0:FF:EE:00:00:03', name: '모의 키보드 (패스키 표시)', appearance: 961, io: 'passkey_display',
      map: SIMPLE_KB_MAP, kinds: ['keyboard'], reports: [{ val: 20, id: 1 }], role: 'simple', foundAfter: 2800,
    },
    {
      addr: 'C0:FF:EE:00:00:04', name: '모의 마우스 (이름은 스캔 응답에만)', appearance: 962, io: 'none',
      map: MOUSE_MAP, kinds: ['mouse'], reports: [{ val: 25, id: 1 }], role: 'mouse', foundAfter: 3500,
      nameLate: true,
    },
    { addr: 'AA:00:00:00:00:01', name: 'Galaxy Buds', appearance: 0, hid: false, foundAfter: 900 },
  ];
}

const hex = (bytes) => bytes.map((b) => (b & 0xff).toString(16).padStart(2, '0').toUpperCase()).join('');
const i16 = (v) => [v & 0xff, (v >> 8) & 0xff];

class MockBoard {
  constructor(emit, opts) {
    this.emit = emit;
    this.opts = opts;
    this.devices = catalog();
    this.pinSet = true;
    this.pin = '2468';   /* 모의 보드 전용 값 */
    this.unlocked = opts.unlocked;
    this.combo = '';
    this.activeHost = 1;
    this.scanning = false;
    this.radio = 'idle';
    this.peers = new Map();   /* addr -> {dev, state, user} */
    this.timers = new Set();
    this.seq = 3;
    /* 호스트: 번호 -> {addr, name, connected, state}. 1번은 본딩된 모의 Mac */
    this.hosts = new Map([[1, { addr: '3C:22:FB:00:00:01', name: '모의 MacBook Pro', connected: false, state: 'offline' }]]);
    this.hostWindow = null;   /* 창이 닫히는 타이머 */
    this.candidate = null;    /* {addr, code} 창에서 붙은 모의 PC */
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

  /* 본딩된 장치가 깨어나 광고: 자동 재연결 대기 중이면 연결된다 */
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
      this.emit(`I (mock) 모의 장치 화면에 보이는 숫자: ${p.code}`);
    } else if (d.io === 'passkey_request') {
      this.later(400, () => this.ev({ event: 'passkey_request', addr: d.addr }));
      this.emit(`I (mock) 모의 장치 화면에 보이는 패스키: ${d.shownCode}`);
    } else if (d.io === 'passkey_display') {
      const code = String(Math.floor(Math.random() * 1e6)).padStart(6, '0');
      this.later(400, () => this.ev({ event: 'passkey_display', addr: d.addr, code, ok: true }));
      /* 사용자가 키보드로 번호를 치는 데 걸리는 시간 */
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

  /* 준비된 장치가 입력을 보낸다: 마우스는 원을 그리고, 키보드는 글자를 친다 */
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
          this.ev({ event: 'report', addr: d.addr, handle: 25, data: hex([btn, ...i16(Math.round(Math.cos(a) * 6)), ...i16(Math.round(Math.sin(a) * 6)), 0]) });
        }
      } else if (d.role === 'nkro' || d.role === 'simple') {
        const text = 'hello km ';
        const step = Math.floor((t % 9) / 0.15);   /* 0.15초마다 한 단계: 누름, 뗌 */
        if (step === p.lastStep || step >= text.length * 2) continue;
        p.lastStep = step;
        const ch = text[step >> 1];
        const usage = ch === ' ' ? 0x2c : 0x04 + ch.charCodeAt(0) - 97;
        const down = (step & 1) === 0;
        if (d.role === 'nkro') {
          const bits = new Array(15).fill(0);
          if (down) bits[usage >> 3] |= 1 << (usage & 7);
          this.ev({ event: 'report', addr: d.addr, handle: 80, data: hex(bits) });
        } else {
          this.ev({ event: 'report', addr: d.addr, handle: 20, data: hex([0, 0, down ? usage : 0, 0, 0, 0, 0, 0]) });
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
          /* 이름이 스캔 응답에만 있는 장치: 같은 주소로 이름이 채워져 다시 온다 */
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

  /* ---- 호스트 ---- */

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
      /* 사용자가 컴퓨터에서 "KM Router"를 고르는 데 걸리는 시간 */
      this.later(2000, () => {
        if (!this.hostWindow || this.candidate) return;
        this.candidate = { addr: '00:1A:7D:00:00:0' + this.freeHost(), code: String(100000 + Math.floor(Math.random() * 900000)) };
        this.ev({ event: 'host_connected', host: 0, addr: this.candidate.addr, bonded: false, itvl: 24 });
        this.later(500, () => {
          if (!this.candidate) return;
          this.emit(`I (mock) 모의 PC 화면에 보이는 숫자: ${this.candidate.code}`);
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
        h.name = '모의 Windows PC';
        this.ev({ event: 'host_name', host: n, addr: c.addr, name: h.name });
      });
    });
  }

  handle(req) {
    const { id, cmd } = req;
    const open = ['hello', 'status', 'auth', 'lock', 'pin_set'];
    if (!open.includes(cmd) && !this.unlocked) {
      return this.fail(id, 'error', '잠금 상태입니다. auth 명령으로 PIN을 먼저 입력하세요');
    }
    switch (cmd) {
      case 'hello':
        return this.reply(id, { fw: 'km-router', version: '0.5-mock', pin_set: this.pinSet, unlocked: this.unlocked, combo_set: !!this.combo, wifi_feature: false });
      case 'auth':
        if (req.pin !== this.pin) return this.fail(id, 'error', 'PIN이 맞지 않습니다');
        this.unlocked = true;
        return this.reply(id);
      case 'lock':
        this.unlocked = false;
        return this.reply(id);
      case 'pin_set':
        if (!/^\d{4,8}$/.test(req.pin || '')) return this.fail(id, 'error', 'PIN은 숫자 4~8자리여야 합니다');
        this.pin = req.pin;
        this.unlocked = true;
        return this.reply(id);
      case 'combo_get':
        return this.reply(id, { combo: this.combo || 'ctrl+alt+1,ctrl+alt+2,ctrl+alt+3', default: !this.combo, stored_invalid: false });
      case 'host_select': {
        if (!(req.host >= 1 && req.host <= 3)) return this.fail(id, 'bad_host', 'host는 1~3입니다');
        const from = this.activeHost;
        this.reply(id);
        if (req.host !== from) {
          this.activeHost = req.host;
          this.ev({ event: 'host_switch', host: req.host, from });
          this.ev({ event: 'host_online', host: req.host, online: this.hosts.get(req.host)?.state === 'ready' });
          this.ev({ event: 'route', host: req.host, keys: [], buttons: 0, consumer: [] });
        }
        return;
      }
      case 'host_pair':
        if (typeof req.on !== 'boolean') return this.fail(id, 'error', 'on(true 또는 false)이 없습니다');
        if (!this.openHostWindow(req.on)) return this.fail(id, 'hosts_full', '호스트 3대가 모두 찼습니다. 하나를 지운 뒤 다시 여세요');
        return this.reply(id);
      case 'host_list': {
        const hosts = [...this.hosts.entries()].sort((x, y) => x[0] - y[0]).map(([n, h]) => ({
          host: n, addr: h.addr, name: h.name, connected: h.connected, state: h.connected ? h.state : 'offline', active: this.activeHost === n,
        }));
        return this.reply(id, { hosts });
      }
      case 'host_remove': {
        if (!(req.host >= 1 && req.host <= 3)) return this.fail(id, 'bad_host', 'host는 1~3입니다');
        const h = this.hosts.get(req.host);
        if (!h) return this.fail(id, 'not_found', '그 번호에 페어링된 호스트가 없습니다');
        this.reply(id);
        if (h.connected) this.ev({ event: 'host_disconnected', host: req.host, addr: h.addr, reason: 534 });
        this.hosts.delete(req.host);
        return this.ev({ event: 'host_removed', host: req.host });
      }
      case 'host_passkey':
        if (!/^\d{6}$/.test(req.code || '')) return this.fail(id, 'bad_code', '패스키는 숫자 6자리입니다');
        if (!this.candidate) return this.fail(id, 'no_request', '지금은 패스키를 받을 호스트가 없습니다');
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
          active_host: this.activeHost,
        });
      }
      case 'pair_scan':
        if (req.on && this.hostWindow) return this.fail(id, 'host_window', '호스트 추가 창이 열려 있습니다. 창을 닫은 뒤 입력 장치를 추가하세요');
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
        if (!d || d.hid === false) return this.fail(id, 'not_found', '그 주소의 장치가 없습니다');
        if (this.peers.has(d.addr)) return this.fail(id, 'busy', '지금은 할 수 없습니다 (이미 연결됨)');
        if (this.freeSlots() <= 0) return this.fail(id, 'no_slot', '입력 장치 자리가 모두 찼습니다');
        if (d.bonded) {
          /* 펌웨어와 같다: 본딩된 기기는 직접 연결하지 않고 자동 연결 목록에 다시 넣는다. 기기가 깨면 붙는다 */
          d.auto = true;
          this.stopScan('radio');
          this.reply(id);
          this.radio = 'idle';
          this.radioUpdate();
          return this.later(1500, () => this.wake(d));
        }
        if (this.bonded().length >= 8 && !d.bonded) {
          this.reply(id);
          return this.ev({ event: 'bond_full', stage: 'before_pairing', bonds: 8, max: 8 });
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
        if (!p) return this.fail(id, 'not_found', '그 주소의 장치가 없습니다');
        p.dev.auto = false;
        this.reply(id);
        return this.drop(p.dev, 534);
      }
      case 'pair_accept': {
        const p = [...this.peers.values()].find((x) => x.code);
        if (!p) return this.fail(id, 'no_request', '지금은 수락할 요청이 없습니다');
        p.code = null;
        this.reply(id);
        if (req.accept) return this.later(300, () => this.secured(p.dev, 'new'));
        return this.failSecurity(p.dev, 1288);
      }
      case 'passkey': {
        const p = [...this.peers.values()].find((x) => x.dev.io === 'passkey_request' && x.state === 'securing');
        if (!p) return this.fail(id, 'no_request', '지금은 패스키를 받을 요청이 없습니다');
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
        if (!d || !d.bonded) return this.fail(id, 'not_found', '그 주소의 장치가 없습니다');
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
        return this.fail(id, 'error', '알 수 없는 명령입니다');
    }
  }
}

export class MockTransport {
  constructor(params = new URLSearchParams()) {
    this.label = '모의 보드';
    this.opts = { unlocked: params.has('unlocked'), fast: params.has('fast') };
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
    /* 실제 보드처럼 응답은 조금 뒤에 온다 */
    setTimeout(() => this.board?.handle(req), 20);
  }

  async close() {
    this.board?.stop();
    this.board = null;
    this.onClose('closed');
  }
}
