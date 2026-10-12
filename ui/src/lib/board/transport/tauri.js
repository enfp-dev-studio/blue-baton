// SPDX-License-Identifier: GPL-3.0-only
/*
 * The menu bar app's own serial link (app/src-tauri/src/board.rs). The app owns the USB port; this page gets
 * its lines as `board-line` events and sends commands with the `board_send` command. Only works inside the app.
 */

import { t } from '../i18n.js';

export class TauriTransport {
  static available() {
    return typeof window !== 'undefined' && !!window.__TAURI__?.core;
  }

  constructor() {
    this.label = t('transport.app');
    this.open_ = false;
    this.listening = null;
    this.onLine = () => {};
    this.onClose = () => {};
  }

  isOpen() {
    return this.open_;
  }

  async open() {
    const app = window.__TAURI__;
    if (!this.listening) {
      this.listening = app.event.listen('board-line', (e) => {
        const line = e.payload;
        if (typeof line !== 'string') return;
        /* The app marks port changes the way tools/bridge.py does */
        if (line.startsWith('#bridge closed')) {
          this.#closed('lost');
          return;
        }
        if (line.startsWith('#bridge')) return;
        if (this.open_) this.onLine(line);
      });
    }
    await this.listening;
    const st = await app.core.invoke('board_status');
    if (st.state !== 'open') {
      throw new Error(st.state === 'busy' ? 'app_port_busy' : st.state === 'released' ? 'bridge_released' : 'app_no_board');
    }
    this.open_ = true;
  }

  #closed(reason) {
    if (!this.open_) return;
    this.open_ = false;
    this.onClose(reason);
  }

  async send(line) {
    try {
      await window.__TAURI__.core.invoke('board_send', { line });
    } catch {
      throw new Error('app_no_board');
    }
  }

  async close() {
    this.#closed('closed');
  }
}
