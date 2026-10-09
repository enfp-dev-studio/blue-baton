/*
 * 로컬 브리지 연결 (firmware/tools/bridge.py). 브리지가 USB 포트를 잡고, 이 화면은 HTTP로 붙는다.
 * 여러 화면과 개발 도구가 동시에 같은 보드를 볼 수 있고, 어떤 브라우저에서도 된다 (Web Serial이 필요 없다).
 * 브리지가 서빙한 페이지에서만 쓴다 (같은 주소).
 */

export class BridgeTransport {
  constructor(base = '') {
    this.label = 'USB (브리지)';
    this.base = base;
    this.es = null;
    this.onLine = () => {};
    this.onClose = () => {};
  }

  isOpen() {
    return !!this.es;
  }

  async open() {
    const st = await fetch(`${this.base}/status`).then((r) => r.json()).catch(() => null);
    if (!st) throw new Error('bridge_down');
    if (!st.open) throw new Error(st.released ? 'bridge_released' : 'bridge_no_board');
    await new Promise((resolve, reject) => {
      const es = new EventSource(`${this.base}/events`);
      es.onopen = () => resolve();
      es.onerror = () => {
        if (!this.es) {
          es.close();
          reject(new Error('bridge_down'));
        }
      };
      es.onmessage = (e) => {
        /* 브리지 자신의 알림은 #으로 시작한다 */
        if (e.data.startsWith('#bridge closed')) {
          this.#closed('lost');
          return;
        }
        this.onLine(e.data);
      };
      this.pendingEs = es;
    });
    this.es = this.pendingEs;
  }

  #closed(reason) {
    if (!this.es) return;
    this.es.close();
    this.es = null;
    this.onClose(reason);
  }

  async send(line) {
    const r = await fetch(`${this.base}/send`, { method: 'POST', body: line });
    if (!r.ok) throw new Error(r.status === 409 ? 'bridge_no_board' : `bridge_http_${r.status}`);
  }

  async close() {
    this.#closed('closed');
  }
}
