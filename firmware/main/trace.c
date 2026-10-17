// SPDX-License-Identifier: GPL-3.0-only
#include "trace.h"

#if CONFIG_KM_DIAG
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TRACE_RING 2048   /* Per link: over 10 s of every event at 7.5 ms */
#define TRACE_CHUNK 100   /* Marks per line */

/* Times in 10 us units (wraps after 11 hours; only differences are used). Written by the NimBLE host task, read
 * by the USB control task: a mark read while it is written is at worst one wrong value in a diagnostic */
static uint32_t s_ring[TRACE_LINKS][TRACE_RING];
static uint32_t s_count[TRACE_LINKS];

void trace_mark(int link, int64_t t_us)
{
    if (link < 0 || link >= TRACE_LINKS) {
        return;
    }
    s_ring[link][s_count[link] % TRACE_RING] = (uint32_t)(t_us / 10);
    s_count[link]++;
}

void trace_dump(int64_t at_us, int ms, void (*emit)(const char *json))
{
    static const char *names[TRACE_LINKS] = {"h1", "h2", "h3", "i1", "i2"};
    uint32_t at = (uint32_t)(at_us / 10), span = (uint32_t)ms * 100;
    char buf[TRACE_CHUNK * 7 + 96];
    for (int k = 0; k < TRACE_LINKS; k++) {
        uint32_t n = s_count[k], first = n > TRACE_RING ? n - TRACE_RING : 0;
        /* Oldest mark inside the window */
        uint32_t i = first;
        while (i < n && (uint32_t)(at - s_ring[k][i % TRACE_RING]) > span) {
            i++;
        }
        uint32_t prev = at - span;
        while (i < n) {
            int len = snprintf(buf, sizeof(buf), "{\"event\":\"trace\",\"link\":\"%s\",\"d\":[", names[k]);
            int m = 0;
            for (; i < n && m < TRACE_CHUNK; i++, m++) {
                uint32_t t = s_ring[k][i % TRACE_RING];
                if ((int32_t)(t - at) > 0) {
                    i = n;   /* After the requested moment */
                    break;
                }
                len += snprintf(buf + len, sizeof(buf) - len, "%s%lu", m ? "," : "", (unsigned long)(t - prev));
                prev = t;
            }
            snprintf(buf + len, sizeof(buf) - len, "]}");
            if (m) {
                emit(buf);
                vTaskDelay(pdMS_TO_TICKS(5));
            }
        }
    }
    snprintf(buf, sizeof(buf), "{\"event\":\"trace_end\",\"at\":%lu,\"ms\":%d}", (unsigned long)at, ms);
    emit(buf);
}
#endif
