/*
 * USB 연결 (Web Serial). Chrome/Edge에서만 되고, localhost나 HTTPS에서만 열린다.
 * 한 번 허용한 포트는 getPorts()로 다시 찾으므로, 다음부터는 선택 창 없이 바로 연결된다.
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

  /* 이미 허용한 보드 포트. 없으면 null */
  async knownPort() {
    const ports = await navigator.serial.getPorts();
    return ports.find((p) => p.getInfo().usbVendorId === ESPRESSIF_VID) || null;
  }

  /* prompt가 false면 이미 허용한 포트만 쓴다 (페이지를 열 때 자동 연결용, 사용자 동작이 필요 없다) */
  async open({ prompt = true } = {}) {
    let port = await this.knownPort();
    if (!port) {
      if (!prompt) throw new Error('no_known_port');
      port = await navigator.serial.requestPort({ filters: [{ usbVendorId: ESPRESSIF_VID }] });
    }
    try {
      await port.open({ baudRate: 115200 });
    } catch (e) {
      /* 다른 프로그램(다른 탭, 시리얼 모니터, 플래시 도구)이 포트를 잡고 있으면 여기서 실패한다 */
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
      /* 포트가 닫히면 읽기가 끝난다 */
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

  /* 포트를 놓는다. 다른 프로그램(플래시, 모니터)이 쓸 수 있게 된다 */
  async close() {
    if (!this.port) return;
    this.closing = true;
    /* 읽기 루프가 먼저 끝나 참조를 비울 수 있으므로 먼저 붙잡아 둔다 */
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
