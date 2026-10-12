// SPDX-License-Identifier: GPL-3.0-only
/*
 * UI strings, with i18next. One JSON file per language in ui/src/locales/; English is the default and fills in any
 * string a language lacks. Strings use {name} placeholders filled by t(key, params).
 *
 * Language: ?lang=… in the URL wins; then the language picked at the bottom of the settings window (kept in the
 * app's local storage, so every window of the app uses it); then the first system language there is a file for;
 * English otherwise. Chinese goes to zh-CN (Simplified) or, for Taiwan, Hong Kong, Macau or Hant, zh-TW
 * (Traditional). The choice is fixed when the page loads.
 *
 * Every file has the same keys and placeholders (test/i18n.test.mjs). To add a language: add its file, list it in
 * STRINGS, and in app/src-tauri/Info.plist (CFBundleLocalizations) and tauri.conf.json (NSIS installer languages).
 */
import i18next from 'i18next';
import de from '../../locales/de.json' with { type: 'json' };
import en from '../../locales/en.json' with { type: 'json' };
import es from '../../locales/es.json' with { type: 'json' };
import fr from '../../locales/fr.json' with { type: 'json' };
import ja from '../../locales/ja.json' with { type: 'json' };
import ko from '../../locales/ko.json' with { type: 'json' };
import zhCN from '../../locales/zh-CN.json' with { type: 'json' };
import zhTW from '../../locales/zh-TW.json' with { type: 'json' };

/* In the order the language picker lists them */
export const STRINGS = { en, ko, ja, 'zh-CN': zhCN, 'zh-TW': zhTW, es, de, fr };

/* A language tag (en-US, zh-Hant-TW, …) to one of ours, or '' */
function match(tag) {
  const t = String(tag).toLowerCase();
  if (t.startsWith('zh')) return /-(tw|hk|mo|hant)\b/.test(t) ? 'zh-TW' : 'zh-CN';
  const code = t.split('-')[0];
  return code && Object.hasOwn(STRINGS, code) ? code : '';
}

/* languages: the system's preferred languages, most preferred first (navigator.languages), or one tag */
export function pickLang(search = '', languages = [], saved = '') {
  const q = new URLSearchParams(search).get('lang');
  if (q && Object.hasOwn(STRINGS, q)) return q;
  if (saved && Object.hasOwn(STRINGS, saved)) return saved;
  for (const tag of [].concat(languages)) {
    const m = match(tag);
    if (m) return m;
  }
  return 'en';
}

const SAVED_KEY = 'bb.lang';
const inBrowser = typeof window !== 'undefined';

/* The language picked in the settings window, '' to follow the system */
export function chosenLang() {
  try {
    return (inBrowser && localStorage.getItem(SAVED_KEY)) || '';
  } catch {
    return '';
  }
}

/* Pick a language ('' follows the system). Every window of the app reloads in it */
export function chooseLang(code) {
  try {
    if (code) localStorage.setItem(SAVED_KEY, code);
    else localStorage.removeItem(SAVED_KEY);
  } catch {
    /* storage off: only this load changes */
  }
  location.reload();
}

/* Also runs under Node (tests import modules that use t()), so the browser globals are optional */
export const lang = inBrowser
  ? pickLang(location.search, navigator.languages?.length ? navigator.languages : [navigator.language], chosenLang())
  : 'en';

i18next.init({
  resources: Object.fromEntries(Object.entries(STRINGS).map(([code, table]) => [code, { translation: table }])),
  lng: lang,
  fallbackLng: 'en',
  keySeparator: false,
  nsSeparator: false,
  interpolation: { prefix: '{', suffix: '}', escapeValue: false },
  initAsync: false,
});

/* The other windows (the panel under the menu bar icon) follow a pick made in the settings window */
if (inBrowser) {
  window.addEventListener('storage', (e) => {
    if (e.key === SAVED_KEY) location.reload();
  });
}

/* The string as written, active language first, then English. undefined if the key is unknown */
function lookup(key) {
  return i18next.getResource(lang, 'translation', key) ?? i18next.getResource('en', 'translation', key);
}

const PLACEHOLDER = /\{(\w+)\}/g;

/* t('devices.summary', { n: 2, max: 2 }). Unknown keys come back as the key itself */
/** @returns {string} */
export function t(key, params) {
  return String(i18next.t(key, params));
}

/*
 * Like t(), but a param may be a DOM node (e.g. a live countdown). Returns the text split around the
 * placeholders as an array of strings and param values, ready to pass to h() as children.
 */
export function tParts(key, params = {}) {
  const s = lookup(key) ?? key;
  const out = [];
  let last = 0;
  for (const m of s.matchAll(PLACEHOLDER)) {
    if (!Object.hasOwn(params, m[1])) continue;
    if (m.index > last) out.push(s.slice(last, m.index));
    out.push(params[m[1]]);
    last = m.index + m[0].length;
  }
  if (last < s.length) out.push(s.slice(last));
  return out;
}

/* All keys under a prefix as a plain object, e.g. group('radio.') -> { idle, scan, ... } */
/** @returns {Record<string, string>} */
function group(prefix) {
  const out = {};
  for (const key of Object.keys(en)) {
    if (key.startsWith(prefix)) out[key.slice(prefix.length)] = lookup(key);
  }
  return out;
}

/* Static page text: <html lang>, and every element with data-i18n="key" gets that string */
export function localizePage(doc = document) {
  doc.documentElement.lang = lang;
  for (const el of doc.querySelectorAll('[data-i18n]')) el.textContent = t(el.dataset.i18n);
}

const ERRORS = group('error.');

export function errorText(e) {
  if (!e) return '';
  if (e.name === 'NotFoundError') return t('link.notPicked');
  const code = e.code || e.message;
  if (code === 'error' && e.detail) return e.detail;   /* firmware 0.4 and earlier: generic code with board text */
  return ERRORS[code] || e.detail || e.message || String(e);
}

const STATES = group('state.');

export function stateText(state) {
  return STATES[state] ?? state;
}

export function stateTone(state) {
  if (state === 'ready') return 'ok';
  if (state === 'failed') return 'bad';
  if (state === 'offline') return 'idle';
  return 'busy';
}

const KINDS = group('kind.');

/* Device kind label. Devices we have connected to use the Report Map verdict (kinds); new ones use the advertised appearance */
export function kindText(kinds, appearance) {
  if (kinds && kinds.length) {
    const main = kinds.filter((k) => k === 'keyboard' || k === 'mouse' || k === 'game');
    return (main.length ? main : kinds).map((k) => KINDS[k] || k).join(' · ');
  }
  const a = appearance || 0;
  if (a === 961) return t('appearance.keyboard');
  if (a === 962) return t('appearance.mouse');
  if (a === 963) return t('appearance.joystick');
  if (a === 964) return t('appearance.gamepad');
  if (a >= 960 && a <= 1023) return t('appearance.input');
  return t('appearance.other');
}

export function kindIcon(kinds, appearance) {
  const k = kinds && kinds.length ? kinds : [];
  if (k.includes('keyboard') || appearance === 961) return 'keyboard';
  if (k.includes('mouse') || appearance === 962) return 'mouse';
  if (k.includes('game') || appearance === 963 || appearance === 964) return 'game';
  return 'device';
}

export const RADIO = group('radio.');

export const FAIL_STAGE = group('fail.');

/* The HCI disconnect reasons we commonly see */
export function reasonText(reason) {
  if (reason === 531 || reason === 0x13) return t('reason.remote');
  if (reason === 520 || reason === 0x08) return t('reason.timeout');
  if (reason === 534 || reason === 22) return t('reason.local');
  return '';
}

const HOST_STATES = group('hostState.');

export function hostStateText(state) {
  return HOST_STATES[state] ?? state;
}

export function hostStateTone(state) {
  if (state === 'ready') return 'ok';
  if (state === 'offline') return 'idle';
  return 'busy';
}
