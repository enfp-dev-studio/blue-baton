/* 화면 문자열. 지금은 한국어만 있다. 다른 언어는 같은 키로 표를 하나 더 두면 된다 */

const ERRORS = {
  busy: '지금은 할 수 없어요. 이미 연결됐거나 다른 연결을 시도하는 중이에요.',
  not_found: '그 기기를 찾을 수 없어요. 다시 스캔해 보세요.',
  no_slot: '입력 기기 자리(2대)가 모두 찼어요. 하나를 끊은 뒤 다시 시도하세요.',
  bad_addr: '기기 주소 형식이 맞지 않아요.',
  no_request: '응답할 페어링 요청이 없어요. 이미 끝났거나 취소됐어요.',
  failed: '보드가 요청을 처리하지 못했어요.',
  timeout: '보드가 응답하지 않아요. USB 연결을 확인하세요.',
  not_connected: '보드가 연결되어 있지 않아요.',
  closed: '보드 연결이 끊겼어요.',
  send_failed: '보드로 보내지 못했어요. USB 연결을 확인하세요.',
  port_busy: '다른 프로그램이 USB 포트를 쓰고 있어요. 다른 탭, 시리얼 모니터, 플래시 도구를 닫고 다시 시도하세요.',
  already_open: '이 포트는 이미 열려 있어요.',
  no_serial: '이 브라우저는 USB 연결(Web Serial)을 지원하지 않아요. Chrome이나 Edge로 여세요.',
  bridge_down: '브리지가 꺼져 있어요. firmware/tools/bridge.py를 실행하세요.',
  bridge_released: '브리지가 포트를 잠시 놓았어요 (플래시 중). 끝나면 자동으로 다시 붙어요.',
  bridge_no_board: '브리지가 보드를 찾지 못했어요. USB 연결을 확인하세요.',
  host_window: '호스트 추가 창이 열려 있어요. 창을 닫은 뒤 입력 기기를 추가하세요.',
  hosts_full: '호스트 3대가 모두 찼어요. 하나를 삭제한 뒤 다시 추가하세요.',
  bad_code: '패스키는 숫자 6자리예요.',
  bad_host: '호스트 번호는 1~3이에요.',
};

export function errorText(e) {
  if (!e) return '';
  if (e.name === 'NotFoundError') return '보드를 고르지 않았어요.';
  const code = e.code || e.message;
  if (code === 'error' && e.detail) return e.detail;
  return ERRORS[code] || e.detail || e.message || String(e);
}

const STATES = {
  ready: '연결됨',
  connecting: '연결하는 중',
  pairing: '페어링하는 중',
  securing: '암호화하는 중',
  discovering: '준비하는 중',
  connected: '연결하는 중',
  failed: '실패',
  offline: '대기 중',
  found: '',
};

export function stateText(state) {
  return STATES[state] ?? state;
}

export function stateTone(state) {
  if (state === 'ready') return 'ok';
  if (state === 'failed') return 'bad';
  if (state === 'offline') return 'idle';
  return 'busy';
}

const KINDS = { keyboard: '키보드', mouse: '마우스', consumer: '미디어 키', system: '시스템 키', game: '게임패드', other: '기타' };

/* 종류 표시. 연결해 본 기기는 Report Map 판정(kinds), 처음 보는 기기는 광고의 외형 값으로 */
export function kindText(kinds, appearance) {
  if (kinds && kinds.length) {
    const main = kinds.filter((k) => k === 'keyboard' || k === 'mouse' || k === 'game');
    return (main.length ? main : kinds).map((k) => KINDS[k] || k).join(' · ');
  }
  const a = appearance || 0;
  if (a === 961) return '키보드';
  if (a === 962) return '마우스';
  if (a === 963) return '조이스틱';
  if (a === 964) return '게임패드';
  if (a >= 960 && a <= 1023) return '입력 기기';
  return '기타 기기';
}

export function kindIcon(kinds, appearance) {
  const k = kinds && kinds.length ? kinds : [];
  if (k.includes('keyboard') || appearance === 961) return 'keyboard';
  if (k.includes('mouse') || appearance === 962) return 'mouse';
  if (k.includes('game') || appearance === 963 || appearance === 964) return 'game';
  return 'device';
}

export const RADIO = {
  idle: '쉬는 중',
  scan: '새 기기 찾는 중',
  connect: '연결 시도 중',
  auto: '페어링된 기기 기다리는 중',
  cancel: '정리하는 중',
};

export const FAIL_STAGE = {
  security: '암호화(페어링)에 실패했어요',
  security_start: '페어링을 시작하지 못했어요',
  not_bonded: '기기가 페어링 정보를 저장하지 않았어요',
  services: '기기 정보를 읽지 못했어요',
  no_hid_service: '키보드나 마우스 기능이 없는 기기예요',
  characteristics: '기기 정보를 읽지 못했어요',
  descriptors: '기기 정보를 읽지 못했어요',
};

/* HCI 끊김 사유 중 자주 보는 것 */
export function reasonText(reason) {
  if (reason === 531 || reason === 0x13) return '기기가 연결을 끊었어요 (절전 등)';
  if (reason === 520 || reason === 0x08) return '신호가 끊겼어요';
  if (reason === 534 || reason === 22) return '보드가 연결을 끊었어요';
  return '';
}

const HOST_STATES = {
  ready: '연결됨',
  secured: '준비하는 중',
  connected: '암호화하는 중',
  pairing: '페어링하는 중',
  offline: '연결 안 됨',
};

export function hostStateText(state) {
  return HOST_STATES[state] ?? state;
}

export function hostStateTone(state) {
  if (state === 'ready') return 'ok';
  if (state === 'offline') return 'idle';
  return 'busy';
}
