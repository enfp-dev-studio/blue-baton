// SPDX-License-Identifier: GPL-3.0-only
/*
 * USB link (Web Serial). Works only in Chrome/Edge, and only on localhost or HTTPS.
 * A port allowed once is found again with getPorts(), so later connections skip the picker.
 */

const ESPRESSIF_VID = 0x303a;

export class WebSerialTransport {
  static supported() {
    return 'serial' in navigator;
  }

  constructor() {
    this.label = 'USB';
    this.port = null;
    this.writer = null;
    this.reader = null;
    this.onLine = () => {};
    this.onClose = () => {};
    this.enc = new TextEncoder();
    this.closing = false;
    navigator.serial?.addEventListener('disconnect', (e) => {
      if (e.target === this.port) this.#closed('unplugged');
    });
  }

  isOpen() {
    return !!this.writer;
  }

  /* The board port allowed earlier, or null */
  async knownPort() {
    const ports = await navigator.serial.getPorts();
    return ports.find((p) => p.getInfo().usbVendorId === ESPRESSIF_VID) || null;
  }

  /* With prompt false, only an already allowed port is used (auto-connect on page load, no user gesture needed) */
  async open({ prompt = true } = {}) {
    let port = await this.knownPort();
    if (!port) {
      if (!prompt) throw new Error('no_known_port');
      port = await navigator.serial.requestPort({ filters: [{ usbVendorId: ESPRESSIF_VID }] });
    }
    try {
      await port.open({ baudRate: 115200 });
    } catch (e) {
      /* Fails here if another program (another tab, a serial monitor, a flash tool) holds the port */
      throw new Error(e.name === 'InvalidStateError' ? 'already_open' : 'port_busy');
    }
    this.port = port;
    this.writer = port.writable.getWriter();
    this.closing = false;
    this.#readLoop();
  }

  async #readLoop() {
    const decoder = new TextDecoderStream();
    const done = this.port.readable.pipeTo(decoder.writable).catch(() => {});
    this.reader = decoder.readable.getReader();
    let buf = '';
    try {
      for (;;) {
        const { value, done: end } = await this.reader.read();
        if (end) break;
        buf += value;
        let nl;
        while ((nl = buf.indexOf('\n')) >= 0) {
          this.onLine(buf.slice(0, nl).replace(/\r$/, ''));
          buf = buf.slice(nl + 1);
        }
      }
    } catch {
      /* Reading ends when the port closes */
    }
    await done;
    this.#closed(this.closing ? 'closed' : 'lost');
  }

  #closed(reason) {
    if (!this.port) return;
    this.writer = null;
    this.reader = null;
    this.port = null;
    this.onClose(reason);
  }

  async send(line) {
    await this.writer.write(this.enc.encode(line + '\n'));
  }

  /* Release the port so other programs (flasher, monitor) can use it */
  async close() {
    if (!this.port) return;
    this.closing = true;
    /* The read loop may finish first and clear these references, so grab them up front */
    const { port, reader, writer } = this;
    try {
      await reader?.cancel();
    } catch {}
    try {
      writer?.releaseLock();
    } catch {}
    try {
      await port.close();
    } catch {}
    this.#closed('closed');
  }
}
