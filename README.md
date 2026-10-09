# KM Router

블루투스 키보드·마우스 하나로 컴퓨터 3대(Mac 2 + Windows 1)를 오가는 라우터. ESP32-S3 SuperMini 한 개로 만든다.
입력 기기와 호스트 모두와 연결을 계속 유지하고, 입력을 보낼 호스트만 바꾼다 (전환 때 재연결 없음).

```text
BLE 키보드 ─┐                    ┌─ BLE HID → 호스트 1
BLE 마우스 ─┴─→ ESP32-S3 (NimBLE) ─┼─ BLE HID → 호스트 2
                  │                └─ BLE HID → 호스트 3
                  └─ USB: 설정 화면과 로그
```

지금 상태: [devlog/journal/00-overview.md](devlog/journal/00-overview.md) (펌웨어 0.5, Mac 2대 실기 확인)

## 폴더

| 경로 | 내용 |
|---|---|
| [firmware/main/](firmware/main/) | 펌웨어 (ESP-IDF v6.1, NimBLE). 입력 쪽 `ble_pair.c`, 해석 `hid_input.c`, 전환 `router.c`, 출력 쪽 `host.c` |
| [firmware/PROTOCOL.md](firmware/PROTOCOL.md) | USB 제어 프로토콜 (한 줄 JSON) |
| [firmware/ui/](firmware/ui/) | 설정 화면 (빌드 없는 ES 모듈, 로컬 브리지·Web Serial·모의 보드) |
| [firmware/tools/](firmware/tools/) | 로컬 브리지 `bridge.py`, 플래시 `flash.sh` |
| [firmware/test/](firmware/test/) | 보드 없이 도는 시험 (C + JS) |
| [devlog/](devlog/) | 제작 기록: 설계 결정, 작업 일지, 제안서, 원본 로그 |
| [km_router_handoff.md](km_router_handoff.md) | 최초 설계 문서 |

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

설정 화면 여는 법은 [firmware/ui/README.md](firmware/ui/README.md), 처음부터 따라 하기는 [devlog/journal/10-reproduce.md](devlog/journal/10-reproduce.md).
