/*
 * Line-JSON protocol with the board (firmware/PROTOCOL.md).
 * The transport (USB, mock board, later Wi-Fi) handles the link; this file only works on top of it.
 *
 * A transport must provide:
 *   open(), close(), isOpen(), send(line) -> Promise, label
 *   This side fills in the onLine(line) and onClose(reason) callbacks
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

  /* Receives board events and internal notifications (_closed, _log, _sent). Returns an unsubscribe function */
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
    /* Board log lines arrive on the same port. Only what follows KM< is protocol */
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
