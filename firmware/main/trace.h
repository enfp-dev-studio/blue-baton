// SPDX-License-Identifier: GPL-3.0-only
#pragma once
/*
 * Developer builds (CONFIG_KM_DIAG): when each radio link last showed it was served. Host links: the controller
 * reported a packet completed (HCI Number Of Completed Packets). Input links: a notification came in. The times show
 * which connection events of each link took place, so the events before a link timeout can be compared across links.
 */
#include <stdint.h>
#include "sdkconfig.h"

#define TRACE_HOST(i) (i)          /* Host links 0-2 */
#define TRACE_INPUT(i) (3 + (i))   /* Input links 0-1 */
#define TRACE_LINKS 5

#if CONFIG_KM_DIAG
void trace_mark(int link, int64_t t_us);
/* Lines {"event":"trace",...}: every mark in the ms before at_us, per link. Call from the USB control task: it waits
 * between lines so the output buffer keeps up */
void trace_dump(int64_t at_us, int ms, void (*emit)(const char *json));
#else
static inline void trace_mark(int link, int64_t t_us) { (void)link; (void)t_us; }
#endif
