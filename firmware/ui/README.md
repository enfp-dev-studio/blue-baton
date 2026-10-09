# 설정 화면 (firmware/ui)

보드(KM 라우터)의 입력 기기 페어링과 상태를 보는 화면이다. 빌드 단계 없이 ES 모듈로 되어 있다.
구조는 [GUI 제안서](../../devlog/proposals/2026-10-09-gui-app.md) 5.1을 따른다.

## 여는 방법 (세 가지 연결 방식)

| 방식 | 주소 | 언제 |
|---|---|---|
| 로컬 브리지 (추천) | `http://localhost:8766/` (브리지가 `?bridge`로 보낸다) | 개발 중. 화면과 모니터가 동시에 붙고, 플래시해도 끊기지 않는다. 어떤 브라우저든 된다 |
| USB 직접 (Web Serial) | `http://localhost:8765/` | 브리지 없이. Chrome/Edge만 된다. 이 탭이 포트를 혼자 잡는다 |
| 모의 보드 | `http://localhost:8766/?mock` (브리지) 또는 `http://localhost:8765/?mock` | 보드 없이 화면 개발. `&unlocked`는 PIN 없이 열기, `&fast`는 스캔 10초 |

브리지 실행 (ESP-IDF 파이썬에 pyserial이 있다):

```bash
~/.espressif/python_env/idf6.1_py3.14_env/bin/python firmware/tools/bridge.py
```

정적 서버만 (USB 직접, 모의 보드):

```bash
python3 -m http.server 8765 --bind 127.0.0.1 --directory firmware/ui
```

플래시 (브리지가 떠 있으면 포트를 잠시 놓았다가 다시 잡는다. 화면은 스스로 다시 붙는다):

```bash
firmware/tools/flash.sh
```

브리지로 명령을 보내고 이벤트를 보기 (개발 도구):

```bash
curl -sN http://127.0.0.1:8766/events
```

```bash
curl -s -X POST --data '{"id":1,"cmd":"status"}' http://127.0.0.1:8766/send
```

## 파일

| 파일 | 하는 일 |
|---|---|
| `js/transport/webserial.js` | USB 직접 연결. 한 번 허용한 포트는 다음부터 선택 창 없이 연다 |
| `js/transport/bridge.js` | 로컬 브리지 연결 (서버 전송 이벤트 + POST) |
| `js/transport/mock.js` | 모의 보드. 실기에서 받은 마우스와 키보드, 실기로 못 해 본 페어링 방식(숫자 비교, 패스키 입력과 표시)을 흉내 낸다 |
| `js/protocol.js` | 한 줄 JSON 명령과 응답, 이벤트 (`firmware/PROTOCOL.md`) |
| `js/store.js` | 화면 상태. 보드의 응답과 이벤트로만 바뀐다. 기기는 하나의 목록이다 |
| `js/views.js` | 상단, 잠금, 기기 목록, 기기 추가, 페어링 확인, 로그 |
| `js/inputs.js` | 입력 확인 (Report Map대로 풀어 보여 준다) |
| `js/hid/report-map.js` | HID Report Map 해석 |
| `js/i18n.js` | 화면 문자열 (지금은 한국어) |

## 시험 (보드 없이)

```bash
firmware/test/run.sh
```

- 펌웨어 C: `hid_map.c` 종류 판정 18개 (주소 검사기 켬)
- 화면 JS: Report Map 해석 9개 (실기 Report Map으로), 모의 보드와의 명령·이벤트 흐름 5개

## 알려 둔 제한

- 화면을 연 뒤에 연결된 기기만 Report Map을 받는다. 이미 연결된 기기의 입력은 기기가 다시 연결될 때부터 풀어 보인다 (보드에 Report Map을 다시 보내 달라는 명령이 아직 없다).
- Claude 데스크톱 앱의 내장 브라우저는 Web Serial 포트 선택 창이 없어서 USB 직접 방식이 안 된다. 브리지 방식은 된다.
