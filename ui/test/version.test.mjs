// SPDX-License-Identifier: GPL-3.0-only
/* Firmware version order (the app offers its firmware to an older board). Run: node --test ui/test/ */
import assert from 'node:assert/strict';
import test from 'node:test';

import { compareVersions } from '../src/lib/board/version.js';

test('numeric parts in order, a missing part is 0', () => {
  assert.equal(compareVersions('0.7', '0.7.1'), -1);
  assert.equal(compareVersions('0.7.0', '0.7'), 0);
  assert.equal(compareVersions('0.10', '0.9.9'), 1);
  assert.equal(compareVersions('1.0', '0.99'), 1);
  assert.equal(compareVersions('', '0.1'), -1);
});
