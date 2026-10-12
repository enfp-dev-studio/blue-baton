/* UI string tables: every language has the same keys and placeholders. Run: node --test ui/test/ */
import assert from 'node:assert/strict';
import test from 'node:test';

import { STRINGS, pickLang, t, tParts } from '../src/lib/board/i18n.js';

const placeholders = (s) => [...s.matchAll(/\{(\w+)\}/g)].map((m) => m[1]).sort();

test('all languages have the same keys as English', () => {
  const keys = Object.keys(STRINGS.en).sort();
  for (const [lang, table] of Object.entries(STRINGS)) {
    assert.deepEqual(Object.keys(table).sort(), keys, `keys of ${lang}`);
  }
});

test('all languages use the same placeholders per key', () => {
  for (const [lang, table] of Object.entries(STRINGS)) {
    for (const [key, en] of Object.entries(STRINGS.en)) {
      assert.deepEqual(placeholders(table[key]), placeholders(en), `${lang} ${key}`);
    }
  }
});

test('the English table has no Hangul', () => {
  for (const [key, s] of Object.entries(STRINGS.en)) assert.doesNotMatch(s, /[\uAC00-\uD7A3]/, key);
});

test('language choice: ?lang wins, then the browser language, default English', () => {
  assert.equal(pickLang('?lang=ko', 'en-US'), 'ko');
  assert.equal(pickLang('?mock&lang=en', 'ko-KR'), 'en');
  assert.equal(pickLang('?lang=fr', 'ko-KR'), 'ko');
  assert.equal(pickLang('', 'ko'), 'ko');
  assert.equal(pickLang('', 'en-GB'), 'en');
  assert.equal(pickLang('', ''), 'en');
  assert.equal(pickLang('?lang=constructor', ''), 'en');
});

test('t fills placeholders, tParts keeps non-string params in place', () => {
  assert.equal(t('no.such.key'), 'no.such.key');
  const node = { countdown: true };
  const parts = tParts('add.scanning', { seconds: node });
  assert.ok(parts.includes(node));
  assert.equal(parts.map((p) => (p === node ? '42' : p)).join(''), t('add.scanning', { seconds: 42 }));
});
