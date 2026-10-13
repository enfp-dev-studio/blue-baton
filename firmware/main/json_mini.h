/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <stdbool.h>
#include <stddef.h>

/*
 * Small helpers for flat JSON objects (single-level objects whose values are only strings, numbers, booleans).
 * Written to avoid a library dependency. Nested objects and arrays are not handled.
 */

/* Get a string value. Escapes are decoded; non-ASCII \u codes are rejected */
bool json_get_string(const char *json, const char *key, char *out, size_t out_len);

/* Get an integer value */
bool json_get_int(const char *json, const char *key, int *out);

/* Get a true or false value */
bool json_get_bool(const char *json, const char *key, bool *out);

/* Escape a string for a JSON response. Non-ASCII text such as Korean (UTF-8) is left as is */
void json_escape(const char *in, char *out, size_t out_len);

/* Number of characters in a UTF-8 string, or -1 if it is not well-formed UTF-8 or has a control character */
int utf8_char_count(const char *s);

/* For a UTF-8 string cut at a byte length, the length without the incomplete trailing character (so a truncated name stays valid in JSON) */
int utf8_complete_len(const char *s, int len);
