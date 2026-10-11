# Blue Baton

[English](README.md) | 한국어

블루투스 키보드·마우스 하나로 컴퓨터 3대(예: Mac 2대 + Windows PC 1대)를 오가는 라우터. ESP32-S3 SuperMini 한 개로 만든다.
입력 기기와 모든 호스트와의 연결을 동시에 유지하고, 입력을 받을 호스트만 바꾼다. 전환할 때 아무것도 다시 연결하지 않는다.

```text
BLE 키보드 ─┐                       ┌─ BLE HID → 호스트 1
BLE 마우스 ─┴─→ ESP32-S3 (NimBLE) ─┼─ BLE HID → 호스트 2
                  │                 └─ BLE HID → 호스트 3
                  └─ USB: 메뉴 막대 앱 (설정, 빠른 전환)
```

상태: 펌웨어 0.6. Mac 2대를 실기로 확인했다 (BLE 마우스 입력이 활성 Mac으로 가고, 설정 화면에서 호스트를 바꾼다). 키보드 조합 전환과 Windows 호스트는 구현했지만 실기 확인 전이다.

라이선스: 아직 정하지 않았다. 라이선스를 넣기 전까지는 코드 재사용 권한을 주지 않는다.

## 폴더

| 경로 | 내용 |
|---|---|
| [firmware/main/](firmware/main/) | 펌웨어 (ESP-IDF v6.1, NimBLE). 입력 쪽 `ble_pair.c`, 리포트 해석 `hid_input.c`, 전환 `router.c`, 출력 쪽 `host.c` |
| [firmware/PROTOCOL.md](firmware/PROTOCOL.md) | USB 제어 프로토콜 (한 줄에 JSON 하나, 영어) |
| [app/](app/) | 메뉴 막대 / 트레이 앱 (Tauri): 보드 USB 포트를 맡고, 호스트를 빠르게 바꾸고, 설정 창을 띄운다 |
| [ui/](ui/) | 설정 화면 (React, Tailwind CSS, shadcn/ui. 브라우저에서도 Web Serial이나 개발용 브리지로 열린다). 브라우저 언어가 한국어면 한국어로 나온다 |
| [firmware/tools/](firmware/tools/) | 개발용 브리지 `bridge.py`, 플래시 `flash.sh`, 보드 관찰 `watch.py` (연결, 지연 통계, 장시간 기록, 알림) |
| [firmware/test/](firmware/test/) | 보드 없이 도는 시험 (C + JS) |

## 빌드, 플래시, 시험

```bash
. ~/esp/esp-idf/export.sh && cd firmware && idf.py build
```

```bash
firmware/tools/flash.sh
```

```bash
firmware/test/run.sh
```

메뉴 막대 앱은 [app/README.md](app/README.md), 설정 화면만은 [ui/README.md](ui/README.md) (영어).
