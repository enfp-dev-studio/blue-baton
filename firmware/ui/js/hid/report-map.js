/*
 * HID Report Map(Report Descriptor) 해석과 입력 리포트 풀이. HID 1.11 6.2.2의 짧은 항목을 읽는다.
 * 기기별 가정 없이 Report Map이 말하는 대로 필드를 꺼낸다 (entries/006).
 */

export const PAGE = { DESKTOP: 0x01, KEYBOARD: 0x07, BUTTON: 0x09, CONSUMER: 0x0c };

const DIGITS = '1234567890';
const MODS = ['LCtrl', 'LShift', 'LAlt', 'LGui', 'RCtrl', 'RShift', 'RAlt', 'RGui'];
const KEY_NAMES = {
  0x28: 'Enter', 0x29: 'Esc', 0x2a: 'Backspace', 0x2b: 'Tab', 0x2c: 'Space', 0x39: 'CapsLock',
  0x2d: '-', 0x2e: '=', 0x2f: '[', 0x30: ']', 0x31: '\\', 0x33: ';', 0x34: "'", 0x35: '`',
  0x36: ',', 0x37: '.', 0x38: '/', 0x4f: 'Right', 0x50: 'Left', 0x51: 'Down', 0x52: 'Up',
  0x4a: 'Home', 0x4b: 'PageUp', 0x4c: 'Delete', 0x4d: 'End', 0x4e: 'PageDown',
};
const SHIFTED = { 0x2d: '_', 0x2e: '+', 0x2f: '{', 0x30: '}', 0x31: '|', 0x33: ':', 0x34: '"', 0x35: '~', 0x36: '<', 0x37: '>', 0x38: '?' };
const SHIFTED_DIGITS = '!@#$%^&*()';

export function hexToBytes(hex) {
  const out = new Uint8Array(hex.length / 2);
  for (let i = 0; i < out.length; i++) out[i] = parseInt(hex.substr(i * 2, 2), 16);
  return out;
}

/* Report Map -> Map(리포트 ID -> {bits, fields}). 입력(Input) 항목만 모은다 */
export function parseReportMap(d) {
  const out = new Map();
  let page = 0, lmin = 0, rsize = 0, rcount = 0, rid = 0;
  const stack = [];
  let usages = [], umin = null;
  const layout = (id) => {
    if (!out.has(id)) out.set(id, { bits: 0, fields: [] });
    return out.get(id);
  };
  /* 4바이트 Usage는 위 16비트가 Usage Page다 */
  const usageOf = (v, size) => (size === 4 ? { page: v >>> 16, usage: v & 0xffff } : { page, usage: v });
  let i = 0;
  while (i < d.length) {
    const pre = d[i++];
    if (pre === 0xfe) {          /* Long Item: 크기, 태그, 데이터 */
      i += 2 + (d[i] || 0);
      continue;
    }
    const sc = pre & 3;
    const size = sc === 3 ? 4 : sc;
    const type = (pre >> 2) & 3;
    const tag = (pre >> 4) & 15;
    if (i + size > d.length) break;
    let v = 0;
    for (let k = 0; k < size; k++) v += d[i + k] * 2 ** (8 * k);
    i += size;
    const sv = size === 0 ? 0 : size === 4 ? v | 0 : (v << (32 - 8 * size)) >> (32 - 8 * size);
    if (type === 0) {                       /* Main */
      if (tag === 8) {                      /* Input */
        const isConst = (v & 1) !== 0;
        const isArr = (v & 2) === 0;
        const isRel = (v & 4) !== 0;
        const L = layout(rid);
        for (let n = 0; n < rcount; n++) {
          if (isConst) continue;
          let u;
          if (usages.length > n) u = usages[n];
          else if (usages.length && !umin) u = usages[usages.length - 1];   /* 마지막 Usage가 나머지에 적용 */
          else if (umin) u = { page: umin.page, usage: umin.usage + n };
          else u = { page, usage: 0 };
          L.fields.push({
            page: isArr && umin ? umin.page : u.page,
            usage: u.usage, size: rsize, offset: L.bits + n * rsize,
            lmin, arr: isArr, rel: isRel, umin,
          });
        }
        L.bits += rsize * rcount;
      }
      usages = [];                          /* Local 항목은 Main 항목마다 초기화 */
      umin = null;
    } else if (type === 1) {                /* Global */
      if (tag === 0) page = v;
      else if (tag === 1) lmin = sv;
      else if (tag === 7) rsize = v;
      else if (tag === 8) rid = v;
      else if (tag === 9) rcount = v;
      else if (tag === 10) stack.push({ page, lmin, rsize, rcount, rid });
      else if (tag === 11 && stack.length) ({ page, lmin, rsize, rcount, rid } = stack.pop());
    } else if (type === 2) {                /* Local */
      if (tag === 0) usages.push(usageOf(v, size));
      else if (tag === 1) umin = usageOf(v, size);
    }
  }
  return out;
}

export function getBits(d, off, size, signed) {
  let v = 0;
  for (let k = 0; k < size; k++) {
    const p = off + k;
    const byte = d[p >> 3];
    if (byte === undefined) return null;
    if ((byte >> (p & 7)) & 1) v += 2 ** k;
  }
  if (signed && size > 0 && v >= 2 ** (size - 1)) v -= 2 ** size;
  return v;
}

export function usageName(page, u) {
  if (page === PAGE.KEYBOARD) {
    if (u >= 0x04 && u <= 0x1d) return String.fromCharCode(97 + u - 0x04);
    if (u >= 0x1e && u <= 0x27) return DIGITS[u - 0x1e];
    if (u >= 0x3a && u <= 0x45) return 'F' + (u - 0x39);
    if (u >= 0xe0 && u <= 0xe7) return MODS[u - 0xe0];
    return KEY_NAMES[u] || '키 0x' + u.toString(16);
  }
  if (page === PAGE.BUTTON) return '버튼' + u;
  if (page === PAGE.DESKTOP) return { 0x30: 'X', 0x31: 'Y', 0x38: '휠' }[u] || '데스크톱 0x' + u.toString(16);
  if (page === PAGE.CONSUMER) return { 0xe9: '볼륨+', 0xea: '볼륨-', 0xe2: '음소거', 0xcd: '재생/정지', 0x238: '가로 휠' }[u] || '소비자 0x' + u.toString(16);
  return `page 0x${page.toString(16)} usage 0x${u.toString(16)}`;
}

/* 키 사용 코드를 글자로 (미국 배열 기준, 확인용) */
export function typeChar(u, shift) {
  if (u === 0x28) return '\n';
  if (u === 0x2b) return '\t';
  if (u === 0x2c) return ' ';
  if (u >= 0x04 && u <= 0x1d) {
    const c = String.fromCharCode(97 + u - 0x04);
    return shift ? c.toUpperCase() : c;
  }
  if (u >= 0x1e && u <= 0x27) return shift ? SHIFTED_DIGITS[u - 0x1e] : DIGITS[u - 0x1e];
  if (KEY_NAMES[u] && KEY_NAMES[u].length === 1) return shift && SHIFTED[u] ? SHIFTED[u] : KEY_NAMES[u];
  return '';
}

/* 눌린 것: 1비트 필드는 값 1, 배열 필드는 값이 가리키는 사용 코드 */
export function pressedUsages(fields, page) {
  const set = new Set();
  for (const f of fields) {
    if (f.page !== page) continue;
    if (f.arr) {
      if (f.value !== 0 && f.umin) set.add(f.umin.usage + (f.value - f.lmin));
    } else if (f.value) {
      set.add(f.usage);
    }
  }
  return set;
}

/*
 * 장치별 해석 상태. report_ref(핸들 -> 리포트 ID, 서비스)와 report_map(서비스별)을 모아 두고
 * 입력 리포트를 필드 값으로 푼다.
 */
export class ReportDecoder {
  constructor() {
    this.maps = new Map();   /* addr|svc -> Map(id -> layout) */
    this.refs = new Map();   /* addr|handle -> {id, type, svc} */
  }

  addRef(ev) {
    this.refs.set(`${ev.addr}|${ev.val}`, { id: ev.id, type: ev.type, svc: ev.svc || 0 });
  }

  addMap(ev) {
    const layouts = parseReportMap(hexToBytes(ev.hex));
    this.maps.set(`${ev.addr}|${ev.svc || 0}`, layouts);
    return layouts;
  }

  forget(addr) {
    for (const k of [...this.maps.keys()]) if (k.startsWith(addr + '|')) this.maps.delete(k);
    for (const k of [...this.refs.keys()]) if (k.startsWith(addr + '|')) this.refs.delete(k);
  }

  decode(addr, handle, data) {
    const ref = this.refs.get(`${addr}|${handle}`) || { id: 0, type: 1, svc: 0 };
    const layouts = this.maps.get(`${addr}|${ref.svc}`);
    if (!layouts) return null;
    const lay = layouts.get(ref.id);
    if (!lay) return null;
    const need = Math.ceil(lay.bits / 8);
    /* 리포트 ID는 Report Reference에만 있고 알림 데이터에는 보통 없다. 있으면 앞 1바이트를 뗀다 */
    const p = ref.id !== 0 && data.length === need + 1 && data[0] === ref.id ? data.slice(1) : data;
    const fields = [];
    for (const f of lay.fields) {
      const value = getBits(p, f.offset, f.size, f.lmin < 0);
      if (value !== null) fields.push({ ...f, value });
    }
    return { id: ref.id, key: `${addr}|${ref.svc}|${ref.id}`, fields };
  }
}
