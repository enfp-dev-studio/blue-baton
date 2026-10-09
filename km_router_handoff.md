# Multi-Host Bluetooth KM Router — Session Handoff

## 목표
여러 대의 Mac mini를 각각 별도 모니터에 연결해 사용하는 환경에서, 키보드와 마우스를 버튼식 KVM/KM 스위치 없이 빠르게 전환하기 위한 소형 장치를 직접 만든다.

핵심 목표는 **각 Mac과 Bluetooth 연결을 계속 유지한 채, 키보드/마우스 입력 패킷의 목적지만 바꾸는 것**이다.

## 왜 만들려고 했는가
현재 문제:
- 여러 Mac mini 사이에서 키보드/마우스를 자주 전환해야 함.
- 일반 USB/KM 스위치는 전환 시 재인식 지연이 생길 수 있음.
- Apple Universal Control은 편하지만 가끔 입력 누락, 한/영 전환 꼬임 등의 문제가 있음.
- 물리 버튼을 눌러 전환하는 것도 번거로움.

원하는 사용감:
- Mac 1 / 2 / 3과 항상 연결 유지.
- 전환 시 재페어링/재연결 없음.
- 키보드와 마우스가 항상 같은 Mac으로 함께 전환.
- 향후 Mac 앱의 단축키/매크로로 Host를 전환.
- 가능하면 물리 버튼 없이 사용.

## 기본 구조

```text
Bluetooth Keyboard ─┐
Bluetooth Mouse ─────┤
                     ↓
               ESP32-S3 Router
                ├─ BLE HID → Mac 1
                ├─ BLE HID → Mac 2
                └─ BLE HID → Mac 3
```

중앙 장치는:
- 입력 측에서는 BLE Central 역할로 키보드/마우스에 연결
- 출력 측에서는 BLE Peripheral/HID Device 역할로 Mac 3대에 연결
- 총 5개 BLE connection을 동시에 유지
  - Keyboard 1
  - Mouse 1
  - Mac 3

Mac 한 대당 BLE 연결 하나 안에서 Keyboard + Mouse HID Report를 함께 제공한다.

전환은 연결을 끊는 방식이 아니라:

```text
activeHost = 0 / 1 / 2
```

만 바꾸고 이후 HID report를 선택된 Mac connection으로만 보낸다.

## 왜 ESP32-S3 SuperMini를 샀는가
구매한 보드: **ESP32-S3 SuperMini**

선택 이유:
- BLE multi-connection 지원
- Central + Peripheral 역할 동시 수행 가능
- BLE HID 구현 가능
- Wi-Fi도 있어 향후 설정 UI/OTA/네트워크 기능 확장 가능
- USB-C로 전원 공급 및 펌웨어 업로드 가능
- 매우 작아서 최종 제품 형태를 상상하기 좋음
- 가격이 약 13,000원으로 비교적 저렴
- 브레드보드를 사용하지 않으므로 핀헤더 없는 버전으로 구매
- 현재 1차 KM 프로토타입에는 별도 납땜이나 추가 부품이 필요하지 않음

## 1차 구현 범위
우선 아래 기능만 구현해서 실사용 가능 여부를 검증한다.

1. ESP32-S3에서 BLE 초기화
2. Bluetooth 키보드 연결
3. Bluetooth 마우스 연결
4. Mac 3대와 BLE HID 연결 및 bonding
5. 5개 연결을 동시에 유지
6. Keyboard HID report 수신
7. Mouse HID report 수신
8. Keyboard/Mouse report를 동일한 `activeHost`로 전달
9. 소프트웨어 명령으로 `activeHost` 변경
10. Mac 전환 시 연결 재수립 없이 즉시 입력되는지 확인

## 설정 관련
ESP-NimBLE을 사용할 예정이며 기본 최대 연결 수가 부족할 수 있으므로 최소 5개 이상으로 설정해야 한다.

예:
```text
CONFIG_BT_NIMBLE_MAX_CONNECTIONS >= 5
```

스캔/Advertising 등 추가 BLE activity도 있으므로 실제 설정에서는 약간의 여유를 두는 편이 좋다.

## 향후 확장 아이디어
1차 KM이 성공하면 다음을 검토한다.

- Mac용 경량 설정 앱
  - Host 1/2/3 이름 지정
  - 단축키 설정
  - 현재 Host 전환
  - 페어링 관리
  - 펌웨어 업데이트

- 마우스 화면 경계 기반 자동 전환
- Wi-Fi 기반 설정 페이지
- OTA 업데이트
- 향후 ESP32-S31로 이전
  - Bluetooth Classic + BLE
  - HFP 마이크/오디오까지 현재 Host와 함께 전환하는 실험

## 최종 제품 방향
장기적으로는 작은 독립형 장치를 목표로 한다.

예상 형태:
- 약 30~35mm급 소형 케이스
- USB-C 암포트 1개
  - 전원 + 설정 통신
- 물리 Host 전환 버튼 없음
- 필요하면 숨겨진 Reset/Pair 핀홀만 제공
- 키보드/마우스 입력은 Bluetooth 또는 향후 USB/동글까지 확장
- Mac 3대는 항상 연결 유지
- 전환은 소프트웨어/키보드 매크로로 처리

## 가장 중요한 설계 원칙
이 프로젝트는 일반적인 “KM Switch”가 아니라:

**Persistent Multi-Host HID Router**

이다.

즉, 장치를 재연결하는 것이 아니라 **연결은 항상 유지하고 HID report destination만 변경한다.**
