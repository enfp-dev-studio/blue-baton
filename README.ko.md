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

상태: 버전 0.7.5. 입력 기기는 2대까지(키보드와 마우스, 또는 아무 두 대), 호스트는 3대. 키보드와 마우스를 동시에 연결하고,
Mac 2대·Windows PC·iPhone을 호스트로 실기 확인했다. 입력은 활성 호스트로 가고, 키 조합과 설정 창 모두로 호스트를 바꾼다.
앱은 macOS와 Windows에서 확인했다. 실기 확인 전: 호스트 패스키를 키보드로 입력하기(앱 화면 입력은 확인), 시스템 키(전원·잠자기·깨우기),
Linux 호스트, Linux용 앱.

## 준비물

- 플래시 4MB 이상인 ESP32-S3 보드. 칩 자체의 USB 포트로 연결한다 (포트가 두 개인 보드는 UART가 아니라 USB라고 적힌 쪽).
  ESP32-S3 SuperMini로 확인했다. 다른 칩은 [보드](#보드) 참고.
- 블루투스 LE 키보드와 마우스. 블루투스 클래식만 되는 기기는 안 된다: ESP32-S3에는 블루투스 LE만 있다.
- 블루투스 LE가 되는 컴퓨터 3대까지.
- 그중 한 대에 블루 바톤 앱을 설치하고 보드를 USB로 꽂는다. 케이블로 보드에 전원이 들어가고, 기기 페어링과 설정은 앱에서
  한다.

## 보드

| 보드 | 칩 | 릴리스와 앱 | 빌드 | 실제 보드에서 실행 |
|---|---|---|---|---|
| ESP32-S3 SuperMini (플래시 4MB) | ESP32-S3 | 지원 | 됨 | 확인 (이 보드로 개발·측정) |
| M5Stack AtomS3 Lite (케이스 포함) | ESP32-S3 | 지원, 같은 펌웨어 | 됨 | 아직 |
| M5Stack NanoC6 (케이스 포함) | ESP32-C6 | 미지원: 직접 빌드 | 됨 | 일부: 앱·키보드는 동작, 마우스 하나는 연결 안 됨 ([#2](https://github.com/enfp-dev-studio/blue-baton/issues/2)) |
| 다른 ESP32-S3 보드 (플래시 4MB 이상, 칩 자체 USB 포트) | ESP32-S3 | 지원, 같은 펌웨어 | 됨 | 안 해 봄 |
| ESP32-C3 보드 | ESP32-C3 | 미지원: 직접 빌드 | 됨 | 안 해 봄 |
| ESP32-C5 보드 | ESP32-C5 | 미지원: 직접 빌드 | 됨 | 안 해 봄 |
| 다른 ESP32-C6 보드 | ESP32-C6 | 미지원: 직접 빌드 | 됨 | 안 해 봄 |
| ESP32-H2 보드 | ESP32-H2 | 미지원: 직접 빌드 | 됨 | 안 해 봄 |
| ESP32 (초기형) 보드 | ESP32 | 미지원 | 됨 | 안 해 봄 |
| ESP32-S2 보드 | ESP32-S2 | 미지원 | 되지만 칩에 블루투스 무선부가 없음 | 안 해 봄 |
| nRF52840, Raspberry Pi Pico W, Raspberry Pi | — | 미지원 | 안 됨 | 안 해 봄 |

- ESP32-C3·C5·C6·H2는 S3와 같은 ESP-IDF, NimBLE, USB 포트를 쓰지만 코어가 하나다 (클럭: C3·C6 160 MHz, H2 96 MHz,
  C5는 S3와 같은 240 MHz). 지연과 연결 안정성은 재 보지 않았다. 빌드와 설치 (`firmware/`에서):
  `idf.py -B build-esp32c3 -DIDF_TARGET=esp32c3 build`, 그다음 `idf.py -B build-esp32c3 -p <포트> flash`. 이 보드들에서
  써 본 결과를 알려 주면 반영한다.
- 초기형 ESP32에는 앱이 보드와 통신하는 USB Serial/JTAG가 없다 (콘솔이 UART로 나간다). ESP32-S2에는 블루투스 무선부가 없다. nRF52840, Pico W,
  Raspberry Pi는 블루투스 스택이 달라서 펌웨어의 블루투스 부분을 새로 짜야 한다.

## 시작하기

1. **앱 설치**: [최신 릴리스](https://github.com/enfp-dev-studio/blue-baton/releases/latest)에서 받는다.
   - macOS (Apple 실리콘): `.dmg`, 서명·공증됨.
   - Windows: `_x64-setup.exe` (관리자 권한 없이 사용자 계정에 설치). 코드 서명이 없어서 SmartScreen 경고가 나올 수
     있다: 추가 정보 → 실행.
   - Linux (experimental): `.deb`, `.rpm`, `.AppImage`. 보드의 시리얼 포트를 쓰려면 사용자가 `dialout` 그룹에 있어야 한다
     (`sudo usermod -aG dialout $USER` 후 로그아웃했다가 다시 로그인).
2. **보드 연결**: 보드를 꽂고 앱을 연다. 블루 바톤이 없는 보드면 "이 보드에는 아직 블루 바톤이 없어요"가 나온다:
   **블루 바톤 설치**를 누른다. 펌웨어는 앱에 들어 있어서 따로 설치할 것이 없다. 보드를 찾지 못하면 BOOT 버튼을 누른 채로
   꽂는다.
3. **키보드와 마우스 추가**: 입력 기기 → 기기 추가…, 기기를 페어링 모드로 두고 목록에 나오면 연결을 누른다.
4. **컴퓨터 추가**: 호스트 → 빈 줄의 추가. 3분 동안 보드가 검색된다. 그 컴퓨터에서:
   - macOS: 시스템 설정 → Bluetooth에서 "Blue Baton" 옆 연결.
   - Windows: 설정 → Bluetooth 및 장치 → 장치 추가 → Bluetooth → "Blue Baton".

   컴퓨터에 숫자 6자리가 나오면 앱에 입력한다 (보드에 연결된 키보드로 쳐도 된다).
5. **전환**: `Alt+1`, `2`, `3` (Mac 키보드는 Option), 또는 메뉴 막대 아이콘. 키 조합은 앱에서 바꿀 수 있다.
   호스트 줄의 메뉴에서 이름(8자까지, 번호 대신 보임)을 정하고, 다시 연결하지 않고 순서를 위아래로 바꿀 수 있다.

## 업데이트

앱은 GitHub Releases에서 스스로 업데이트한다: 켜고 1분 뒤와 하루 한 번 확인하고, 메뉴에 업데이트 확인…이 있다. 앱
릴리스마다 같은 버전의 펌웨어가 들어 있다. 보드의 펌웨어가 더 낮으면 설정 창에 펌웨어 → 업데이트가 나온다. 업데이트해도
페어링한 기기와 컴퓨터는 그대로다.

## 한계와 알려진 문제

- 입력 기기 2대, 컴퓨터 3대까지.
- 입력 기기와 컴퓨터 모두 블루투스 LE만.
- 커서가 움직이는 횟수는 블루투스 LE의 연결 간격에 묶인다. 입력 받는 컴퓨터는 연결 간격마다 한 번 입력을 받는다.
  Windows는 7.5 ms(초당 약 133번)를 받았다. macOS는 15 ms(초당 약 66번)로 유지하고, 보드가 요청한 더 짧은 간격은
  모두 거절했다. 60 Hz 화면의 Mac에서 앱이 받은 커서 이동은 보드로 초당 약 60번, 2.4 GHz 수신기와 같았다. macOS가
  화면 한 프레임에 한 번까지만 넘기기 때문이다. 게이밍 마우스의 수신기 1,000 Hz는 이어지지 않는다. 보드를 거치면
  그 마우스의 블루투스 모드로 동작한다.
- 연결이 가끔 끊기고(연결 시간 초과, 우리 환경에서 하루 여러 번) 스스로 다시 붙는다. 원인은 아직 모른다.
- [pid.codes](https://pid.codes)에서 USB ID를 받기 전까지, 보드는 컴퓨터에 임시 제조사·제품 번호를 알린다.

## 앱 없이 펌웨어 설치

릴리스마다 [esptool](https://docs.espressif.com/projects/esptool/)용 펌웨어 파일도 있다 (`pip install esptool`):

```bash
esptool --chip esp32s3 write-flash 0x0 blue-baton-firmware-v0.7.5-esp32s3.bin
```

는 전체 이미지를 쓰고(처음 설치. 페어링도 지운다),

```bash
esptool --chip esp32s3 write-flash 0x20000 blue-baton-firmware-v0.7.5-esp32s3-app.bin
```

는 페어링을 그대로 두고 펌웨어만 업데이트한다.

## 폴더

| 경로 | 내용 |
|---|---|
| [firmware/main/](firmware/main/) | 펌웨어 (ESP-IDF v6.1, NimBLE). 입력 쪽 `ble_pair.c`, 리포트 해석 `hid_input.c`, 전환 `router.c`, 출력 쪽 `host.c` |
| [firmware/PROTOCOL.md](firmware/PROTOCOL.md) | USB 제어 프로토콜 (한 줄에 JSON 하나, 영어) |
| [app/](app/) | 메뉴 막대 앱 (Tauri): 보드 USB 포트를 맡고, 호스트를 빠르게 바꾸고, 설정 창을 띄우고, 들어 있는 펌웨어를 보드에 쓴다 |
| [ui/](ui/) | 설정 화면 (React, Tailwind CSS, shadcn/ui. 브라우저에서도 Web Serial이나 개발용 브리지로 열린다). 8개 언어, 시스템 언어를 따르거나 창에서 고른다 |
| [firmware/tools/](firmware/tools/) | 릴리스 파일 `package.sh`, 개발용 브리지 `bridge.py`, 플래시 `flash.sh`, 보드 관찰 `watch.py` (연결, 지연 통계, 장시간 기록, 알림) |
| [firmware/test/](firmware/test/) | 보드 없이 도는 시험: 펌웨어 C 시험. `run.sh`는 설정 화면 JS 시험(`ui/test/`)도 돌린다 |

## 빌드, 플래시, 시험

공개용 빌드 (개발 진단 꺼짐). `firmware/tools/package.sh <버전> <폴더>`는 릴리스 파일 두 개도 만든다:

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

## 라이선스

[GNU GPL v3](LICENSE) (GPL-3.0-only). Copyright (C) 2026 ENFPDEV (ENFP-Dev-Studio). GPL 조건 없이 쓰는 상용 라이선스는 문의:
contact@enfpdev.com. 기여에는 [CLA](CLA.md) 동의가 필요하다 ([CONTRIBUTING.md](CONTRIBUTING.md)). `ui/src/components/ui/`는
[shadcn/ui](https://ui.shadcn.com)(MIT)에서 가져왔다. 앱은 [espflash](https://github.com/esp-rs/espflash)(MIT 또는
Apache-2.0)로 펌웨어를 쓴다.
