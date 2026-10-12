/* platform.h on ESP32-S3 (ESP-IDF): time, heap, NimBLE bring-up, a microsecond timer, and the USB serial channel */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "platform.h"

#define CTL_LINE_MAX 256
#define CTL_TASK_STACK 8192

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
    return xTaskCreate(ctl_task, "usb_ctl", CTL_TASK_STACK, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void plat_ctl_write(const char *prefix, const char *text)
{
    printf("%s%s\n", prefix, text);
    fflush(stdout);
}
