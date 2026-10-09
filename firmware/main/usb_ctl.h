#pragma once

#include "esp_err.h"

/* USB 명령 읽기 작업을 시작한다. 명령은 한 줄의 JSON이다 (PROTOCOL.md 참고) */
esp_err_t usb_ctl_start(void);

/* 보드에서 밖으로 보내는 한 줄. "KM< " 접두사를 붙여 출력한다 */
void usb_ctl_emit(const char *json);
