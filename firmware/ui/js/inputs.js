/*
 * 입력 확인 (개발자 영역). 입력 리포트는 초당 수십 개가 오므로 화면 전체를 다시 그리지 않고 칸만 바꾼다.
 * 해석은 Report Map대로 한다 (hid/report-map.js).
 */

import { h } from './dom.js';
import { PAGE, ReportDecoder, hexToBytes, pressedUsages, typeChar, usageName } from './hid/report-map.js';

export class InputMonitor {
  constructor(root) {
    this.decoder = new ReportDecoder();
    this.kb = new Map();       /* 리포트 키 -> 눌린 키 집합 */
    this.typed = '';
    this.totals = { dx: 0, dy: 0, wh: 0 };
    this.mouseCount = 0;
    this.thisSec = 0;
    this.rows = new Map();     /* 장치|핸들 -> 표의 행 */
    this.build(root);
    setInterval(() => {
      this.el.rate.textContent = this.thisSec;
      this.thisSec = 0;
    }, 1000);
  }

  build(root) {
    const cell = (label, id) => {
      const v = h('span', { class: 'big' }, id === 'btn' ? '-' : '0');
      const c = h('div', { class: 'cell' }, h('small', {}, label), v);
      return [c, v];
    };
    const [cBtn, vBtn] = cell('버튼', 'btn');
    const [cDx, vDx] = cell('X 누적', 'dx');
    const [cDy, vDy] = cell('Y 누적', 'dy');
    const [cWh, vWh] = cell('휠 누적', 'wh');
    this.el = {
      cells: { btn: cBtn, dx: cDx, dy: cDy, wh: cWh },
      btn: vBtn, dx: vDx, dy: vDy, wh: vWh,
      count: h('b', {}, '0'),
      rate: h('b', {}, '0'),
      mods: h('b', {}, '-'),
      keys: h('b', {}, '-'),
      media: h('b', {}, '-'),
      text: h('div', { class: 'typed mono', 'aria-live': 'polite' }),
      table: h('tbody'),
      fields: h('div', { class: 'mono fields' }, '-'),
      maps: h('p', { class: 'hint' }, 'Report Map 대기 중 (연결되면 보드가 읽어서 보내 줘요)'),
    };
    const e = this.el;
    root.replaceChildren(
      h('div', { class: 'sec-head' }, h('h2', {}, '입력 확인'), h('span', { class: 'hint' }, '보드에 들어온 입력을 Report Map대로 풀어 보여 줘요')),
      h('h3', {}, '마우스'),
      h('div', { class: 'grid4' }, cBtn, cDx, cDy, cWh),
      h('p', { class: 'hint' }, '리포트 ', e.count, '개 · 초당 ', e.rate, '개 ',
        h('button', { class: 'ghost small', onclick: () => this.resetMouse() }, '누적 지우기')),
      h('h3', {}, '키보드'),
      h('p', { class: 'hint' }, '수정 키 ', e.mods, ' · 누른 키 ', e.keys, ' · 미디어 키 ', e.media),
      e.text,
      h('button', { class: 'ghost small', onclick: () => this.resetKeys() }, '입력 지우기'),
      h('details', { class: 'sub' },
        h('summary', {}, '리포트 해석 상세'),
        e.maps,
        h('table', { class: 'rtab' },
          h('thead', {}, h('tr', {}, ['장치', '핸들', '길이', '개수', '마지막 값'].map((t) => h('th', {}, t)))),
          e.table),
        h('p', { class: 'hint' }, '마지막 리포트에서 0이 아닌 필드'),
        e.fields),
    );
  }

  flash(el) {
    el.classList.add('hit');
    clearTimeout(el._t);
    el._t = setTimeout(() => el.classList.remove('hit'), 140);
  }

  resetMouse() {
    this.totals = { dx: 0, dy: 0, wh: 0 };
    for (const k of ['dx', 'dy', 'wh']) this.el[k].textContent = '0';
  }

  resetKeys() {
    this.typed = '';
    this.kb.clear();
    this.el.text.textContent = '';
  }

  /* 보드 이벤트를 받는다. 관심 있는 것만 처리한다 */
  onEvent(ev) {
    if (ev.event === 'report_ref') this.decoder.addRef(ev);
    else if (ev.event === 'report_map') this.onMap(ev);
    else if (ev.event === 'report') this.onReport(ev);
    else if (ev.event === 'unpaired') this.decoder.forget(ev.id_addr);
  }

  onMap(ev) {
    const layouts = this.decoder.addMap(ev);
    const parts = [...layouts].map(([id, l]) => `ID ${id}: ${l.bits}비트 ${l.fields.length}필드`);
    this.el.maps.textContent = `${ev.addr} 서비스 ${ev.svc || 0}: Report Map ${ev.len}바이트${ev.truncated ? ' (잘림!)' : ''} · ${parts.join(' / ') || '입력 리포트 없음'}`;
  }

  track(ev) {
    const key = `${ev.addr}|${ev.handle}`;
    let row = this.rows.get(key);
    if (!row) {
      const cells = [ev.addr, ev.handle, `${ev.data.length / 2}바이트`, '0', ''].map((t) => h('td', {}, t));
      row = { tr: h('tr', {}, cells), count: 0, cells };
      this.rows.set(key, row);
      this.el.table.append(row.tr);
    }
    row.count++;
    row.cells[3].textContent = row.count;
    row.cells[4].textContent = ev.data;
  }

  onReport(ev) {
    this.track(ev);
    const dec = this.decoder.decode(ev.addr, ev.handle, hexToBytes(ev.data));
    if (!dec) {
      this.el.fields.textContent = `Report Map이 아직 없어 원본만: ${ev.data}`;
      return;
    }
    const hasKb = dec.fields.some((f) => f.page === PAGE.KEYBOARD);
    const hasMouse = dec.fields.some((f) => (f.page === PAGE.DESKTOP && f.rel) || f.page === PAGE.BUTTON);
    const hasMedia = dec.fields.some((f) => f.page === PAGE.CONSUMER);
    if (hasMouse) this.onMouse(dec);
    if (hasKb) this.onKeyboard(dec);
    if (hasMedia) this.onMedia(dec);
    const lines = dec.fields.filter((f) => f.value)
      .map((f) => usageName(f.page, f.usage) + (f.arr ? ` (배열 값 ${f.value})` : ` = ${f.value}`));
    this.el.fields.textContent = (dec.id ? `리포트 ID ${dec.id}\n` : '') + (lines.join('\n') || '(모두 0, 뗀 상태)');
  }

  onMouse(dec) {
    let dx = 0, dy = 0, wh = 0;
    const buttons = [];
    for (const f of dec.fields) {
      if (f.page === PAGE.DESKTOP && f.rel) {
        if (f.usage === 0x30) dx += f.value;
        else if (f.usage === 0x31) dy += f.value;
        else if (f.usage === 0x38) wh += f.value;
      } else if (f.page === PAGE.BUTTON && f.value) {
        buttons.push({ 1: '왼쪽', 2: '오른쪽', 3: '가운데' }[f.usage] || `버튼${f.usage}`);
      }
    }
    this.mouseCount++;
    this.thisSec++;
    this.totals.dx += dx;
    this.totals.dy += dy;
    this.totals.wh += wh;
    const e = this.el;
    e.btn.textContent = buttons.join(' ') || '-';
    e.dx.textContent = this.totals.dx;
    e.dy.textContent = this.totals.dy;
    e.wh.textContent = this.totals.wh;
    e.count.textContent = this.mouseCount;
    if (dx) this.flash(e.cells.dx);
    if (dy) this.flash(e.cells.dy);
    if (wh) this.flash(e.cells.wh);
    if (buttons.length) this.flash(e.cells.btn);
  }

  union() {
    const all = new Set();
    for (const s of this.kb.values()) for (const u of s) all.add(u);
    return all;
  }

  onKeyboard(dec) {
    const before = this.union();
    this.kb.set(dec.key, pressedUsages(dec.fields, PAGE.KEYBOARD));
    const after = this.union();
    const shift = after.has(0xe1) || after.has(0xe5);
    for (const u of after) {
      if (before.has(u) || (u >= 0xe0 && u <= 0xe7)) continue;
      if (u === 0x2a) this.typed = this.typed.slice(0, -1);
      else this.typed += typeChar(u, shift);
    }
    if (this.typed.length > 2000) this.typed = this.typed.slice(-1500);
    const mods = [...after].filter((u) => u >= 0xe0 && u <= 0xe7);
    const keys = [...after].filter((u) => !(u >= 0xe0 && u <= 0xe7));
    this.el.mods.textContent = mods.map((u) => usageName(PAGE.KEYBOARD, u)).join(' ') || '-';
    this.el.keys.textContent = keys.map((u) => usageName(PAGE.KEYBOARD, u)).join(' ') || '-';
    this.el.text.textContent = this.typed;
    if (keys.length) this.flash(this.el.keys);
  }

  onMedia(dec) {
    const on = dec.fields.filter((f) => f.page === PAGE.CONSUMER && f.value)
      .map((f) => usageName(PAGE.CONSUMER, f.arr && f.umin ? f.umin.usage + (f.value - f.lmin) : f.usage));
    this.el.media.textContent = on.join(' ') || '-';
    if (on.length) this.flash(this.el.media);
  }
}
