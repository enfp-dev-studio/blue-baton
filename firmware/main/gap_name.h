#pragma once

#include <stdint.h>

/*
 * Read the peer's full GAP Device Name (0x2A00) (both input devices and hosts).
 *
 * A Read By Type response carries at most ATT_MTU-4 bytes of the value (Core v5.4 Vol 3 Part F 3.4.4.2).
 * Before the MTU changes it is cut at 19 bytes ("Example’s Mac mini" becomes "Example’s Mac min").
 * So if it may be truncated, continue with Read Blob (long read) on the obtained handle (same part, 3.4.4.5).
 * Reads up to the stored name length (GAP_NAME_MAX), and drops a UTF-8 character cut off at the end.
 * Call only from the NimBLE host task.
 */

#define GAP_NAME_MAX 23   /* Bytes that fit in cfg_paired_t.name */

typedef void (*gap_name_fn)(uint16_t conn, const char *name);

/* 0 means the read started. Then fn is always called once: with the name, or an empty string on failure */
int gap_name_read(uint16_t conn, gap_name_fn fn);
