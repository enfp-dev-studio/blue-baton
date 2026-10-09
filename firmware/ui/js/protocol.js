/*
 * 보드와 주고받는 한 줄 JSON 프로토콜 (firmware/PROTOCOL.md).
 * 통신 방식(USB, 모의 보드, 나중에 Wi-Fi)은 transport가 맡고, 이 파일은 그 위에서만 동작한다.
 *
 * transport가 갖춰야 할 것:
 *   open(), close(), isOpen(), send(line) -> Promise, label
 *   onLine(line), onClose(reason) 콜백을 이쪽에서 채운다
 */

export class BoardError extends Error {
  constructor(code, detail = '') {
    super(detail ? `${code}: ${detail}` : code);
    this.code = code;
    this.detail = detail;
  }
}

export class BoardClient {
  constructor(transport, { timeoutMs = 5000 } = {}) {
    this.transport = transport;
    this.timeoutMs = timeoutMs;
    this.nextId = 1;
    this.pending = new Map();
    this.listeners = new Set();
    transport.onLine = (line) => this.#onLine(line);
    transport.onClose = (reason) => {
      for (const p of this.pending.values()) {
        clearTimeout(p.timer);
        p.reject(new BoardError('closed', reason));
      }
      this.pending.clear();
      this.#emit({ event: '_closed', reason });
    };
  }

  /* 보드가 보낸 이벤트와 내부 알림(_closed, _log, _sent)을 받는다. 해제 함수를 돌려준다 */
  on(fn) {
    this.listeners.add(fn);
    return () => this.listeners.delete(fn);
  }

  #emit(msg) {
    for (const fn of this.listeners) {
      try {
        fn(msg);
      } catch (e) {
        console.error(e);
      }
    }
  }

  #onLine(line) {
    /* 보드 로그도 같은 포트로 온다. KM< 뒤만 프로토콜이다 */
    const at = line.indexOf('KM< ');
    if (at < 0) {
      if (line.trim()) this.#emit({ event: '_log', line });
      return;
    }
    let msg;
    try {
      msg = JSON.parse(line.slice(at + 4));
    } catch {
      this.#emit({ event: '_bad_line', line });
      return;
    }
    if (msg.event) {
      this.#emit(msg);
      return;
    }
    this.#emit({ event: '_reply', msg });
    const p = this.pending.get(msg.id);
    if (!p) return;
    this.pending.delete(msg.id);
    clearTimeout(p.timer);
    if (msg.ok) p.resolve(msg);
    else p.reject(new BoardError(msg.code || 'error', msg.error || ''));
  }

  request(cmd, args = {}) {
    if (!this.transport.isOpen()) return Promise.reject(new BoardError('not_connected'));
    const id = this.nextId++;
    const line = JSON.stringify({ id, cmd, ...args });
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        if (this.pending.delete(id)) reject(new BoardError('timeout', cmd));
      }, this.timeoutMs);
      this.pending.set(id, { resolve, reject, timer });
      this.#emit({ event: '_sent', line });
      this.transport.send(line).catch((e) => {
        this.pending.delete(id);
        clearTimeout(timer);
        reject(new BoardError('send_failed', e.message));
      });
    });
  }
}
