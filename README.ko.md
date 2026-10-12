# Blue Baton

[English](README.md) | 한국어

블루투스 LE 키보드·마우스 하나로 컴퓨터 3대(예: Mac 2대 + Windows PC 1대)를 오가는 라우터. ESP32-S3 SuperMini 한 개로 만든다. 칩에 블루투스 클래식이 없어서 입력 기기는 블루투스 LE를 지원해야 한다.
입력 기기와 모든 호스트와의 연결을 동시에 유지하고, 입력을 받을 호스트만 바꾼다. 전환할 때 아무것도 다시 연결하지 않는다.

```text
BLE 키보드 ─┐                       ┌─ BLE HID → 호스트 1
BLE 마우스 ─┴─→ ESP32-S3 (NimBLE) ─┼─ BLE HID → 호스트 2
                  │                 └─ BLE HID → 호스트 3
                  └─ USB: 메뉴 막대 앱 (설정, 빠른 전환)
```

상태: 펌웨어 0.7. 입력 기기는 2대까지(키보드와 마우스, 또는 아무 두 대), 호스트는 3대. 키보드와 마우스를 동시에 연결하고, Mac 2대·Windows PC·iPhone을 호스트로 실기 확인했다. 입력은 활성 호스트로 가고, 키 조합과 설정 화면 모두로 호스트를 바꾼다. 실기 확인 전: 호스트 패스키를 키보드로 입력하기(앱 화면 입력은 확인), 시스템 키(전원·잠자기·깨우기). 앱은 macOS용만 빌드했다.

라이선스: 아직 정하지 않았다.

## 폴더

| 경로 | 내용 |
|---|---|
| [firmware/main/](firmware/main/) | 펌웨어 (ESP-IDF v6.1, NimBLE). 입력 쪽 `ble_pair.c`, 리포트 해석 `hid_input.c`, 전환 `router.c`, 출력 쪽 `host.c` |
| [firmware/PROTOCOL.md](firmware/PROTOCOL.md) | USB 제어 프로토콜 (한 줄에 JSON 하나, 영어) |
| [app/](app/) | 메뉴 막대 앱 (Tauri, macOS용만 빌드함): 보드 USB 포트를 맡고, 호스트를 빠르게 바꾸고, 설정 창을 띄운다 |
| [ui/](ui/) | 설정 화면 (React, Tailwind CSS, shadcn/ui. 브라우저에서도 Web Serial이나 개발용 브리지로 열린다). 브라우저 언어가 한국어면 한국어로 나온다 |
| [firmware/tools/](firmware/tools/) | 개발용 브리지 `bridge.py`, 플래시 `flash.sh`, 보드 관찰 `watch.py` (연결, 지연 통계, 장시간 기록, 알림) |
| [firmware/test/](firmware/test/) | 보드 없이 도는 시험: 펌웨어 C 시험. `run.sh`는 설정 화면 JS 시험(`ui/test/`)도 돌린다 |

## 빌드, 플래시, 시험

공개용 빌드 (개발 진단 꺼짐):

```bash
. ~/esp/esp-idf/export.sh && cd firmware && idf.py build
```

개발용 빌드와 올리기 (진단 켜짐: 상세 `stats`, `selftest`):

```bash
firmware/tools/flash.sh
```

`flash.sh`는 `firmware/build-dev`에 빌드한 뒤 첫 번째 `/dev/cu.usbmodem*` 포트에 올린다 (macOS. 다른 포트는 `KM_PORT`로 지정). ESP-IDF가 `~/esp/esp-idf`에 있어야 한다.

```bash
firmware/test/run.sh
```

메뉴 막대 앱은 [app/README.md](app/README.md), 설정 화면만은 [ui/README.md](ui/README.md) (영어).
