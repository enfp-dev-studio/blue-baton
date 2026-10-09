# USB 제어 프로토콜 (펌웨어 0.5)

보드를 USB로 메인 기기에 연결하고, 그 기기의 설정 화면(`firmware/ui`, 로컬 브리지 또는 Web Serial)이 이 프로토콜로 보드를 제어합니다.

이전 판에서 바뀐 것은 맨 아래 "0.4 → 0.5 변경", "0.3 → 0.4 변경"에 모았습니다.

## 기본 규칙

- 호스트 → 보드: 한 줄에 JSON 객체 하나, 줄 끝은 `\n`. 한 줄은 255바이트까지입니다.
  ```text
  {"id":1,"cmd":"hello"}
  ```
- 보드 → 호스트: 줄 앞에 `KM< ` 접두사를 붙인 JSON 한 줄
  ```text
  KM< {"id":1,"ok":true,"fw":"km-router","version":"0.5","pin_set":true,"unlocked":false,"combo_set":false,"wifi_feature":false}
  ```
- 응답은 `id`가 같습니다. 보드가 먼저 보내는 알림(이벤트)은 `event` 필드가 있고 `id`가 없습니다.
- 보드 로그(`I (123) km: ...`)도 같은 포트로 나옵니다. 호스트는 `KM< `로 시작하는 줄만 읽으세요. `KM< ` 위치를 찾은 뒤 파싱합니다.
- 실패 응답은 `code`(기계용)와 `error`(사람용 문장)를 함께 줍니다. 화면은 `code`를 보고 문구를 고릅니다.
  ```text
  {"id":5,"ok":false,"code":"not_found","error":"그 주소의 장치가 없습니다"}
  ```
- 주소는 `AA:BB:CC:DD:EE:FF` 형식입니다.
  - `addr`: 지금 연결에 쓰는 무선(on-air) 주소입니다.
  - `id_addr`: 장치의 identity 주소입니다. 페어링된 장치를 구분하는 영속 키입니다.
  - 정적 주소를 쓰는 장치는 둘이 같습니다. RPA를 쓰는 장치는 다릅니다.

## 잠금

- 처음에는 PIN이 없습니다. `pin_set`으로 PIN(숫자 4~8자리)을 정하면 바로 열린 상태가 됩니다.
- PIN이 있으면 `auth`로 먼저 열어야 합니다. `lock`으로 다시 잠글 수 있습니다.
- 재부팅하면 잠긴 상태로 시작합니다. PIN과 저장한 값은 NVS에 남습니다.
- 잠긴 상태에서 막히는 명령은 `code: "error"`, `error: "잠금 상태입니다..."`로 옵니다.

## 명령

| 명령 | 인자 | 잠금 필요 | 설명 |
|---|---|---|---|
| `hello` | 없음 | 아니오 | 펌웨어 정보, PIN 설정 여부, 열림 상태, 전환 키 설정 여부, Wi-Fi 기능 여부 |
| `status` | 없음 | 아니오 | 아래 "status 응답" |
| `pin_set` | `pin` | 있으면 필요 | PIN을 정하거나 바꿈 |
| `auth` | `pin` | 아니오 | PIN 확인. 맞으면 열림 |
| `lock` | 없음 | 아니오 | 다시 잠금 |
| `combo_get` | 없음 | 필요 | 실제로 적용되는 호스트 전환 조합 (`combo`). 저장된 값이 없거나 형식이 틀리면 기본값이고 `default:true`, 틀린 값이 저장돼 있으면 `stored_invalid:true` |
| `combo_set` | `combo` | 필요 | 호스트 순서대로 쉼표로 나눈 조합 목록 (예: `ctrl+alt+1,ctrl+alt+2,ctrl+alt+3`, 최대 95자). 형식을 검사한 뒤 바로 적용하고 저장한다. 틀리면 `code:"bad_combo"` |
| `host_select` | `host` (1~3) | 필요 | 활성 호스트를 직접 고른다. 키 조합 전환과 같은 처리(이전 호스트에 모두 뗌) |
| `host_pair` | `on` | 필요 | 호스트 추가 창을 열거나 닫는다. 열면 180초 동안 새 호스트의 연결과 페어링을 받는다. 한 대가 붙으면 스스로 닫힌다. 3대가 차 있으면 `code:"hosts_full"`. 여는 동안 입력 장치 스캔과 사용자 연결 시도는 멈춘다 |
| `host_list` | 없음 | 필요 | 페어링된 호스트 목록 (`hosts`). 아래 "host_list 항목" |
| `host_remove` | `host` (1~3) | 필요 | 그 번호의 호스트 본딩을 지운다. 연결 중이면 끊는다. 그 컴퓨터에서도 "KM Router"를 지워야 다시 추가할 수 있다 |
| `host_passkey` | `code` (6자리 문자열) | 필요 | `host_passkey_request`에 GUI로 답한다 (라우터에 붙은 키보드로 치는 대신) |
| `pair_scan` | `on` | 필요 | 새 장치 스캔 켜기/끄기. 켜면 60초 뒤 스스로 멈춘다(`scan_stopped` `reason:"timeout"`). 스캔 중에는 자동 재연결을 쉰다. 사용자 연결 시도가 진행 중이면 그만두고(`connect_failed` `status:"cancelled"`) 바로 스캔한다 |
| `pair_list` | `all` (선택, 기본 false) | 필요 | 찾은 장치 목록 (`devices`). 기본은 HID 장치만, `all:true`면 연결 가능한 모든 장치. 지금 연결된 장치는 빠진다 |
| `pair_connect` | `addr` | 필요 | 찾은 새 장치: 연결하고 페어링한다 (10초 시도). 페어링된 장치: 직접 연결하지 않고 자동 연결 목록에 다시 넣는다 (자동 연결이 꺼졌던 것도 켠다). 장치가 깨어 광고하면 연결된다. 스캔 중이면 스캔을 멈춘다 |
| `disconnect` | `addr` | 필요 | 연결된 장치를 끊는다. 페어링은 남기고, 다시 `pair_connect`할 때까지 자동 재연결하지 않는다 |
| `pair_accept` | `accept` | 필요 | 숫자 비교(`numcmp`) 요청에 수락/거부 |
| `passkey` | `code` (6자리 문자열) | 필요 | `passkey_request`에 응답 |
| `pair_cancel` | 없음 | 필요 | 스캔과 사용자 연결 시도를 멈추고, 진행 중인 페어링 입력을 거절한다. **이미 연결된 장치는 끊지 않는다** |
| `paired_list` | 없음 | 필요 | 페어링된 입력 장치 목록 (`paired`). 원천은 NimBLE 본딩 저장소. 호스트로 적힌 본딩은 빠진다 |
| `paired_remove` | `addr` (identity) | 필요 | 페어링 하나를 지운다 (본딩 키, 해석 리스트, 이름 등). 연결 중이면 끊는다 |
| `paired_clear` | 없음 | 필요 | 페어링을 모두 지운다 |

실패 `code` (BLE 명령)

| code | 뜻 |
|---|---|
| `busy` | 지금은 할 수 없음 (BLE 준비 전, 이미 연결됨, 다른 연결 시도 진행 중) |
| `not_found` | 그 주소의 장치가 없음 (찾은 목록에도, 페어링 목록에도) |
| `no_slot` | 입력 장치 자리(동시 2대)가 모두 참 |
| `bad_addr` | 주소가 없거나 형식이 틀림 |
| `no_request` | 응답할 페어링 입력 요청이 없음 |
| `host_window` | 호스트 추가 창이 열려 있어 입력 장치 스캔/새 연결을 받지 않음 |
| `hosts_full` | 호스트 3대가 모두 참 |
| `bad_host` | 호스트 번호가 1~3이 아님 |
| `bad_code` | 패스키가 숫자 6자리가 아님 |
| `failed` | 그 밖의 실패 |

### status 응답

```text
{"id":9,"ok":true,"scanning":false,"connected":true,"hid_ready":true,"peer":"00:00:5E:00:53:17",
 "peers":[{"addr":"00:00:5E:00:53:17","id_addr":"00:00:5E:00:53:17","state":"ready","hid_ready":true}],
 "radio":"auto","bonds":1,"bonds_max":8,"active_host":1}
```

- `radio`: 라디오가 지금 하는 일입니다.
  - `idle`: 아무것도 안 함
  - `scan`: 새 장치 스캔
  - `connect`: 사용자가 고른 장치에 연결 시도
  - `auto`: 페어링된 장치의 자동 재연결 대기
  - `cancel`: 이전 시도를 취소하는 중
- `peers[].state`: `connected`, `securing`, `discovering`, `ready`
- `bonds`: NimBLE에 저장된 본딩 수. `bonds_max`: 최대 (입력 4 + 호스트 3 + 진행 중 1)
- `active_host`: 입력을 보낼 호스트 (1부터)

### pair_list와 paired_list 항목

```text
devices: [{"addr":"00:00:5E:00:53:18","name":"Xiaoxin Mplus 5.0","rssi":-43,"paired":false,"appearance":962,"hid":true}]
paired:  [{"addr":"00:00:5E:00:53:17","type":1,"name":"Xiaoxin Mplus 5.0","kinds":["mouse"],"connected":false,
           "state":"offline","auto":true,"last_seq":1}]
```

- `appearance`: 광고의 외형 값 (Bluetooth Assigned Numbers). 961 키보드, 962 마우스 등. 0이면 없음
- `kinds`: Report Map으로 판정한 종류 (`keyboard`, `mouse`, `consumer`, `system`, `game`, `other`). 한 번 연결해야 알 수 있다
- `auto`: 자동 재연결 대상인지. 사용자가 `disconnect`했거나 연속 3번 준비 전에 끊기면 false
- `last_seq`: 마지막으로 연결된 순서. 클수록 최근이다. 오래 안 쓴 기록을 정리할 때 쓴다

### host_list 항목

```text
hosts: [{"host":1,"addr":"AA:BB:CC:DD:EE:FF","name":"MacBook Pro","connected":true,"state":"ready","active":true}]
```

- `addr`: 호스트의 identity 주소 (Mac은 RPA로 붙어도 컨트롤러가 풀어 identity로 보인다)
- `name`: 호스트의 GAP Device Name (Mac은 컴퓨터 이름). 처음 준비될 때 읽는다
- `state`: `offline`, `connected`(암호화 전), `secured`(구독 전), `ready`(리포트를 보낼 수 있음)

## 이벤트 (보드가 먼저 보냄)

| 이벤트 | 필드 | 뜻 |
|---|---|---|
| `ble_ready` | `bonds`, `max` | BLE 준비 완료 (부팅, 호스트 리셋 뒤) |
| `ble_reset` | 없음 | BLE 호스트 리셋. 연결이 모두 사라짐 |
| `scan_started` | 없음 | 새 장치 스캔 시작 |
| `scan_stopped` | `reason` | 스캔이 멈춤. `timeout`(60초), `radio`(다른 작업으로 전환), `preempted`(스택이 잠시 끊음, 곧 다시 시작), `error` |
| `device_found` | `addr`, `name`, `rssi`, `paired`, `appearance` | HID 장치를 찾음. 이름 등이 나중에(스캔 응답) 채워지면 같은 `addr`로 다시 온다 |
| `autoconnect` | `devices` | 페어링된 장치 N개를 accept list에 넣고 자동 연결 대기 시작 |
| `autoconnect_paused` | `id_addr`, `fails` | 연속으로 준비 전에 끊겨 그 장치의 자동 재연결을 멈춤 |
| `connecting` | `addr` | 사용자가 고른 장치에 연결 시도 중 |
| `connect_failed` | `addr`, `status` | 사용자 연결 실패 (`status`: HCI/NimBLE 코드 또는 `"no_slot"`) |
| `connected` | `addr`, `id_addr`, `bonded`, `user`, `itvl`, `latency`, `timeout` | 연결됨. `bonded`: 이미 페어링된 장치, `user`: 사용자가 고른 연결. 연결 값: `itvl` 단위 1.25 ms, `timeout` 단위 10 ms. 보드는 7.5~15 ms, Latency 0, 감시 4초를 요청한다 |
| `conn_params` | `addr`, `status`, `itvl`, `latency`, `timeout` | 장치가 연결 값을 바꿈 |
| `numcmp` | `addr`, `code` (6자리 문자열) | 숫자 비교. 장치와 같은 숫자인지 보고 `pair_accept`로 답함 |
| `passkey_request` | `addr` | 장치에 나온 6자리를 입력해야 함. `passkey`로 답함 |
| `passkey_display` | `addr`, `code`, `ok` | 보드가 만든 번호. 이 번호를 장치(키보드)로 입력해야 함 |
| `encrypted` | `addr`, `ok`, `status`, `bond`, `authenticated`, `key_size` | 암호화 결과. `bond`: `new`(새 페어링), `restored`(저장된 키로 복원), `none` |
| `paired` | `addr`, `id_addr`, `name`, `bond` | 페어링된 장치로 확정 (새 페어링과 복원 모두) |
| `bond_full` | `stage`, `bonds`, `max` | 본딩 저장소가 가득 차서 새 페어링을 거부함. 기존 본딩은 지우지 않는다 |
| `store_full` | `obj` | 본딩이 아닌 저장 항목(구독 CCCD 등)의 칸이 모자람. 설정값 문제 |
| `hid_chr` | `addr`, `svc`, `val`, `props`, `uuid16` | HID 서비스의 특성 목록 (확인용) |
| `report_ref` | `addr`, `val`, `id`, `type`, `svc` | 입력 리포트 값 핸들 `val`의 Report ID와 종류 |
| `hid_info` | `addr`, `svc`, `bcd_hid`, `country`, `remote_wake`, `normally_connectable` | HID Information |
| `report_map` | `addr`, `svc`, `len`, `truncated`, `kinds`, `hex` | Report Map 전체. 서비스(`svc`)마다 하나. 512바이트를 넘으면 `truncated:true` |
| `subscribe_failed` | `addr`, `val`, `status` | 입력 리포트 하나의 알림 켜기 실패 (나머지는 계속) |
| `hid_ready` | `addr`, `id_addr`, `kinds`, `reports`, `services` | 입력을 받을 준비가 됨 |
| `device_name` | `addr`, `name` | GAP Device Name을 읽음 (광고에 이름이 없는 장치도 여기서 이름을 얻는다) |
| `report` | `addr`, `handle`, `data` (hex) | 입력 리포트. `handle`은 `report_ref`의 `val` |
| `failed` | `addr`, `stage`, `status` | 연결 하나가 실패로 끝남. 보드가 연결을 끊는다. `stage`: `security_start`, `security`, `not_bonded`, `services`, `no_hid_service`, `characteristics`, `descriptors` |
| `disconnected` | `addr`, `id_addr`, `reason` | 연결이 끊김 (HCI 사유 코드. 531 = 상대가 끊음) |
| `unpaired` | `id_addr` | 페어링 하나가 지워짐 |
| `unpaired_all` | 없음 | 페어링이 모두 지워짐 |
| `host_switch` | `host`, `from` | 활성 호스트가 바뀜 (키 조합 또는 `host_select`). 1부터 |
| `route` | `host`, `keys`, `buttons`, `consumer` | 활성 호스트로 보낼 상태가 바뀜 (키, 마우스 버튼, 미디어 키). `keys`는 Keyboard 페이지 Usage 목록(수정 키 0xE0~0xE7 포함). 마우스 이동은 싣지 않는다. 확인용 |
| `host_online` | `host`, `online` | 전환한 호스트가 지금 연결돼 있는지. `false`면 연결될 때까지 입력은 버린다 (나중에 몰아 보내지 않는다) |
| `host_window` | `open`, `seconds` | 호스트 추가 창이 열림/닫힘 |
| `host_connected` | `host`, `addr`, `bonded`, `itvl` | 호스트가 연결됨. 본딩된 호스트면 `host`가 번호, 창에서 처음 붙은 컴퓨터면 `host:0` |
| `host_passkey_request` | `host`, `addr` | 호스트 화면의 6자리를 입력해야 함. 라우터에 붙은 키보드로 치고 Enter(이동안 키 입력은 어느 호스트에도 가지 않는다), 또는 `host_passkey`. Esc는 취소 |
| `host_passkey_progress` | `host`, `addr`, `len` | 키보드로 친 자릿수 (숫자는 싣지 않는다) |
| `host_passkey_sent` | `host`, `addr`, `ok` | 패스키를 보안 절차에 넘김 |
| `host_paired` | `host`, `addr` | 새 호스트를 그 번호로 저장함. 창은 닫힌다 |
| `host_pair_failed` | `host`, `addr`, `status`, `authenticated` | 호스트 페어링(또는 키 복원) 실패. 인증되지 않은 새 본딩은 지우고 끊는다 |
| `host_repair_blocked` | `host`, `addr` | 본딩된 호스트가 다시 페어링하려 함 (그 컴퓨터에서 보드를 지움). 창 밖이라 거부하고 끊었다. 창을 연 뒤 다시 연결하면 받는다 (번호와 이름은 유지) |
| `host_ready` | `host`, `addr` | 호스트가 알림을 켜서 리포트를 받을 수 있음 |
| `host_name` | `host`, `addr`, `name` | 호스트의 GAP Device Name |
| `host_conn_params` | `host`, `addr`, `status`, `itvl`, `latency`, `timeout` | 호스트 연결 값이 바뀜. 보드는 15 ms, Latency 4, 감시 6초를 요청한다 |
| `host_disconnected` | `host`, `addr`, `reason` | 호스트 연결이 끊김. 보드는 30초 동안 빠르게(20 ms) 광고한 뒤 152.5 ms로 광고한다 |
| `host_removed` | `host` | 호스트 하나를 지움 |

## 흐름

### 새 장치 추가

1. `auth`로 열기
2. `pair_scan` `{"on":true}` → `device_found` 확인
3. `pair_connect` `{"addr":"..."}` (스캔은 보드가 알아서 멈춘다)
4. 이벤트에 따라 답하기
   - 아무 요청도 없음: Just Works (대부분의 마우스, 일부 키보드)
   - `numcmp` → `pair_accept` `{"accept":true}`
   - `passkey_request` → `passkey` `{"code":"123456"}`
   - `passkey_display` → 보드가 보여 준 번호를 장치로 입력
5. `encrypted` `bond:"new"` → `paired` → `hid_ready` → `device_name`

### 호스트 추가 (Mac, Windows)

1. `host_pair` `{"on":true}` → `host_window` `open:true`
2. 컴퓨터의 Bluetooth 설정에서 "KM Router"에 연결 → `host_connected` `host:0`
3. `host_passkey_request` → 컴퓨터 화면의 6자리를 라우터 키보드로 치고 Enter, 또는 `host_passkey` `{"code":"123456"}`
4. `host_passkey_sent` → `host_paired` `host:N` → `host_window` `open:false` → `host_ready` → `host_name`
5. 다음부터는 컴퓨터가 켜지면 스스로 다시 붙는다 (`host_connected` `bonded:true` → `host_ready`)

보드는 창이 열린 동안만 키보드(KeyboardOnly)로 알리고, 본딩된 호스트나 창 안의 새 컴퓨터가 아닌 연결은 바로 끊는다.

### 이미 페어링된 장치

- 부팅하거나 장치가 끊기면 보드가 자동으로 다시 연결을 기다린다 (`autoconnect`).
- 장치가 광고하면 연결된다. 순서는 `connected` `bonded:true` → `encrypted` `bond:"restored"` → `paired` → `hid_ready`. 사용자 조작은 필요 없다.
- 장치가 자기 쪽 키를 잃었으면 스택이 자동으로 새로 페어링한다. 이때는 `bond:"new"`가 온다 (이 동작은 끌 수 없다).

## 알려 둘 제한

- 동시 입력 연결은 2대, 본딩은 8개(입력 4 + 호스트 3 + 진행 중 1)까지다.
- 보안 절차(페어링, 키 복원)는 장치 전체에서 한 번에 하나씩 처리한다. 두 장치가 동시에 붙으면 하나는 잠시 기다린다.
- 장치가 페어링 모드에 들어갈 때마다 주소(identity)를 새로 만드는 경우가 있다. 그러면 새 장치로 들어오고, 이전 기록은 `last_seq`를 보고 사용자가 지운다.
- 찾은 목록은 최대 16개이고, 넘치면 가장 오래전에 본 장치를 밀어낸다. 이름은 최대 23바이트다.
- Wi-Fi 설정 명령(`/api/*`)은 `KM_WIFI_ENABLE=y`로 빌드할 때만 있다 (기본은 꺼짐).

## 호스트 전환 키 조합

- 형식: 수정 키(`ctrl`/`control`, `shift`, `alt`/`option`/`opt`, `cmd`/`gui`/`win`/`meta`/`super`) 하나 이상 + 일반 키 하나. 일반 키는 `a`~`z`, `0`~`9`, `f1`~`f24`, `space`, `enter`, `esc`, `tab`, `backspace`, `home`, `end`, `pageup`, `pagedown`, `insert`, `delete`, 방향키(`left` 등), `scrolllock`, `pause`, `printscreen`, `capslock`, 또는 `0x3a` 같은 Usage 값
- 수정 키 없는 단일 키는 받지 않는다 (일반 입력을 빼앗는다)
- 기본값: `ctrl+alt+1,ctrl+alt+2,ctrl+alt+3`
- 동작 (일반 KVM과 같다)
  - 조합의 일반 키가 눌리는 순간, 잡고 있는 수정 키 묶음이 조합과 정확히 같고 다른 일반 키가 없으면 전환한다. 왼쪽/오른쪽은 가리지 않는다
  - 그 순간 눌려 있던 키는 각각 뗄 때까지 어느 호스트에도 보내지 않는다 (조합이 새지 않는다)
  - 이전 호스트에는 "모두 뗌"을 보낸다. 지금 쓰는 호스트의 조합을 누르면 전환 없이 조합만 삼킨다
  - 입력 장치가 끊기면 그 장치가 누르던 키는 뗀 것으로 본다

## 0.4 → 0.5 변경

- 출력 쪽을 추가했다: 보드가 호스트 3대에 HID 키보드 + 마우스(HOGP)로 보인다. 연결은 동시에 유지하고 활성 호스트에만 리포트를 보낸다.
- 명령 추가: `host_pair`, `host_list`, `host_remove`, `host_passkey`
- 이벤트 추가: `host_online`, `host_window`, `host_connected`, `host_passkey_request`, `host_passkey_progress`, `host_passkey_sent`, `host_paired`, `host_pair_failed`, `host_repair_blocked`, `host_ready`, `host_name`, `host_conn_params`, `host_disconnected`, `host_removed`
- 실패 `code` 추가: `host_window`, `hosts_full`, `bad_code`
- 호스트 추가 창이 열린 동안 `pair_scan` `on:true`와 새 장치 `pair_connect`는 `code:"host_window"`로 거부한다.

## 0.3 → 0.4 변경

- 페어링 목록의 원천을 NimBLE 본딩 저장소로 바꿨다. 본딩 키를 NVS에 영구 저장한다. 0.3의 `paired` 기록(v1)은 키가 없는 기록이라 부팅 때 지운다.
- 자동 재연결(accept list)을 추가했다. 이벤트는 `autoconnect`, `autoconnect_paused`.
- 명령 추가: `disconnect`, `paired_clear`, `pair_list`의 `all`.
- `pair_cancel`은 더 이상 연결된 장치를 끊지 않는다.
- 실패 응답에 `code`를 추가했다.
- 이벤트 필드 추가
  - `encrypted`: `bond`, `authenticated`, `key_size`
  - `connected`: `id_addr`, `bonded`, `user`
  - `report_ref`, `report_map`: `svc`
  - `report_map`: `kinds`, `truncated`
  - `hid_ready`: `kinds`, `reports`, `services`
- 이벤트 추가: `ble_ready`, `ble_reset`, `scan_started`, `hid_info`, `device_name`, `failed`, `bond_full`, `unpaired`, `unpaired_all`
- `no_hid_service` 이벤트는 `failed` `stage:"no_hid_service"`로 바꿨다.
- `numcmp`와 `passkey_display`의 `code`는 6자리 문자열이다 (앞자리 0 보존).
- `status`에 `radio`, `bonds`, `bonds_max`를 추가했다. `peers` 항목에 `id_addr`, `state`를 추가했다.
- HID 서비스가 여러 개인 장치를 처리한다 (0.3은 첫 번째만).
- 스캔은 광고와 스캔 응답을 합쳐서 이름과 UUID를 얻는다. 60초 뒤 스스로 멈춘다.
