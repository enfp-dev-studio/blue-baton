#!/usr/bin/env python3
"""
Watch the board through the menu bar app's developer API or tools/bridge.py (same HTTP interface).

  watch.py links         One line per input-device connection: interval, connection parameter requests and
                         whether the controller took them, and how the connection ended.
  watch.py stats [N]     Reset the board's latency statistics, wait for N activity events (default 40, about
                         10 s of use; idle gaps don't count), then print the stats reply.

  --url URL              Default http://127.0.0.1:8766

Intervals are in units of 1.25 ms (6 = 7.5 ms, 7 = 8.75 ms, 12 = 15 ms), as in firmware/PROTOCOL.md.
Standard library only.
"""
import json
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


def main(args):
    global URL
    if '--url' in args:
        i = args.index('--url')
        URL = args[i + 1].rstrip('/')
        del args[i:i + 2]
    if not args or args[0] not in ('links', 'stats'):
        print(__doc__)
        return 2
    try:
        if args[0] == 'links':
            links()
        else:
            stats(int(args[1]) if len(args) > 1 else 40)
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
