// SPDX-License-Identifier: GPL-3.0-only
/* Small DOM helpers. Text always goes in through textContent (safe even if a device name contains HTML) */

export function h(tag, props = {}, ...children) {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(props || {})) {
    if (v === undefined || v === null || v === false) continue;
    if (k === 'class') el.className = v;
    else if (k === 'text') el.textContent = v;
    else if (k.startsWith('on')) el.addEventListener(k.slice(2), v);
    else if (k === 'dataset') Object.assign(el.dataset, v);
    else if (v === true) el.setAttribute(k, '');
    else el.setAttribute(k, v);
  }
  for (const c of children.flat()) {
    if (c === null || c === undefined || c === false) continue;
    el.append(c instanceof Node ? c : document.createTextNode(String(c)));
  }
  return el;
}

/* Redraw a region while keeping the focus and value of the field being typed in */
export function mount(container, node) {
  const active = document.activeElement;
  const keep = active && container.contains(active) && active.dataset.keep ? active.dataset.keep : null;
  const value = keep ? active.value : null;
  container.replaceChildren(node);
  if (keep) {
    const el = container.querySelector(`[data-keep="${keep}"]`);
    if (el) {
      el.value = value;
      el.focus();
    }
  }
}

const ICONS = {
  keyboard: 'M3 7h18v10H3z M6 10h1 M9 10h1 M12 10h1 M15 10h1 M18 10h1 M7 14h10',
  mouse: 'M12 3a5 5 0 0 1 5 5v8a5 5 0 0 1-10 0V8a5 5 0 0 1 5-5z M12 3v6',
  game: 'M6 9h12a3 3 0 0 1 3 3v2a3 3 0 0 1-5 2l-1-1H9l-1 1a3 3 0 0 1-5-2v-2a3 3 0 0 1 3-3z M8 11v3 M6.5 12.5h3',
  device: 'M7 3h10v18H7z M11 18h2',
  usb: 'M12 3v14 M9 6l3-3 3 3 M8 11v2h8v-2 M12 17a2 2 0 1 0 0 4 2 2 0 0 0 0-4z',
  plus: 'M12 5v14 M5 12h14',
  lock: 'M6 11h12v9H6z M8 11V8a4 4 0 0 1 8 0v3',
  computer: 'M3 5h18v11H3z M9 20h6 M12 16v4',
  arrow: 'M5 12h14 M13 6l6 6-6 6',
};

/* Small line icons (they take the text color) */
export function icon(name, size = 18) {
  const ns = 'http://www.w3.org/2000/svg';
  const svg = document.createElementNS(ns, 'svg');
  svg.setAttribute('viewBox', '0 0 24 24');
  svg.setAttribute('width', size);
  svg.setAttribute('height', size);
  svg.setAttribute('aria-hidden', 'true');
  svg.classList.add('icon');
  const path = document.createElementNS(ns, 'path');
  path.setAttribute('d', ICONS[name] || ICONS.device);
  svg.append(path);
  return svg;
}
