#!/usr/bin/env python3
"""
Watch the board through the menu bar app's developer API or tools/bridge.py (same HTTP interface).

  watch.py links         One line per input-device connection: interval, connection parameter requests and
                         whether the controller took them, and how the connection ended.
  watch.py stats [N]     Reset the board's latency statistics, wait for N activity events (default 40, about
                         10 s of use; idle gaps don't count), then print the stats reply.
  watch.py soak FILE     Every 60 s, append one CSV row per host link with traffic: reports, reports the stack
                         could not take, completion-time counts (developer builds, CONFIG_KM_DIAG). Runs until
                         stopped; minutes without traffic leave no row.
  watch.py alerts [FILE] Print a line when something goes wrong: a host's report took over 150 ms to
                         complete or was dropped, a link dropped, or the board restarted. With FILE (the CSV a
                         running soak writes), it reads that instead of polling stats, since every stats read
                         resets the counters and two pollers would split them.
  watch.py report FILE   Per host, from a soak CSV: share completed within 45 ms (target: 99%),
                         minutes with a report over 150 ms, dropped reports.

  --url URL              Default http://127.0.0.1:8766

Intervals are in units of 1.25 ms (6 = 7.5 ms, 7 = 8.75 ms, 12 = 15 ms), as in firmware/PROTOCOL.md.
Standard library only.
"""
import csv
import json
import os
import re
import sys
import time
import urllib.request

URL = 'http://127.0.0.1:8766'


def events():
    """Board lines from /events, forever (reconnects when the app or bridge restarts the stream)"""
    while True:
        try:
            with urllib.request.urlopen(URL + '/events', timeout=3600) as r:
                for raw in r:
                    line = raw.decode('utf-8', 'replace')
                    if line.startswith('data: '):
                        yield line[6:].rstrip('\n')
        except OSError:
            time.sleep(2)


def board_event(line):
    m = re.search(r'KM< (\{.*\})', line)
    if not m:
        return None
    try:
        return json.loads(m.group(1))
    except ValueError:
        return None


def send(cmd):
    req = urllib.request.Request(URL + '/send', data=json.dumps(cmd).encode(), method='POST')
    urllib.request.urlopen(req, timeout=5).read()


def ms(itvl):
    return f'{itvl * 1.25:g} ms' if isinstance(itvl, int) else '?'


def links():
    start = None
    inputs = 0
    for line in events():
        now = time.time()
        if 'ocf=0x0013' in line and start:
            print(f'  {now - start:5.1f}s  controller refused the change (HCI LE Connection Update)', flush=True)
            continue
        e = board_event(line)
        if not e or 'event' not in e:
            continue
        ev = e['event']
        if ev == 'connected':
            start, inputs = now, 0
            print(f'connected at {ms(e.get("itvl"))}', flush=True)
        elif not start:
            continue
        elif ev == 'activity':
            inputs += 1
        elif ev == 'hid_ready':
            print(f'  {now - start:5.1f}s  ready', flush=True)
        elif ev == 'conn_params_req':
            lo, hi = e.get('itvl_min'), e.get('itvl_max')
            span = ms(lo) if lo == hi else f'{ms(lo)}-{ms(hi)}'
            print(f'  {now - start:5.1f}s  device asks for {span}, latency {e.get("latency")}, '
                  f'timeout {e.get("timeout", 0) * 10} ms', flush=True)
        elif ev == 'conn_params':
            print(f'  {now - start:5.1f}s  now {ms(e.get("itvl"))}, latency {e.get("latency")} '
                  f'(status {e.get("status")})', flush=True)
        elif ev == 'disconnected':
            print(f'  {now - start:5.1f}s  disconnected, reason {e.get("reason")}, {inputs} activity events',
                  flush=True)
            start = None


def stats(n):
    rid = int(time.time()) % 100000 + 900000
    send({'id': rid, 'cmd': 'stats'})   # the first read resets the counters
    seen = 0
    stream = events()
    for line in stream:
        e = board_event(line)
        if e and e.get('event') == 'activity':
            seen += 1
            if seen >= n:
                break
    send({'id': rid + 1, 'cmd': 'stats'})
    for line in stream:
        e = board_event(line)
        if e and e.get('id') == rid + 1:
            print(json.dumps(e.get('stats'), indent=2))
            return


ACK_EDGES = ['<15', '15-30', '30-45', '45-60', '60-90', '90-150', '150-300', '300+']


def stats_replies(period):
    """Every period seconds, send stats and yield its reply (the first read resets the counters)"""
    rid = int(time.time()) % 100000 + 700000
    send({'id': rid, 'cmd': 'stats'})
    stream = events()
    deadline = time.time() + period
    want = None
    for line in stream:
        e = board_event(line)
        if want and e and e.get('id') == want and 'stats' in e:
            yield e['stats']
            want = None
        if time.time() >= deadline:
            rid += 1
            want = rid
            send({'id': rid, 'cmd': 'stats'})
            deadline = time.time() + period


def soak(path):
    new = not os.path.exists(path)
    with open(path, 'a', newline='') as fh:
        w = csv.writer(fh)
        if new:
            w.writerow(['time', 'host', 'latency', 'rssi', 'mouse', 'dropped'] + ['ack_' + b for b in ACK_EDGES] +
                       ['ack_max_ms'])
        for st in stats_replies(60):
            for h in st.get('hosts', []):
                a = h.get('ack_ms')
                if not a or (not sum(a) and not h.get('mouse_reports')):
                    continue
                w.writerow([int(time.time()), h['host'], h.get('latency'), h.get('rssi'), h.get('mouse_reports'),
                            h.get('nobuf_mouse', 0) + h.get('nobuf_other', 0)] + a + [h.get('ack_max_ms')])
            fh.flush()


def alerts(path=None):
    import threading

    def warn(host, over150, ack_max, dropped, mouse):
        if dropped or over150:
            print(f"{time.strftime('%H:%M')} host {host}: {over150} reports over 150 ms (max {ack_max} ms), "
                  f"{dropped} dropped, of {mouse} mouse", flush=True)

    def poll():
        for st in stats_replies(60):
            for h in st.get('hosts', []):
                a = h.get('ack_ms') or [0] * 8
                warn(h['host'], a[6] + a[7], h.get('ack_max_ms'), h.get('nobuf_mouse', 0) + h.get('nobuf_other', 0),
                     h.get('mouse_reports'))

    def tail():
        with open(path) as fh:
            fh.seek(0, 2)
            while True:
                line = fh.readline()
                if not line:
                    time.sleep(5)
                    continue
                r = line.strip().split(',')
                if len(r) >= 15 and r[0].isdigit():
                    warn(r[1], int(r[12]) + int(r[13]), r[14], int(r[5]), r[4])

    threading.Thread(target=tail if path else poll, daemon=True).start()
    for line in events():
        if re.search(r'rst:|Guru|panic|never completed', line):
            print(f"{time.strftime('%H:%M')} board: {line.strip()[:120]}", flush=True)
            continue
        e = board_event(line)
        if e and e.get('event') in ('host_disconnected', 'disconnected', 'failed'):
            who = f"host {e['host']}" if 'host' in e else 'input device'
            print(f"{time.strftime('%H:%M')} {who} {e['event']} reason {e.get('reason', e.get('stage'))}", flush=True)


def report(path):
    tot = {}
    with open(path) as fh:
        for row in csv.DictReader(fh):
            t = tot.setdefault(row['host'], {'minutes': 0, 'acks': 0, 'within45': 0, 'stall_min': 0, 'dropped': 0,
                                             'mouse': 0, 'max': 0})
            a = [int(row['ack_' + b]) for b in ACK_EDGES]
            t['minutes'] += 1
            t['acks'] += sum(a)
            t['within45'] += sum(a[:3])
            t['stall_min'] += 1 if a[6] + a[7] else 0
            t['dropped'] += int(row['dropped'])
            t['mouse'] += int(row['mouse'] or 0)
            t['max'] = max(t['max'], int(row['ack_max_ms'] or 0))
    for host, t in sorted(tot.items()):
        share = 100 * t['within45'] / t['acks'] if t['acks'] else 0
        print(f"host {host}: {t['minutes']} min with traffic, {t['mouse']} mouse reports, {share:.1f}% completed "
              f"within 45 ms, {t['stall_min']} min with a report over 150 ms (max {t['max']} ms), "
              f"{t['dropped']} dropped")


def main(args):
    global URL
    if '--url' in args:
        i = args.index('--url')
        URL = args[i + 1].rstrip('/')
        del args[i:i + 2]
    if not args or args[0] not in ('links', 'stats', 'soak', 'alerts', 'report') or \
            (args[0] in ('soak', 'report') and len(args) < 2):
        print(__doc__)
        return 2
    try:
        if args[0] == 'links':
            links()
        elif args[0] == 'stats':
            stats(int(args[1]) if len(args) > 1 else 40)
        elif args[0] == 'soak':
            soak(args[1])
        elif args[0] == 'alerts':
            alerts(args[1] if len(args) > 1 else None)
        else:
            report(args[1])
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
