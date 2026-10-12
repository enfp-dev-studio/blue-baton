// SPDX-License-Identifier: GPL-3.0-only
/*
 * Local bridge link (firmware/tools/bridge.py). The bridge holds the USB port and this page talks to it over HTTP.
 * Several pages and dev tools can watch the same board at once, in any browser (no Web Serial needed).
 * Use it only from a page the bridge itself serves (same origin).
 */

import { t } from '../i18n.js';

export class BridgeTransport {
  constructor(base = '') {
    this.label = t('transport.bridge');
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
        /* The bridge's own notices start with # */
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
