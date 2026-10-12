#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
Local bridge: this one program holds the board's USB serial port and shares it with several clients.

  - The settings UI (browser), a dev monitor, and a log writer can attach at the same time without fighting over the port.
  - To flash, /release frees the port and /acquire takes it back afterwards (tools/flash.sh).
  - Also serves the settings UI files (ui/dist). Same origin, so no browser security restrictions get in the way.

HTTP (listens on 127.0.0.1 only)
  GET  /events        Lines from the board as server-sent events (text/event-stream). One line = one data field
  POST /send          Sends the one-line body (a JSON command) to the board
  POST /release       Frees the port (before flashing)
  POST /acquire       Takes the port back
  GET  /status        {"port", "open", "released", "clients"}
  GET  anything else  Static files from ui/dist. "/" redirects to "/?bridge"

Security: keystrokes pass through this board, so arbitrary web pages must not be able to send commands.
  - Binds to 127.0.0.1 only.
  - POST is accepted only with no Origin (command-line tools) or this bridge's own origin. Requests from other sites are refused.
  - No CORS headers are sent, so other sites cannot read /events.

Run with the ESP-IDF Python (includes pyserial):
  ~/.espressif/python_env/idf6.1_py3.14_env/bin/python firmware/tools/bridge.py
"""

import argparse
import glob
import json
import os
import queue
import threading
import time
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

import serial  # pyserial

# The built settings UI (ui/dist; run `npm run build` in ui/ first)
UI_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "..", "ui", "dist"))


class Board:
    """One serial port and its subscribers. Finds the port again if it disappears."""

    def __init__(self, port_glob, log_path):
        self.port_glob = port_glob
        self.ser = None
        self.port = None
        self.released = False
        self.lock = threading.Lock()
        self.clients = set()
        self.clients_lock = threading.Lock()
        self.log = open(log_path, "a", buffering=1) if log_path else None
        threading.Thread(target=self._run, daemon=True).start()

    def _find_port(self):
        ports = sorted(glob.glob(self.port_glob))
        return ports[0] if ports else None

    def _open(self):
        port = self._find_port()
        if not port:
            return False
        try:
            ser = serial.Serial(port, 115200, timeout=0.1)
            # USB Serial/JTAG resets on certain DTR/RTS combinations. Use one that does not reset (same as live.py)
            ser.dtr = True
            ser.rts = False
        except (OSError, serial.SerialException) as e:
            self.broadcast(f"#bridge open failed {port}: {e}")
            return False
        with self.lock:
            self.ser, self.port = ser, port
        self.broadcast(f"#bridge open {port}")
        return True

    def _close(self, why):
        with self.lock:
            ser, self.ser = self.ser, None
        if ser:
            try:
                ser.close()
            except Exception:
                pass
            self.broadcast(f"#bridge closed ({why})")

    def _run(self):
        buf = b""
        while True:
            if self.released:
                time.sleep(0.2)
                continue
            if not self.ser and not self._open():
                time.sleep(1.0)
                continue
            try:
                data = self.ser.read(4096)
            except (OSError, serial.SerialException):
                self._close("lost")
                buf = b""
                continue
            if not data:
                continue
            buf += data
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                self.broadcast(line.decode("utf-8", "replace").rstrip("\r"))

    def broadcast(self, line):
        if self.log:
            self.log.write(f"{time.time():.3f} {line}\n")
        with self.clients_lock:
            for q in list(self.clients):
                try:
                    q.put_nowait(line)
                except queue.Full:
                    pass  # A subscriber that is too slow loses lines (it does not block the others)

    def send(self, line):
        with self.lock:
            if not self.ser:
                return False
            self.ser.write((line.rstrip("\n") + "\n").encode())
        if self.log:
            self.log.write(f"{time.time():.3f} >> {line}\n")
        return True

    def release(self):
        self.released = True
        self._close("released")

    def acquire(self):
        self.released = False

    def subscribe(self):
        q = queue.Queue(maxsize=5000)
        with self.clients_lock:
            self.clients.add(q)
        return q

    def unsubscribe(self, q):
        with self.clients_lock:
            self.clients.discard(q)


def make_handler(board, port):
    allowed = {f"http://localhost:{port}", f"http://127.0.0.1:{port}"}

    class Handler(SimpleHTTPRequestHandler):
        def __init__(self, *a, **kw):
            super().__init__(*a, directory=UI_DIR, **kw)

        def log_message(self, fmt, *args):
            pass  # do not log every request

        def end_headers(self):
            # This is a dev server: no caching, so edits to the UI files show up after one reload
            if not self.path.startswith("/events"):
                self.send_header("Cache-Control", "no-store")
            super().end_headers()

        def _json(self, code, obj):
            body = json.dumps(obj).encode()
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def _origin_ok(self):
            origin = self.headers.get("Origin")
            return origin is None or origin in allowed

        def do_GET(self):
            path = urlparse(self.path).path
            if path == "/events":
                return self._events()
            if path == "/status":
                return self._json(200, {"port": board.port, "open": board.ser is not None,
                                        "released": board.released, "clients": len(board.clients)})
            if self.path == "/":
                self.send_response(302)
                self.send_header("Location", "/?bridge")
                self.end_headers()
                return
            return super().do_GET()

        def _events(self):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.end_headers()
            q = board.subscribe()
            try:
                self.wfile.write(b": connected\n\n")
                self.wfile.flush()
                while True:
                    try:
                        line = q.get(timeout=15)
                        self.wfile.write(b"data: " + line.encode() + b"\n\n")
                    except queue.Empty:
                        self.wfile.write(b": keepalive\n\n")
                    self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError):
                pass
            finally:
                board.unsubscribe(q)

        def do_POST(self):
            if not self._origin_ok():
                return self._json(403, {"error": "origin not allowed"})
            path = urlparse(self.path).path
            length = int(self.headers.get("Content-Length") or 0)
            body = self.rfile.read(length).decode("utf-8", "replace") if length else ""
            if path == "/send":
                line = body.strip()
                if not line or "\n" in line:
                    return self._json(400, {"error": "one line required"})
                if not board.send(line):
                    return self._json(409, {"error": "port not open"})
                return self._json(200, {"ok": True})
            if path == "/release":
                board.release()
                return self._json(200, {"ok": True})
            if path == "/acquire":
                board.acquire()
                return self._json(200, {"ok": True})
            return self._json(404, {"error": "not found"})

    return Handler


def main():
    ap = argparse.ArgumentParser(description="Blue Baton USB serial bridge")
    ap.add_argument("--port", type=int, default=8766, help="HTTP port (127.0.0.1)")
    ap.add_argument("--serial", default="/dev/cu.usbmodem*", help="serial device glob")
    ap.add_argument("--log", default="", help="append every line with a timestamp to this file")
    args = ap.parse_args()
    board = Board(args.serial, args.log)
    server = ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(board, args.port))
    server.daemon_threads = True
    print(f"bridge on http://localhost:{args.port}/  (serial {args.serial}, ui {UI_DIR})", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
