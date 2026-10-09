/*
 * Input monitor (developer area). Input reports arrive dozens of times per second, so instead of redrawing
 * the page we update individual cells. Decoding follows the Report Map (hid/report-map.js).
 */

import { h } from './dom.js';
import { PAGE, ReportDecoder, hexToBytes, pressedUsages, typeChar, usageName } from './hid/report-map.js';
import { t, tParts } from './i18n.js';

export class InputMonitor {
  constructor(root) {
    this.decoder = new ReportDecoder();
    this.kb = new Map();       /* report key -> set of pressed keys */
    this.typed = '';
    this.totals = { dx: 0, dy: 0, wh: 0 };
    this.mouseCount = 0;
    this.thisSec = 0;
    this.rows = new Map();     /* device|handle -> table row */
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
    const [cBtn, vBtn] = cell(t('inputs.buttons'), 'btn');
    const [cDx, vDx] = cell(t('inputs.dx'), 'dx');
    const [cDy, vDy] = cell(t('inputs.dy'), 'dy');
    const [cWh, vWh] = cell(t('inputs.wheel'), 'wh');
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
      maps: h('p', { class: 'hint' }, t('inputs.mapWaiting')),
    };
    const e = this.el;
    root.replaceChildren(
      h('div', { class: 'panel-head' }, h('h2', {}, t('inputs.title')), h('span', { class: 'hint' }, t('inputs.hint'))),
      h('h3', {}, t('inputs.mouse')),
      h('div', { class: 'grid4' }, cBtn, cDx, cDy, cWh),
      h('p', { class: 'hint' }, tParts('inputs.mouseStats', { count: e.count, rate: e.rate }), ' ',
        h('button', { class: 'ghost small', onclick: () => this.resetMouse() }, t('inputs.resetMouse'))),
      h('h3', {}, t('inputs.keyboard')),
      h('p', { class: 'hint' }, tParts('inputs.keyStats', { mods: e.mods, keys: e.keys, media: e.media })),
      e.text,
      h('button', { class: 'ghost small', onclick: () => this.resetKeys() }, t('inputs.resetKeys')),
      h('details', { class: 'sub' },
        h('summary', {}, t('inputs.details')),
        e.maps,
        h('table', { class: 'rtab' },
          h('thead', {}, h('tr', {}, ['inputs.colDevice', 'inputs.colHandle', 'inputs.colLength', 'inputs.colCount', 'inputs.colLast'].map((k) => h('th', {}, t(k))))),
          e.table),
        h('p', { class: 'hint' }, t('inputs.fieldsHint')),
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

  /* Board events. Only the ones we care about are handled */
  onEvent(ev) {
    if (ev.event === 'report_ref') this.decoder.addRef(ev);
    else if (ev.event === 'report_map') this.onMap(ev);
    else if (ev.event === 'report') this.onReport(ev);
    else if (ev.event === 'unpaired') this.decoder.forget(ev.id_addr);
  }

  onMap(ev) {
    const layouts = this.decoder.addMap(ev);
    const parts = [...layouts].map(([id, l]) => t('inputs.layout', { id, bits: l.bits, fields: l.fields.length }));
    this.el.maps.textContent = t('inputs.mapInfo', {
      addr: ev.addr,
      svc: ev.svc || 0,
      len: ev.len,
      truncated: ev.truncated ? t('inputs.truncated') : '',
      layouts: parts.join(' / ') || t('inputs.noInputReports'),
    });
  }

  track(ev) {
    const key = `${ev.addr}|${ev.handle}`;
    let row = this.rows.get(key);
    if (!row) {
      const cells = [ev.addr, ev.handle, t('inputs.bytes', { n: ev.data.length / 2 }), '0', ''].map((x) => h('td', {}, x));
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
      this.el.fields.textContent = t('inputs.rawOnly', { data: ev.data });
      return;
    }
    const hasKb = dec.fields.some((f) => f.page === PAGE.KEYBOARD);
    const hasMouse = dec.fields.some((f) => (f.page === PAGE.DESKTOP && f.rel) || f.page === PAGE.BUTTON);
    const hasMedia = dec.fields.some((f) => f.page === PAGE.CONSUMER);
    if (hasMouse) this.onMouse(dec);
    if (hasKb) this.onKeyboard(dec);
    if (hasMedia) this.onMedia(dec);
    const lines = dec.fields.filter((f) => f.value)
      .map((f) => usageName(f.page, f.usage) + (f.arr ? t('inputs.arrayValue', { value: f.value }) : ` = ${f.value}`));
    this.el.fields.textContent = (dec.id ? t('inputs.reportId', { id: dec.id }) + '\n' : '') + (lines.join('\n') || t('inputs.allZero'));
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
        buttons.push({ 1: t('inputs.left'), 2: t('inputs.right'), 3: t('inputs.middle') }[f.usage] || t('usage.button', { n: f.usage }));
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
