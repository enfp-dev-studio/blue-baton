// SPDX-License-Identifier: GPL-3.0-only
/*
 * Host switch combos: from a key press in the settings window to the firmware's combo text (PROTOCOL.md,
 * "Host switch combos"), and back to labels. Keys are taken by physical position (KeyboardEvent.code), as the
 * board sees them (HID usages), so the keyboard layout does not matter.
 */

const NAMED = {
  Space: 'space', Enter: 'enter', NumpadEnter: 'enter', Escape: 'esc', Tab: 'tab', Backspace: 'backspace',
  Minus: 'minus', Equal: 'equal', Backquote: 'grave', Home: 'home', End: 'end', PageUp: 'pageup', PageDown: 'pagedown',
  Insert: 'insert', Delete: 'delete', ArrowLeft: 'left', ArrowRight: 'right', ArrowUp: 'up', ArrowDown: 'down',
  ScrollLock: 'scrolllock', Pause: 'pause', PrintScreen: 'printscreen', CapsLock: 'capslock',
};

const MODIFIER_CODES = new Set(['ControlLeft', 'ControlRight', 'AltLeft', 'AltRight', 'ShiftLeft', 'ShiftRight',
  'MetaLeft', 'MetaRight', 'OSLeft', 'OSRight']);

export const DEFAULT_COMBOS = ['ctrl+alt+1', 'ctrl+alt+2', 'ctrl+alt+3'];

/* The firmware name of a non-modifier key, or '' if combos cannot use it */
export function keyName(code) {
  let m = /^Key([A-Z])$/.exec(code);
  if (m) return m[1].toLowerCase();
  m = /^Digit([0-9])$/.exec(code);
  if (m) return m[1];
  m = /^F([1-9]|1[0-9]|2[0-4])$/.exec(code);
  if (m) return 'f' + m[1];
  return NAMED[code] || '';
}

export function isModifier(code) {
  return MODIFIER_CODES.has(code);
}

/* The modifiers held in a key event, in the order the firmware text uses */
export function modifiers(e) {
  return [e.ctrlKey && 'ctrl', e.altKey && 'alt', e.shiftKey && 'shift', e.metaKey && 'cmd'].filter(Boolean);
}

/*
 * A key press while recording: { combo } when it makes a valid combo, { error } when not ('modifier': a key
 * alone would steal normal typing; 'key': a key combos cannot use), or {} for a modifier on its own.
 */
export function comboFromEvent(e) {
  if (isModifier(e.code)) return {};
  const key = keyName(e.code);
  if (!key) return { error: 'key' };
  const mods = modifiers(e);
  if (!mods.length) return { error: 'modifier' };
  return { combo: [...mods, key].join('+') };
}

/* Same combo regardless of how it is spelled (control/ctrl, option/alt, order of modifiers) */
export function sameCombo(a, b) {
  const norm = (c) => {
    const alias = { control: 'ctrl', option: 'alt', opt: 'alt', command: 'cmd', gui: 'cmd', win: 'cmd', meta: 'cmd',
      super: 'cmd', return: 'enter', escape: 'esc', backquote: 'grave' };
    const parts = (c || '').toLowerCase().split('+').map((k) => alias[k.trim()] || k.trim()).filter(Boolean);
    return parts.sort().join('+');
  };
  return !!a && !!b && norm(a) === norm(b);
}
