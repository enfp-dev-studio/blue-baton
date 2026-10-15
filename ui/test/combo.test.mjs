// SPDX-License-Identifier: GPL-3.0-only
/* Recording a switch combo from key presses. Run: node --test ui/test/ */
import assert from 'node:assert/strict';
import test from 'node:test';

import { comboFromEvent, isDefaultCombos, keyName, sameCombo } from '../src/lib/board/combo.js';

const press = (code, mods = {}) => ({ code, ctrlKey: false, altKey: false, shiftKey: false, metaKey: false, ...mods });

test('a modifier plus a key makes a combo in firmware order', () => {
  assert.deepEqual(comboFromEvent(press('Digit1', { ctrlKey: true, altKey: true })), { combo: 'ctrl+alt+1' });
  assert.deepEqual(comboFromEvent(press('KeyK', { metaKey: true, shiftKey: true })), { combo: 'shift+cmd+k' });
  assert.deepEqual(comboFromEvent(press('F5', { ctrlKey: true })), { combo: 'ctrl+f5' });
  assert.deepEqual(comboFromEvent(press('ArrowLeft', { altKey: true })), { combo: 'alt+left' });
});

test('a key alone, an unusable key, and a modifier alone', () => {
  assert.deepEqual(comboFromEvent(press('KeyA')), { error: 'modifier' });
  assert.deepEqual(comboFromEvent(press('Semicolon', { ctrlKey: true })), { error: 'key' });
  assert.deepEqual(comboFromEvent(press('ControlLeft', { ctrlKey: true })), {});
});

test('key names match the firmware table', () => {
  assert.equal(keyName('Backquote'), 'grave');
  assert.equal(keyName('NumpadEnter'), 'enter');
  assert.equal(keyName('F24'), 'f24');
  assert.equal(keyName('F25'), '');
});

test('the same combo in another spelling', () => {
  assert.ok(sameCombo('ctrl+alt+1', 'alt+control+1'));
  assert.ok(sameCombo('option+cmd+escape', 'cmd+alt+esc'));
  assert.ok(!sameCombo('ctrl+alt+1', 'ctrl+alt+2'));
  assert.ok(!sameCombo('', ''));
});

test('combos set back to the defaults count as default (the reset link hides)', () => {
  assert.equal(isDefaultCombos(['alt+1', 'alt+2', 'alt+3'], false), true);
  assert.equal(isDefaultCombos(['option+1', 'opt+2', 'alt+3'], false), true);
  assert.equal(isDefaultCombos(['alt+1', 'alt+2', 'alt+4'], false), false);
  assert.equal(isDefaultCombos(['alt+1', 'alt+2'], false), false);
  assert.equal(isDefaultCombos(['ctrl+alt+1', 'ctrl+alt+2', 'ctrl+alt+3'], false), false);
  assert.equal(isDefaultCombos(['ctrl+alt+1', 'ctrl+alt+2', 'ctrl+alt+3'], true), true);
});
