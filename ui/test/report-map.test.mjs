/* UI-side Report Map parsing tests. Run: node --test firmware/ui/test/ */
import assert from 'node:assert/strict';
import test from 'node:test';

import { PAGE, ReportDecoder, parseReportMap, pressedUsages } from '../src/lib/board/hid/report-map.js';

/* Report Maps from real hardware (Xiaoxin Mplus 5.0 mouse, ABKO AK84BT-1 keyboard) */
const MOUSE_MAP =
  '05010902A10185010901A1000509190129051500250195057501810295017503810105010930093116018026FF7F75109502' +
  '810609381581257F750895018106C0C0';
const KB_MAP =
  '05010902A10185010901A100050919012905150025019505750181029501750381010501093009311581257F750895028106' +
  '09381581257F750895018106050C0A38021581257F750895018106C0C0050C0901A101850275109501150126FF0319012AFF' +
  '038160C005010906A1018503050719E029E71500250175019508810295017508810195057501050819012905910295017503' +
  '910195067508150025E70507190029E78100C005010906A101850475019578050715002501190029788102C0050C0901A101' +
  '85051500250175019503810175019501050C09B8810206FF0009038102750195038101C0';

const bytes = (hex) => Uint8Array.from(hex.match(/../g).map((x) => parseInt(x, 16)));

function decoderFor(addr, map, refs) {
  const d = new ReportDecoder();
  d.addMap({ addr, svc: 0, hex: map });
  for (const [val, id] of refs) d.addRef({ addr, val, id, type: 1, svc: 0 });
  return d;
}

const field = (dec, page, usage) => dec.fields.find((f) => f.page === page && f.usage === usage);

test('real mouse: buttons, 16-bit X/Y (signed), wheel', () => {
  const d = decoderFor('M', MOUSE_MAP, [[25, 1]]);
  /* left button, X=+5, Y=-5, wheel +1 */
  const dec = d.decode('M', 25, bytes('010500FBFF01'));
  assert.equal(field(dec, PAGE.BUTTON, 1).value, 1);
  assert.equal(field(dec, PAGE.DESKTOP, 0x30).value, 5);
  assert.equal(field(dec, PAGE.DESKTOP, 0x31).value, -5);
  assert.equal(field(dec, PAGE.DESKTOP, 0x38).value, 1);
});

test('real keyboard: five input reports with the expected sizes', () => {
  const layouts = parseReportMap(bytes(KB_MAP));
  assert.deepEqual([...layouts.keys()].sort(), [1, 2, 3, 4, 5]);
  assert.equal(layouts.get(4).bits, 120);   /* 15-byte NKRO bitmap */
  assert.equal(layouts.get(3).bits, 64);    /* modifiers + reserved + 6 keys */
});

test('real keyboard: NKRO bitmap bit index is the usage', () => {
  const d = decoderFor('K', KB_MAP, [[72, 3], [80, 4]]);
  const data = new Uint8Array(15);
  data[0x0b >> 3] |= 1 << (0x0b & 7);       /* 'h' = 0x0B */
  data[0x2c >> 3] |= 1 << (0x2c & 7);       /* Space */
  const dec = d.decode('K', 80, data);
  assert.deepEqual([...pressedUsages(dec.fields, PAGE.KEYBOARD)].sort((a, b) => a - b), [0x0b, 0x2c]);
});

test('real keyboard: 6-key array report with modifier bits', () => {
  const d = decoderFor('K', KB_MAP, [[72, 3]]);
  /* LShift + 'a' */
  const dec = d.decode('K', 72, bytes('0200040000000000'));
  assert.deepEqual([...pressedUsages(dec.fields, PAGE.KEYBOARD)].sort((a, b) => a - b), [0x04, 0xe1]);
});

test('real keyboard: consumer control array gives the usage (volume up)', () => {
  const d = decoderFor('K', KB_MAP, [[68, 2]]);
  const dec = d.decode('K', 68, bytes('E900'));
  const f = dec.fields.find((x) => x.page === PAGE.CONSUMER && x.value);
  assert.equal(f.umin.usage + (f.value - f.lmin), 0xe9);
});

test('report id byte in payload is stripped only when it is really there', () => {
  const d = decoderFor('M', MOUSE_MAP, [[25, 1]]);
  const withId = d.decode('M', 25, bytes('01010500FBFF01'));
  assert.equal(field(withId, PAGE.DESKTOP, 0x30).value, 5);
});

test('fewer usages than report count: the last usage repeats (HID 1.11 6.2.2.8)', () => {
  /* Usage X, Report Count 3 -> all three fields are X */
  const layouts = parseReportMap(bytes('05010902A101093015002501750195038102C0'));
  const fields = layouts.get(0).fields;
  assert.equal(fields.length, 3);
  assert.ok(fields.every((f) => f.usage === 0x30));
});

test('push/pop restores global state', () => {
  /* Push, change Report Size, Pop: Report Size must be back to 8 */
  const layouts = parseReportMap(bytes('05010902A10175089501A47501B409308102C0'));
  assert.equal(layouts.get(0).fields[0].size, 8);
});

test('broken map does not throw', () => {
  assert.doesNotThrow(() => parseReportMap(bytes('0501090')));
  assert.doesNotThrow(() => parseReportMap(bytes('FE05')));
});
