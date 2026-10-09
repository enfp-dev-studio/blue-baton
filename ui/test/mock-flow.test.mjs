/*
 * Protocol client + mock board integration tests (no browser). Run: node --test ui/test/
 * Sends commands through the same BoardClient the UI uses and checks replies and events in the firmware 0.6 format.
 */
import assert from 'node:assert/strict';
import test from 'node:test';

import { BoardClient } from '../src/lib/board/protocol.js';
import { MockTransport } from '../src/lib/board/transport/mock.js';

function setup(params = '') {
  const t = new MockTransport(new URLSearchParams(params));
  const c = new BoardClient(t);
  const events = [];
  c.on((ev) => {
    if (ev.event && !ev.event.startsWith('_')) events.push(ev);
  });
  const waitFor = (pred, ms = 8000) =>
    new Promise((resolve, reject) => {
      const start = Date.now();
      const tick = () => {
        const hit = events.find(pred);
        if (hit) return resolve(hit);
        if (Date.now() - start > ms) return reject(new Error('timeout waiting for event'));
        setTimeout(tick, 20);
      };
      tick();
    });
  return { t, c, events, waitFor };
}

test('no lock: commands work right after hello', async () => {
  const { t, c } = setup();
  await t.open();
  const hello = await c.request('hello');
  assert.equal(hello.fw, 'blue-baton');
  assert.equal('unlocked' in hello, false);
  const pl = await c.request('paired_list');
  assert.equal(pl.paired.length, 2);
  await assert.rejects(c.request('auth', { pin: '0000' }), (e) => e.code === 'unknown_cmd');
  await t.close();
});

test('input content only under a monitor lease; activity always', async () => {
  const { t, c, events, waitFor } = setup();
  await t.open();
  await waitFor((e) => e.event === 'activity' && e.addr === '00:00:5E:00:53:17');
  assert.equal(events.some((e) => e.event === 'report'), false, 'no reports without a lease');
  const r = await c.request('monitor', { on: true });
  assert.equal(r.lease_ms, 60000);
  await waitFor((e) => e.event === 'report' && e.addr === '00:00:5E:00:53:17');
  await c.request('monitor', { on: false });
  await new Promise((r) => setTimeout(r, 50));
  const n = events.filter((e) => e.event === 'report').length;
  await new Promise((r) => setTimeout(r, 400));
  assert.equal(events.filter((e) => e.event === 'report').length, n, 'no reports after the lease ends');
  /* activity is throttled to one per 250 ms per device */
  const acts = events.filter((e) => e.event === 'activity' && e.addr === '00:00:5E:00:53:17');
  assert.ok(acts.length >= 1);
  await assert.rejects(c.request('monitor', {}), (e) => e.code === 'bad_request');
  await t.close();
});

test('bonded devices reconnect by themselves with stored keys', async () => {
  const { t, waitFor } = setup();
  await t.open();
  await waitFor((e) => e.event === 'autoconnect');
  const enc = await waitFor((e) => e.event === 'encrypted' && e.addr === '00:00:5E:00:53:17');
  assert.equal(enc.bond, 'restored');
  await waitFor((e) => e.event === 'hid_ready' && e.addr === '00:00:5E:00:53:22');
  await t.close();
});

test('slots full -> no_slot, then numeric comparison pairing after freeing one', async () => {
  const { t, c, waitFor } = setup('fast');
  await t.open();
  await waitFor((e) => e.event === 'hid_ready' && e.addr === '00:00:5E:00:53:22');
  await c.request('pair_scan', { on: true });
  await waitFor((e) => e.event === 'device_found' && e.addr === 'C0:FF:EE:00:00:01');
  await assert.rejects(c.request('pair_connect', { addr: 'C0:FF:EE:00:00:01' }), (e) => e.code === 'no_slot');
  await c.request('disconnect', { addr: '00:00:5E:00:53:17' });
  await c.request('pair_connect', { addr: 'C0:FF:EE:00:00:01' });
  const nc = await waitFor((e) => e.event === 'numcmp');
  assert.match(nc.code, /^\d{6}$/);
  await c.request('pair_accept', { accept: true });
  const paired = await waitFor((e) => e.event === 'paired' && e.addr === 'C0:FF:EE:00:00:01');
  assert.equal(paired.bond, 'new');
  const pl = await c.request('paired_list');
  assert.equal(pl.paired.length, 3);
  /* A mouse the user disconnected is dropped from auto-reconnect */
  assert.equal(pl.paired.find((p) => p.addr === '00:00:5E:00:53:17').auto, false);
  await t.close();
});

test('wrong passkey fails with a security failure, then unknown command and bad state errors', async () => {
  const { t, c, waitFor } = setup('fast');
  await t.open();
  await waitFor((e) => e.event === 'hid_ready' && e.addr === '00:00:5E:00:53:22');
  await c.request('disconnect', { addr: '00:00:5E:00:53:22' });
  await c.request('pair_scan', { on: true });
  await waitFor((e) => e.event === 'device_found' && e.addr === 'C0:FF:EE:00:00:02');
  await c.request('pair_connect', { addr: 'C0:FF:EE:00:00:02' });
  await waitFor((e) => e.event === 'passkey_request');
  await c.request('passkey', { code: '000000' });
  const failed = await waitFor((e) => e.event === 'failed');
  assert.equal(failed.stage, 'security');
  await assert.rejects(c.request('pair_accept', { accept: true }), (e) => e.code === 'no_request');
  await assert.rejects(c.request('nope'), (e) => e.code === 'unknown_cmd');
  await t.close();
});

test('connect on a paired offline device resumes auto-connect instead of a direct attempt', async () => {
  const { t, c, events, waitFor } = setup();
  await t.open();
  await waitFor((e) => e.event === 'hid_ready' && e.addr === '00:00:5E:00:53:17');
  await c.request('disconnect', { addr: '00:00:5E:00:53:17' });
  let pl = await c.request('paired_list');
  assert.equal(pl.paired.find((p) => p.addr === '00:00:5E:00:53:17').auto, false);
  const before = events.length;
  await c.request('pair_connect', { addr: '00:00:5E:00:53:17' });
  pl = await c.request('paired_list');
  assert.equal(pl.paired.find((p) => p.addr === '00:00:5E:00:53:17').auto, true);
  const conn = await waitFor((e, i) => e.event === 'connected' && e.addr === '00:00:5E:00:53:17' && events.indexOf(e) >= before);
  assert.equal(conn.user, false);
  assert.ok(!events.slice(before).some((e) => e.event === 'connecting'), 'no direct connecting attempt');
  await t.close();
});

test('remove a pairing', async () => {
  const { t, c, waitFor } = setup();
  await t.open();
  await c.request('paired_remove', { addr: '00:00:5E:00:53:17' });
  await waitFor((e) => e.event === 'unpaired' && e.id_addr === '00:00:5E:00:53:17');
  const pl = await c.request('paired_list');
  assert.deepEqual(pl.paired.map((p) => p.addr), ['00:00:5E:00:53:22']);
  await assert.rejects(c.request('paired_remove', { addr: '00:00:5E:00:53:17' }), (e) => e.code === 'not_found');
  await t.close();
});

test('host pairing: window, passkey typed in the GUI, then the host shows up as ready', async () => {
  const lines = [];
  const { t, c, waitFor } = setup();
  const onLine = t.onLine;
  await t.open();
  t.onLine = (line) => {
    lines.push(line);
    onLine(line);
  };
  await waitFor((e) => e.event === 'host_ready' && e.host === 1);
  await c.request('host_pair', { on: true });
  await waitFor((e) => e.event === 'host_window' && e.open === true);
  await assert.rejects(c.request('pair_scan', { on: true }), (e) => e.code === 'host_window');
  const req = await waitFor((e) => e.event === 'host_passkey_request');
  const code = lines.map((l) => /code shown on the mock PC: (\d{6})/.exec(l)).find(Boolean)[1];
  await assert.rejects(c.request('host_passkey', { code: '12' }), (e) => e.code === 'bad_code');
  await c.request('host_passkey', { code });
  const paired = await waitFor((e) => e.event === 'host_paired');
  assert.equal(paired.host, 2);
  assert.equal(paired.addr, req.addr);
  await waitFor((e) => e.event === 'host_window' && e.open === false);
  await waitFor((e) => e.event === 'host_name' && e.host === 2);
  const hl = await c.request('host_list');
  assert.deepEqual(hl.hosts.map((h) => [h.host, h.state]), [[1, 'ready'], [2, 'ready']]);
  await t.close();
});

test('host pairing with a wrong passkey fails, and host_remove frees the slot', async () => {
  const { t, c, waitFor } = setup();
  await t.open();
  await c.request('host_pair', { on: true });
  await waitFor((e) => e.event === 'host_passkey_request');
  await c.request('host_passkey', { code: '000000' });
  await waitFor((e) => e.event === 'host_pair_failed');
  await assert.rejects(c.request('host_passkey', { code: '000000' }), (e) => e.code === 'no_request');
  await c.request('host_pair', { on: false });
  await c.request('host_remove', { host: 1 });
  await waitFor((e) => e.event === 'host_removed' && e.host === 1);
  assert.deepEqual((await c.request('host_list')).hosts, []);
  await assert.rejects(c.request('host_remove', { host: 1 }), (e) => e.code === 'not_found');
  await t.close();
});
