/* SPDX-License-Identifier: GPL-3.0-only */
/* platform.h on ESP32-S3 (ESP-IDF): time, heap, NimBLE bring-up, a microsecond timer, and the USB serial channel */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "platform.h"

#define CTL_LINE_MAX 256
#define CTL_TASK_STACK 8192
#define OUT_BUF_BYTES 8192     /* Lines waiting for the computer to read them */
#define OUT_TASK_STACK 4096
#define OUT_TASK_CORE (portNUM_PROCESSORS - 1)   /* The other core where there are two: NimBLE and the controller run on core 0 */

int64_t plat_now_us(void)
{
    return esp_timer_get_time();
}

uint32_t plat_heap_free(void)
{
    return esp_get_free_heap_size();
}

uint32_t plat_heap_min(void)
{
    return esp_get_minimum_free_heap_size();
}

/* ---------- NimBLE ---------- */

void ble_store_config_init(void);   /* NimBLE's NVS-backed bond store (no public header; the ESP-IDF examples declare it so) */

static void host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t plat_ble_init(void)
{
    return nimble_port_init();
}

void plat_ble_start(void)
{
    ble_store_config_init();
    nimble_port_freertos_init(host_task);
}

/* ---------- Microsecond timer ---------- */

struct plat_timer {
    esp_timer_handle_t handle;
};

plat_timer_t *plat_timer_create(void (*fn)(void *arg), void *arg)
{
    plat_timer_t *t = calloc(1, sizeof(*t));
    if (!t) {
        return NULL;
    }
    const esp_timer_create_args_t args = {.callback = fn, .arg = arg, .name = "plat"};
    if (esp_timer_create(&args, &t->handle) != ESP_OK) {
        free(t);
        return NULL;
    }
    return t;
}

void plat_timer_start_periodic(plat_timer_t *t, uint64_t period_us)
{
    esp_timer_start_periodic(t->handle, period_us);
}

void plat_timer_stop(plat_timer_t *t)
{
    esp_timer_stop(t->handle);
}

/* ---------- Control channel: USB Serial/JTAG console (stdin/stdout) ---------- */

/*
 * Lines out. A USB write waits until the computer reads it (no USB Serial/JTAG driver: the console writes straight
 * to the FIFO), so writers only copy the line into a buffer and a task on the other core writes it out. The
 * Bluetooth task never waits on USB. If the computer stops reading and the buffer fills, new lines are dropped.
 */
static RingbufHandle_t s_out;
static uint32_t s_out_dropped;

static void out_task(void *arg)
{
    while (1) {
        size_t n;
        char *line = xRingbufferReceive(s_out, &n, portMAX_DELAY);
        if (line) {
            fwrite(line, 1, n, stdout);
            fflush(stdout);
            vRingbufferReturnItem(s_out, line);
        }
    }
}

#if CONFIG_KM_DIAG
static portMUX_TYPE s_ctl_diag_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_ctl_lines, s_ctl_total_us, s_ctl_max_us;

void plat_ctl_diag_take(uint32_t *lines, uint32_t *total_us, uint32_t *max_us, uint32_t *dropped)
{
    taskENTER_CRITICAL(&s_ctl_diag_lock);
    *lines = s_ctl_lines;
    *total_us = s_ctl_total_us;
    *max_us = s_ctl_max_us;
    *dropped = s_out_dropped;
    s_ctl_lines = s_ctl_total_us = s_ctl_max_us = s_out_dropped = 0;
    taskEXIT_CRITICAL(&s_ctl_diag_lock);
}
#endif

void plat_ctl_write(const char *prefix, const char *text)
{
#if CONFIG_KM_DIAG
    int64_t t0 = esp_timer_get_time();
#endif
    size_t np = strlen(prefix), nt = strlen(text);
    char *slot = NULL;
    if (!s_out) {
        printf("%s%s\n", prefix, text);   /* Before the output task starts (boot) */
        fflush(stdout);
    } else if (xRingbufferSendAcquire(s_out, (void **)&slot, np + nt + 1, 0) == pdTRUE) {
        memcpy(slot, prefix, np);
        memcpy(slot + np, text, nt);
        slot[np + nt] = '\n';
        xRingbufferSendComplete(s_out, slot);
    } else {
        s_out_dropped++;
    }
#if CONFIG_KM_DIAG
    uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
    taskENTER_CRITICAL(&s_ctl_diag_lock);
    s_ctl_lines++;
    s_ctl_total_us += us;
    s_ctl_max_us = us > s_ctl_max_us ? us : s_ctl_max_us;
    taskEXIT_CRITICAL(&s_ctl_diag_lock);
#endif
}


static void (*s_on_line)(const char *line);

static void ctl_task(void *arg)
{
    char line[CTL_LINE_MAX];
    while (1) {
        if (fgets(line, sizeof(line), stdin) == NULL) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
            line[--n] = '\0';
        }
        if (n > 0) {
            s_on_line(line);
        }
    }
}

esp_err_t plat_ctl_start(void (*on_line)(const char *line))
{
    s_on_line = on_line;
    RingbufHandle_t out = xRingbufferCreate(OUT_BUF_BYTES, RINGBUF_TYPE_NOSPLIT);
    if (!out || xTaskCreatePinnedToCore(out_task, "usb_out", OUT_TASK_STACK, NULL, 4, NULL, OUT_TASK_CORE) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_out = out;
    return xTaskCreate(ctl_task, "usb_ctl", CTL_TASK_STACK, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

