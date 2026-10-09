#!/usr/bin/env python3
"""
로컬 브리지: 보드의 USB 시리얼 포트를 이 프로그램 하나가 잡고, 여러 곳에 나눠 준다 (GUI 제안서 5.4).

  - 설정 화면(브라우저), 개발 모니터, 로그 저장이 동시에 붙는다. 포트를 두고 서로 막지 않는다.
  - 플래시할 때는 /release로 포트를 놓고, 끝나면 /acquire로 다시 잡는다 (tools/flash.sh).
  - 설정 화면 파일(firmware/ui)도 같이 서빙한다. 같은 주소에서 열리므로 브라우저 보안 제약이 없다.

HTTP (127.0.0.1에만 연다)
  GET  /events        보드에서 온 줄을 서버 전송 이벤트(text/event-stream)로 보낸다. 한 줄 = data 한 개
  POST /send          본문 한 줄(JSON 명령)을 보드로 보낸다
  POST /release       포트를 놓는다 (플래시 전에)
  POST /acquire       포트를 다시 잡는다
  GET  /status        {"port", "open", "released", "clients"}
  GET  그 밖          firmware/ui의 정적 파일. "/"는 "/?bridge"로 보낸다

보안: 키 입력이 지나가는 보드라서 아무 웹페이지나 명령을 보내면 안 된다.
  - 127.0.0.1에만 바인딩한다.
  - POST는 Origin이 없거나(명령줄 도구) 이 브리지 자신일 때만 받는다. 다른 사이트의 요청은 거부한다.
  - CORS 헤더를 주지 않으므로 다른 사이트는 /events를 읽을 수 없다.

실행: ESP-IDF 파이썬(pyserial 포함)으로
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

UI_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "ui"))


class Board:
    """시리얼 포트 하나와 구독자들. 포트가 빠지면 다시 찾는다."""

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
            # USB Serial/JTAG는 DTR/RTS 조합으로 리셋된다. 리셋하지 않는 조합으로 둔다 (live.py와 같음)
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
                    pass  # 너무 느린 구독자는 줄을 잃는다 (다른 구독자를 막지 않는다)

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
            pass  # 요청마다 찍지 않는다

        def end_headers(self):
            # 개발용 서버다: 화면 파일을 고치면 새로고침 한 번에 반영되게 캐시하지 않는다
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
    ap = argparse.ArgumentParser(description="KM router USB serial bridge")
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
