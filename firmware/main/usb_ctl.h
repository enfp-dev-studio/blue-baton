/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include "esp_err.h"

/* Start the USB command reader task. Each command is one line of JSON (see PROTOCOL.md) */
esp_err_t usb_ctl_start(void);

/* One outgoing line from the board. Printed with the "KM< " prefix */
void usb_ctl_emit(const char *json);
