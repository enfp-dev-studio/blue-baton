/*
 * UI strings. English is the default, Korean is the second language.
 *
 * Language: ?lang=en|ko in the URL wins; otherwise navigator.language starting with "ko" picks Korean,
 * anything else English. The choice is fixed when the page loads.
 *
 * Every table uses the same keys (test/i18n.test.mjs checks this). To add a language, add a table
 * with the same keys and placeholders. Strings use {name} placeholders filled by t(key, params).
 */

const en = {
  /* ---- page (index.html, data-i18n) ---- */
  'app.title': 'Blue Baton Settings',
  'app.name': 'Blue Baton',
  'app.subtitle': 'Settings',
  'log.title': 'Developer: communication log',
  'log.boardLines': 'Show board log lines too',

  /* ---- transport labels ---- */
  'transport.bridge': 'USB (bridge)',
  'transport.mock': 'Mock board',

  /* ---- shared ---- */
  'common.close': 'Close',
  'common.remove': 'Remove',
  'common.send': 'Send',
  'common.unnamedDevice': 'Unnamed device',

  /* ---- errors by code (errorText) ---- */
  'error.busy': "Can't do that right now. Already connected, or another connection attempt is in progress.",
  'error.not_found': "Couldn't find that device. Try scanning again.",
  'error.no_slot': 'Both input device slots (2) are in use. Disconnect one, then try again.',
  'error.bad_addr': "That device address isn't valid.",
  'error.no_request': "There's no pairing request to answer. It already finished or was cancelled.",
  'error.failed': "The board couldn't complete the request.",
  'error.timeout': "The board isn't responding. Check the USB connection.",
  'error.not_connected': "The board isn't connected.",
  'error.closed': 'The connection to the board was lost.',
  'error.send_failed': "Couldn't send to the board. Check the USB connection.",
  'error.port_busy': 'Another program is using the USB port. Close other tabs, serial monitors, and flash tools, then try again.',
  'error.already_open': 'This port is already open.',
  'error.no_serial': "This browser doesn't support USB connections (Web Serial). Open this page in Chrome or Edge.",
  'error.bridge_down': "The bridge isn't running. Start firmware/tools/bridge.py.",
  'error.bridge_released': 'The bridge released the port for flashing. It reconnects by itself when done.',
  'error.bridge_no_board': "The bridge can't find the board. Check the USB connection.",
  'error.host_window': 'The host pairing window is open. Close it before adding an input device.',
  'error.hosts_full': 'All 3 host slots are in use. Remove one, then add again.',
  'error.bad_code': 'The passkey must be 6 digits.',
  'error.bad_host': 'Host number must be 1 to 3.',
  'error.locked': 'Settings are locked. Enter the PIN first.',
  'error.no_pin': 'No PIN is set yet. Set one first.',
  'error.wrong_pin': "That PIN isn't right.",
  'error.bad_pin': 'The PIN must be 4 to 8 digits.',
  'error.bad_combo': "That switch combo isn't valid. Use modifiers (ctrl, shift, alt, cmd) plus one key.",
  'error.bad_request': "The board didn't understand the request.",
  'error.unknown_cmd': "The board doesn't know this command. Update the firmware.",

  /* ---- input device states (stateText) ---- */
  'state.ready': 'Connected',
  'state.connecting': 'Connecting',
  'state.pairing': 'Pairing',
  'state.securing': 'Encrypting',
  'state.discovering': 'Setting up',
  'state.connected': 'Connecting',
  'state.failed': 'Failed',
  'state.offline': 'Waiting',
  'state.found': '',

  /* ---- host states (hostStateText) ---- */
  'hostState.ready': 'Connected',
  'hostState.secured': 'Setting up',
  'hostState.connected': 'Encrypting',
  'hostState.pairing': 'Pairing',
  'hostState.offline': 'Not connected',

  /* ---- device kinds from the Report Map (kindText) ---- */
  'kind.keyboard': 'Keyboard',
  'kind.mouse': 'Mouse',
  'kind.consumer': 'Media keys',
  'kind.system': 'System keys',
  'kind.game': 'Gamepad',
  'kind.other': 'Other',

  /* ---- device kinds from the advertised appearance (kindText) ---- */
  'appearance.keyboard': 'Keyboard',
  'appearance.mouse': 'Mouse',
  'appearance.joystick': 'Joystick',
  'appearance.gamepad': 'Gamepad',
  'appearance.input': 'Input device',
  'appearance.other': 'Other device',

  /* ---- radio activity (RADIO) ---- */
  'radio.idle': 'Idle',
  'radio.scan': 'Scanning for new devices',
  'radio.connect': 'Connecting',
  'radio.auto': 'Waiting for paired devices',
  'radio.cancel': 'Cleaning up',

  /* ---- failure stage of a "failed" event (FAIL_STAGE) ---- */
  'fail.security': 'Encryption (pairing) failed',
  'fail.security_start': "Couldn't start pairing",
  'fail.not_bonded': "The device didn't save the pairing",
  'fail.services': "Couldn't read device info",
  'fail.no_hid_service': 'This device has no keyboard or mouse function',
  'fail.characteristics': "Couldn't read device info",
  'fail.descriptors': "Couldn't read device info",

  /* ---- HCI disconnect reasons (reasonText) ---- */
  'reason.remote': 'The device disconnected (e.g. to save power)',
  'reason.timeout': 'Signal lost',
  'reason.local': 'The board disconnected',

  /* ---- top bar: board link and lock ---- */
  'link.connecting': 'Connecting to the board…',
  'link.disconnected': 'Board not connected',
  'link.connect': 'Connect board',
  'link.connected': 'Connected',
  'link.info': '{transport} · firmware {fw}',
  'link.lockTitle': 'Lock settings',
  'link.lock': 'Lock',
  'link.releaseTitle': 'Releases the USB port so other programs can use the board',
  'link.release': 'Disconnect',
  'link.notPicked': 'No board was selected.',

  /* ---- PIN lock ---- */
  'lock.setTitle': 'Set a settings PIN',
  'lock.lockedTitle': 'Settings are locked',
  'lock.setHint': 'Choose a PIN (4 to 8 digits) to protect the board settings. It is stored on the board.',
  'lock.unlockHint': 'Enter the PIN stored on the board to unlock settings.',
  'lock.pin': 'PIN',
  'lock.set': 'Set PIN',
  'lock.unlock': 'Unlock',

  /* ---- hosts ---- */
  'hosts.title': 'Hosts',
  'hosts.hint': 'Press a switch combo or pick a host here to choose which computer gets your input',
  'hosts.groupLabel': 'Active host',
  'hosts.pickTitle': 'Send input to this host',
  'hosts.unnamed': 'Unnamed host',
  'hosts.empty': 'Empty',
  'hosts.noCombo': 'No combo',
  'hosts.add': 'Add',
  'hosts.windowTitle': 'Host pairing · {seconds}s',
  'hosts.stepMac': 'macOS: System Settings → Bluetooth, click Connect next to “Blue Baton”',
  'hosts.stepWindows': 'Windows: Settings → Bluetooth & devices → Add device → Bluetooth → “Blue Baton”',
  'hosts.stepCode': 'When a 6-digit code appears, enter it as described below',
  'hosts.verifying': 'Verifying…',
  'hosts.candidate': 'A computer ({addr}) connected. Waiting for pairing…',
  'hosts.passkeyHelp': 'Type the 6-digit code shown on the computer on a keyboard connected to the board, then press Enter.',
  'hosts.passkeyDigits': '{n} of 6 digits entered',
  'hosts.passkeyFallback': 'No keyboard on the board? Enter the code here instead (Esc cancels)',
  'hosts.passkeyLabel': 'Host passkey',

  /* ---- input devices ---- */
  'devices.title': 'Input devices',
  'devices.summary': 'Paired {bonds}/{max} · up to 2 connected at once',
  'devices.empty': 'No paired devices yet',
  'devices.emptyHint': 'Add a keyboard or mouse. The board remembers it and connects to it automatically from then on.',
  'devices.add': 'Add device',
  'devices.addBlocked': 'Close the host pairing window to add a device',
  'devices.failed': 'Connection failed',
  'devices.restored': 'Reconnected with saved keys',
  'devices.newBond': 'Newly paired',
  'devices.autoOff': 'Auto-connect is off · turn it back on to reconnect',
  'devices.autoHint': 'Connects automatically when you move the device or press a key',
  'devices.disconnect': 'Disconnect',
  'devices.retry': 'Retry',
  'devices.autoOn': 'Turn on auto-connect',

  /* ---- add device (scan results) ---- */
  'add.title': 'Add a new device',
  'add.step1': 'Put the keyboard or mouse you want to add into pairing mode (the button differs by device)',
  'add.step2': 'When it shows up below, click Connect. The board connects only to the device you pick',
  'add.scanning': 'Scanning · {seconds}s · Stop',
  'add.starting': 'Starting…',
  'add.start': 'Start scan',
  'add.showAll': 'Show non-input devices too',
  'add.searching': 'Looking for devices…',
  'add.idleHint': 'Click Start scan. It scans for 60 seconds.',
  'add.paired': 'Paired',
  'add.signal': 'Signal {rssi} dBm',
  'add.notInput': 'Not an input device',
  'add.connecting': 'Connecting…',
  'add.connect': 'Connect',

  /* ---- pairing prompts for input devices ---- */
  'prompt.numcmp': 'Check that this matches the number shown on the device',
  'prompt.match': 'It matches, connect',
  'prompt.noMatch': "It doesn't match",
  'prompt.passkeyRequest': 'Enter the 6-digit code shown on the device',
  'prompt.passkeyLabel': 'Passkey',
  'prompt.passkeyDisplay': 'Type this code on that keyboard, then press Enter',

  /* ---- notices (app.js) ---- */
  'notice.unplugged': 'The board was unplugged.',
  'notice.lost': 'Lost the connection to the board. Reconnecting…',
  'notice.bondFull': 'Pairing storage is full ({bonds}/{max}). Remove a device you no longer use, then try again.',
  'notice.pairFailed': 'Pairing failed. The code was wrong or the device declined. You can try again.',
  'notice.failed': 'Connection failed.',
  'notice.connectFailed': "Couldn't connect. Make sure the device is in pairing mode and nearby.",
  'notice.paired': '{name} is paired. It connects automatically from now on.',
  'notice.newDevice': 'New device',
  'notice.hostPaired': 'Added host {host}. It connects automatically whenever that computer is on.',
  'notice.hostPairFailed': 'Host pairing failed. The code was wrong or time ran out. Click Connect on the computer again.',
  'notice.hostRepairBlocked': 'Host {host} is trying to pair again. If you removed the board on that computer, open the host pairing window here and connect again.',
  'notice.hostOffline': "Host {host} isn't connected. No input is sent until it connects.",
  'notice.released': 'Released the USB port. Other programs (flasher, monitor) can use the board now.',
  'notice.autoOn': '{name}: auto-connect is on. Move the device or press a key to connect.',
  'notice.reconnected': 'Reconnected to the board.',

  /* ---- confirm dialogs (app.js) ---- */
  'confirm.hostName': 'Host {n}',
  'confirm.removeHost': 'Remove {name}?\nTo add it again later, also remove “Blue Baton” from that computer’s Bluetooth settings.',
  'confirm.removeDevice': 'Remove the pairing for {name}?\nTo use it again, put the device in pairing mode and add it again.',

  /* ---- input monitor (inputs.js) ---- */
  'inputs.title': 'Input monitor',
  'inputs.hint': 'Shows the input reaching the board, decoded with its Report Map',
  'inputs.mouse': 'Mouse',
  'inputs.buttons': 'Buttons',
  'inputs.dx': 'X total',
  'inputs.dy': 'Y total',
  'inputs.wheel': 'Wheel total',
  'inputs.mouseStats': '{count} reports · {rate}/s',
  'inputs.resetMouse': 'Reset totals',
  'inputs.keyboard': 'Keyboard',
  'inputs.keyStats': 'Modifiers {mods} · Keys {keys} · Media {media}',
  'inputs.resetKeys': 'Clear text',
  'inputs.details': 'Report decoding details',
  'inputs.mapWaiting': 'Waiting for the Report Map (the board reads it and sends it when a device connects)',
  'inputs.colDevice': 'Device',
  'inputs.colHandle': 'Handle',
  'inputs.colLength': 'Length',
  'inputs.colCount': 'Count',
  'inputs.colLast': 'Last value',
  'inputs.fieldsHint': 'Non-zero fields in the last report',
  'inputs.layout': 'ID {id}: {bits} bits, {fields} fields',
  'inputs.mapInfo': '{addr} service {svc}: Report Map {len} bytes{truncated} · {layouts}',
  'inputs.truncated': ' (truncated!)',
  'inputs.noInputReports': 'no input reports',
  'inputs.bytes': '{n} bytes',
  'inputs.rawOnly': 'No Report Map yet, raw data only: {data}',
  'inputs.arrayValue': ' (array value {value})',
  'inputs.reportId': 'Report ID {id}',
  'inputs.allZero': '(all zero, released)',
  'inputs.left': 'Left',
  'inputs.right': 'Right',
  'inputs.middle': 'Middle',

  /* ---- HID usage names (hid/report-map.js usageName) ---- */
  'usage.key': 'Key 0x{hex}',
  'usage.button': 'Button {n}',
  'usage.wheel': 'Wheel',
  'usage.desktop': 'Desktop 0x{hex}',
  'usage.volumeUp': 'Volume+',
  'usage.volumeDown': 'Volume-',
  'usage.mute': 'Mute',
  'usage.playPause': 'Play/Pause',
  'usage.hWheel': 'Horizontal wheel',
  'usage.consumer': 'Consumer 0x{hex}',
};

const ko = {
  /* ---- page (index.html, data-i18n) ---- */
  'app.title': 'Blue Baton 설정',
  'app.name': 'Blue Baton',
  'app.subtitle': '설정',
  'log.title': '개발자: 통신 로그',
  'log.boardLines': '보드 로그 줄도 보기',

  /* ---- transport labels ---- */
  'transport.bridge': 'USB (브리지)',
  'transport.mock': '모의 보드',

  /* ---- shared ---- */
  'common.close': '닫기',
  'common.remove': '삭제',
  'common.send': '보내기',
  'common.unnamedDevice': '이름 없는 기기',

  /* ---- errors by code (errorText) ---- */
  'error.busy': '지금은 할 수 없어요. 이미 연결됐거나 다른 연결을 시도하는 중이에요.',
  'error.not_found': '그 기기를 찾을 수 없어요. 다시 스캔해 보세요.',
  'error.no_slot': '입력 기기 자리(2대)가 모두 찼어요. 하나를 끊은 뒤 다시 시도하세요.',
  'error.bad_addr': '기기 주소 형식이 맞지 않아요.',
  'error.no_request': '응답할 페어링 요청이 없어요. 이미 끝났거나 취소됐어요.',
  'error.failed': '보드가 요청을 처리하지 못했어요.',
  'error.timeout': '보드가 응답하지 않아요. USB 연결을 확인하세요.',
  'error.not_connected': '보드가 연결되어 있지 않아요.',
  'error.closed': '보드 연결이 끊겼어요.',
  'error.send_failed': '보드로 보내지 못했어요. USB 연결을 확인하세요.',
  'error.port_busy': '다른 프로그램이 USB 포트를 쓰고 있어요. 다른 탭, 시리얼 모니터, 플래시 도구를 닫고 다시 시도하세요.',
  'error.already_open': '이 포트는 이미 열려 있어요.',
  'error.no_serial': '이 브라우저는 USB 연결(Web Serial)을 지원하지 않아요. Chrome이나 Edge로 여세요.',
  'error.bridge_down': '브리지가 꺼져 있어요. firmware/tools/bridge.py를 실행하세요.',
  'error.bridge_released': '브리지가 포트를 잠시 놓았어요 (플래시 중). 끝나면 자동으로 다시 붙어요.',
  'error.bridge_no_board': '브리지가 보드를 찾지 못했어요. USB 연결을 확인하세요.',
  'error.host_window': '호스트 추가 창이 열려 있어요. 창을 닫은 뒤 입력 기기를 추가하세요.',
  'error.hosts_full': '호스트 3대가 모두 찼어요. 하나를 삭제한 뒤 다시 추가하세요.',
  'error.bad_code': '패스키는 숫자 6자리예요.',
  'error.bad_host': '호스트 번호는 1~3이에요.',
  'error.locked': '설정이 잠겨 있어요. PIN을 먼저 입력하세요.',
  'error.no_pin': 'PIN이 아직 없어요. 먼저 정하세요.',
  'error.wrong_pin': 'PIN이 맞지 않아요.',
  'error.bad_pin': 'PIN은 숫자 4~8자리예요.',
  'error.bad_combo': '키 조합 형식이 맞지 않아요. 수정 키(ctrl, shift, alt, cmd)와 일반 키 하나로 정하세요.',
  'error.bad_request': '보드가 요청을 이해하지 못했어요.',
  'error.unknown_cmd': '보드가 모르는 명령이에요. 펌웨어를 업데이트하세요.',

  /* ---- input device states (stateText) ---- */
  'state.ready': '연결됨',
  'state.connecting': '연결하는 중',
  'state.pairing': '페어링하는 중',
  'state.securing': '암호화하는 중',
  'state.discovering': '준비하는 중',
  'state.connected': '연결하는 중',
  'state.failed': '실패',
  'state.offline': '대기 중',
  'state.found': '',

  /* ---- host states (hostStateText) ---- */
  'hostState.ready': '연결됨',
  'hostState.secured': '준비하는 중',
  'hostState.connected': '암호화하는 중',
  'hostState.pairing': '페어링하는 중',
  'hostState.offline': '연결 안 됨',

  /* ---- device kinds from the Report Map (kindText) ---- */
  'kind.keyboard': '키보드',
  'kind.mouse': '마우스',
  'kind.consumer': '미디어 키',
  'kind.system': '시스템 키',
  'kind.game': '게임패드',
  'kind.other': '기타',

  /* ---- device kinds from the advertised appearance (kindText) ---- */
  'appearance.keyboard': '키보드',
  'appearance.mouse': '마우스',
  'appearance.joystick': '조이스틱',
  'appearance.gamepad': '게임패드',
  'appearance.input': '입력 기기',
  'appearance.other': '기타 기기',

  /* ---- radio activity (RADIO) ---- */
  'radio.idle': '쉬는 중',
  'radio.scan': '새 기기 찾는 중',
  'radio.connect': '연결 시도 중',
  'radio.auto': '페어링된 기기 기다리는 중',
  'radio.cancel': '정리하는 중',

  /* ---- failure stage of a "failed" event (FAIL_STAGE) ---- */
  'fail.security': '암호화(페어링)에 실패했어요',
  'fail.security_start': '페어링을 시작하지 못했어요',
  'fail.not_bonded': '기기가 페어링 정보를 저장하지 않았어요',
  'fail.services': '기기 정보를 읽지 못했어요',
  'fail.no_hid_service': '키보드나 마우스 기능이 없는 기기예요',
  'fail.characteristics': '기기 정보를 읽지 못했어요',
  'fail.descriptors': '기기 정보를 읽지 못했어요',

  /* ---- HCI disconnect reasons (reasonText) ---- */
  'reason.remote': '기기가 연결을 끊었어요 (절전 등)',
  'reason.timeout': '신호가 끊겼어요',
  'reason.local': '보드가 연결을 끊었어요',

  /* ---- top bar: board link and lock ---- */
  'link.connecting': '보드에 연결하는 중…',
  'link.disconnected': '보드가 연결되지 않았어요',
  'link.connect': '보드 연결',
  'link.connected': '연결됨',
  'link.info': '{transport} · 펌웨어 {fw}',
  'link.lockTitle': '설정 잠그기',
  'link.lock': '잠그기',
  'link.releaseTitle': 'USB 포트를 놓습니다. 다른 프로그램이 보드를 쓸 수 있어요',
  'link.release': '연결 끊기',
  'link.notPicked': '보드를 고르지 않았어요.',

  /* ---- PIN lock ---- */
  'lock.setTitle': '설정 PIN 정하기',
  'lock.lockedTitle': '설정이 잠겨 있어요',
  'lock.setHint': '보드 설정을 보호할 PIN(숫자 4~8자리)을 정하세요. 보드에 저장돼요.',
  'lock.unlockHint': '보드에 저장된 PIN을 입력하면 설정을 열 수 있어요.',
  'lock.pin': 'PIN',
  'lock.set': '정하기',
  'lock.unlock': '열기',

  /* ---- hosts ---- */
  'hosts.title': '호스트',
  'hosts.hint': '키 조합을 누르거나 여기서 골라 입력을 보낼 컴퓨터를 바꿔요',
  'hosts.groupLabel': '활성 호스트',
  'hosts.pickTitle': '이 호스트로 입력을 보내요',
  'hosts.unnamed': '이름 없는 호스트',
  'hosts.empty': '비어 있음',
  'hosts.noCombo': '조합 없음',
  'hosts.add': '추가',
  'hosts.windowTitle': '호스트 추가 · {seconds}초',
  'hosts.stepMac': 'Mac: 시스템 설정 → Bluetooth에서 근처 기기의 "Blue Baton" 옆 연결',
  'hosts.stepWindows': 'Windows: 설정 → Bluetooth 및 장치 → 장치 추가 → Bluetooth → "Blue Baton"',
  'hosts.stepCode': '숫자 6자리가 나오면 아래 안내대로 입력하세요',
  'hosts.verifying': '확인하는 중…',
  'hosts.candidate': '컴퓨터({addr})가 연결했어요. 페어링을 기다리는 중…',
  'hosts.passkeyHelp': '컴퓨터 화면에 6자리 숫자가 보이면, 보드에 연결된 키보드로 그 숫자를 치고 Enter를 누르세요.',
  'hosts.passkeyDigits': '{n}자리 입력됨',
  'hosts.passkeyFallback': '보드에 키보드가 없으면 여기에 입력하세요 (Esc는 취소)',
  'hosts.passkeyLabel': '호스트 패스키',

  /* ---- input devices ---- */
  'devices.title': '입력 기기',
  'devices.summary': '페어링 {bonds}/{max} · 동시 연결 최대 2대',
  'devices.empty': '아직 페어링된 기기가 없어요',
  'devices.emptyHint': '키보드나 마우스를 추가하면 보드에 저장되고, 다음부터는 자동으로 연결돼요.',
  'devices.add': '기기 추가',
  'devices.addBlocked': '호스트 추가 창을 닫은 뒤에 추가할 수 있어요',
  'devices.failed': '연결에 실패했어요',
  'devices.restored': '저장된 키로 다시 연결됨',
  'devices.newBond': '새로 페어링됨',
  'devices.autoOff': '자동 연결 꺼짐 · 연결을 누르면 다시 켜져요',
  'devices.autoHint': '기기를 움직이거나 키를 누르면 자동으로 연결돼요',
  'devices.disconnect': '끊기',
  'devices.retry': '다시 시도',
  'devices.autoOn': '자동 연결 켜기',

  /* ---- add device (scan results) ---- */
  'add.title': '새 기기 추가',
  'add.step1': '추가할 키보드나 마우스를 페어링 모드로 켜세요 (기기마다 버튼이 달라요)',
  'add.step2': '아래 목록에 나타나면 연결을 누르세요. 보드는 고른 기기에만 연결해요',
  'add.scanning': '찾는 중 · {seconds}초 · 멈추기',
  'add.starting': '시작하는 중…',
  'add.start': '찾기 시작',
  'add.showAll': '입력 기기가 아닌 것도 보기',
  'add.searching': '기기를 찾고 있어요…',
  'add.idleHint': '찾기 시작을 누르세요. 60초 동안 찾아요.',
  'add.paired': '페어링됨',
  'add.signal': '신호 {rssi} dBm',
  'add.notInput': '입력 기기 아님',
  'add.connecting': '연결하는 중…',
  'add.connect': '연결',

  /* ---- pairing prompts for input devices ---- */
  'prompt.numcmp': '기기 화면에 보이는 숫자와 같은지 확인하세요',
  'prompt.match': '같아요, 연결',
  'prompt.noMatch': '달라요',
  'prompt.passkeyRequest': '기기에 보이는 6자리 숫자를 입력하세요',
  'prompt.passkeyLabel': '패스키',
  'prompt.passkeyDisplay': '이 번호를 그 키보드로 입력한 뒤 Enter를 누르세요',

  /* ---- notices (app.js) ---- */
  'notice.unplugged': '보드 USB가 뽑혔어요.',
  'notice.lost': '보드 연결이 끊겼어요. 다시 연결을 시도해요…',
  'notice.bondFull': '페어링 저장 공간이 가득 찼어요 ({bonds}/{max}). 안 쓰는 기기를 삭제한 뒤 다시 시도하세요.',
  'notice.pairFailed': '페어링에 실패했어요. 번호가 틀렸거나 기기가 거절했어요. 다시 시도할 수 있어요.',
  'notice.failed': '연결에 실패했어요.',
  'notice.connectFailed': '연결하지 못했어요. 기기가 페어링 모드인지, 가까이 있는지 확인하세요.',
  'notice.paired': '{name}를 페어링했어요. 다음부터는 자동으로 연결돼요.',
  'notice.newDevice': '새 기기',
  'notice.hostPaired': '호스트 {host}를 추가했어요. 다음부터는 컴퓨터가 켜지면 자동으로 연결돼요.',
  'notice.hostPairFailed': '호스트 페어링에 실패했어요. 숫자가 틀렸거나 시간이 지났어요. 컴퓨터에서 다시 연결을 누르세요.',
  'notice.hostRepairBlocked': '호스트 {host}가 다시 페어링하려 해요. 그 컴퓨터에서 보드를 지웠다면, 여기서 호스트 추가 창을 연 뒤 다시 연결하세요.',
  'notice.hostOffline': '호스트 {host}가 연결되어 있지 않아요. 연결될 때까지 입력은 보내지 않아요.',
  'notice.released': 'USB 포트를 놓았어요. 다른 프로그램(플래시, 모니터)이 보드를 쓸 수 있어요.',
  'notice.autoOn': '{name}: 자동 연결을 켰어요. 기기를 움직이거나 키를 누르면 연결돼요.',
  'notice.reconnected': '보드에 다시 연결했어요.',

  /* ---- confirm dialogs (app.js) ---- */
  'confirm.hostName': '호스트 {n}',
  'confirm.removeHost': '{name}를 삭제할까요?\n그 컴퓨터의 Bluetooth 설정에서도 "Blue Baton"를 지워야 다시 추가할 수 있어요.',
  'confirm.removeDevice': '{name}의 페어링을 삭제할까요?\n다시 쓰려면 기기를 페어링 모드로 켜고 새로 추가해야 해요.',

  /* ---- input monitor (inputs.js) ---- */
  'inputs.title': '입력 확인',
  'inputs.hint': '보드에 들어온 입력을 Report Map대로 풀어 보여 줘요',
  'inputs.mouse': '마우스',
  'inputs.buttons': '버튼',
  'inputs.dx': 'X 누적',
  'inputs.dy': 'Y 누적',
  'inputs.wheel': '휠 누적',
  'inputs.mouseStats': '리포트 {count}개 · 초당 {rate}개',
  'inputs.resetMouse': '누적 지우기',
  'inputs.keyboard': '키보드',
  'inputs.keyStats': '수정 키 {mods} · 누른 키 {keys} · 미디어 키 {media}',
  'inputs.resetKeys': '입력 지우기',
  'inputs.details': '리포트 해석 상세',
  'inputs.mapWaiting': 'Report Map 대기 중 (연결되면 보드가 읽어서 보내 줘요)',
  'inputs.colDevice': '장치',
  'inputs.colHandle': '핸들',
  'inputs.colLength': '길이',
  'inputs.colCount': '개수',
  'inputs.colLast': '마지막 값',
  'inputs.fieldsHint': '마지막 리포트에서 0이 아닌 필드',
  'inputs.layout': 'ID {id}: {bits}비트 {fields}필드',
  'inputs.mapInfo': '{addr} 서비스 {svc}: Report Map {len}바이트{truncated} · {layouts}',
  'inputs.truncated': ' (잘림!)',
  'inputs.noInputReports': '입력 리포트 없음',
  'inputs.bytes': '{n}바이트',
  'inputs.rawOnly': 'Report Map이 아직 없어 원본만: {data}',
  'inputs.arrayValue': ' (배열 값 {value})',
  'inputs.reportId': '리포트 ID {id}',
  'inputs.allZero': '(모두 0, 뗀 상태)',
  'inputs.left': '왼쪽',
  'inputs.right': '오른쪽',
  'inputs.middle': '가운데',

  /* ---- HID usage names (hid/report-map.js usageName) ---- */
  'usage.key': '키 0x{hex}',
  'usage.button': '버튼{n}',
  'usage.wheel': '휠',
  'usage.desktop': '데스크톱 0x{hex}',
  'usage.volumeUp': '볼륨+',
  'usage.volumeDown': '볼륨-',
  'usage.mute': '음소거',
  'usage.playPause': '재생/정지',
  'usage.hWheel': '가로 휠',
  'usage.consumer': '소비자 0x{hex}',
};

export const STRINGS = { en, ko };

/* ?lang=en|ko wins; otherwise a browser language starting with "ko" picks Korean, anything else English */
export function pickLang(search = '', language = '') {
  const q = new URLSearchParams(search).get('lang');
  if (q && Object.hasOwn(STRINGS, q)) return q;
  return String(language).toLowerCase().startsWith('ko') ? 'ko' : 'en';
}

/* Also runs under Node (tests import modules that use t()), so the browser globals are optional */
export const lang = pickLang(
  typeof location !== 'undefined' ? location.search : '',
  typeof navigator !== 'undefined' ? navigator.language || '' : '',
);

/* Active language first, then English. undefined if the key is unknown */
function lookup(key) {
  return STRINGS[lang][key] ?? en[key];
}

const PLACEHOLDER = /\{(\w+)\}/g;

/* t('devices.summary', { bonds: 2, max: 8 }). Unknown keys come back as the key itself */
export function t(key, params) {
  const s = lookup(key) ?? key;
  if (!params) return s;
  return s.replace(PLACEHOLDER, (m, name) => (Object.hasOwn(params, name) ? String(params[name]) : m));
}

/*
 * Like t(), but a param may be a DOM node (e.g. a live countdown). Returns the text split around the
 * placeholders as an array of strings and param values, ready to pass to h() as children.
 */
export function tParts(key, params = {}) {
  const s = lookup(key) ?? key;
  const out = [];
  let last = 0;
  for (const m of s.matchAll(PLACEHOLDER)) {
    if (!Object.hasOwn(params, m[1])) continue;
    if (m.index > last) out.push(s.slice(last, m.index));
    out.push(params[m[1]]);
    last = m.index + m[0].length;
  }
  if (last < s.length) out.push(s.slice(last));
  return out;
}

/* All keys under a prefix as a plain object, e.g. group('radio.') -> { idle, scan, ... } */
function group(prefix) {
  const out = {};
  for (const key of Object.keys(en)) {
    if (key.startsWith(prefix)) out[key.slice(prefix.length)] = lookup(key);
  }
  return out;
}

/* Static page text: <html lang>, and every element with data-i18n="key" gets that string */
export function localizePage(doc = document) {
  doc.documentElement.lang = lang;
  for (const el of doc.querySelectorAll('[data-i18n]')) el.textContent = t(el.dataset.i18n);
}

const ERRORS = group('error.');

export function errorText(e) {
  if (!e) return '';
  if (e.name === 'NotFoundError') return t('link.notPicked');
  const code = e.code || e.message;
  if (code === 'error' && e.detail) return e.detail;   /* firmware 0.4 and earlier: generic code with board text */
  return ERRORS[code] || e.detail || e.message || String(e);
}

const STATES = group('state.');

export function stateText(state) {
  return STATES[state] ?? state;
}

export function stateTone(state) {
  if (state === 'ready') return 'ok';
  if (state === 'failed') return 'bad';
  if (state === 'offline') return 'idle';
  return 'busy';
}

const KINDS = group('kind.');

/* Device kind label. Devices we have connected to use the Report Map verdict (kinds); new ones use the advertised appearance */
export function kindText(kinds, appearance) {
  if (kinds && kinds.length) {
    const main = kinds.filter((k) => k === 'keyboard' || k === 'mouse' || k === 'game');
    return (main.length ? main : kinds).map((k) => KINDS[k] || k).join(' · ');
  }
  const a = appearance || 0;
  if (a === 961) return t('appearance.keyboard');
  if (a === 962) return t('appearance.mouse');
  if (a === 963) return t('appearance.joystick');
  if (a === 964) return t('appearance.gamepad');
  if (a >= 960 && a <= 1023) return t('appearance.input');
  return t('appearance.other');
}

export function kindIcon(kinds, appearance) {
  const k = kinds && kinds.length ? kinds : [];
  if (k.includes('keyboard') || appearance === 961) return 'keyboard';
  if (k.includes('mouse') || appearance === 962) return 'mouse';
  if (k.includes('game') || appearance === 963 || appearance === 964) return 'game';
  return 'device';
}

export const RADIO = group('radio.');

export const FAIL_STAGE = group('fail.');

/* The HCI disconnect reasons we commonly see */
export function reasonText(reason) {
  if (reason === 531 || reason === 0x13) return t('reason.remote');
  if (reason === 520 || reason === 0x08) return t('reason.timeout');
  if (reason === 534 || reason === 22) return t('reason.local');
  return '';
}

const HOST_STATES = group('hostState.');

export function hostStateText(state) {
  return HOST_STATES[state] ?? state;
}

export function hostStateTone(state) {
  if (state === 'ready') return 'ok';
  if (state === 'offline') return 'idle';
  return 'busy';
}
