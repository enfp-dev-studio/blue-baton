/* SPDX-License-Identifier: GPL-3.0-only */
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "json_mini.h"

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void skip_space(const char **p)
{
    while (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n') {
        (*p)++;
    }
}

/* Find where the value after "key" starts. NULL if absent */
static const char *find_value(const char *json, const char *key)
{
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) {
        return NULL;
    }
    p += strlen(pattern);
    skip_space(&p);
    if (*p != ':') {
        return NULL;
    }
    p++;
    skip_space(&p);
    return p;
}

bool json_get_string(const char *json, const char *key, char *out, size_t out_len)
{
    const char *p = find_value(json, key);
    if (!p || *p != '"') {
        return false;
    }
    p++;

    size_t n = 0;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\') {
            char e = *p++;
            switch (e) {
            case '"': case '\\': case '/': c = e; break;
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'u': {
                int code = 0;
                for (int i = 0; i < 4; i++) {
                    int v = *p ? hex_value(*p++) : -1;
                    if (v < 0) {
                        return false;
                    }
                    code = (code << 4) | v;
                }
                if (code >= 0x80) {
                    return false;
                }
                c = (char)code;
                break;
            }
            default:
                return false;
            }
        }
        if (n + 1 >= out_len) {
            return false;
        }
        out[n++] = c;
    }
    if (*p != '"') {
        return false;
    }
    out[n] = '\0';
    return true;
}

bool json_get_int(const char *json, const char *key, int *out)
{
    const char *p = find_value(json, key);
    if (!p) {
        return false;
    }
    bool neg = false;
    if (*p == '-') {
        neg = true;
        p++;
    }
    if (!isdigit((unsigned char)*p)) {
        return false;
    }
    long v = 0;
    while (isdigit((unsigned char)*p)) {
        v = v * 10 + (*p - '0');
        if (v > 1000000000L) {
            return false;
        }
        p++;
    }
    *out = (int)(neg ? -v : v);
    return true;
}

bool json_get_bool(const char *json, const char *key, bool *out)
{
    const char *p = find_value(json, key);
    if (!p) {
        return false;
    }
    if (strncmp(p, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if (strncmp(p, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}

void json_escape(const char *in, char *out, size_t out_len)
{
    size_t n = 0;
    for (; *in; in++) {
        unsigned char c = (unsigned char)*in;
        if (c < 0x20) {
            c = ' ';
        }
        size_t need = (c == '"' || c == '\\') ? 2 : 1;
        if (n + need + 1 > out_len) {
            break;
        }
        if (need == 2) {
            out[n++] = '\\';
        }
        out[n++] = (char)c;
    }
    out[utf8_complete_len(out, (int)n)] = '\0';   /* If cut short for lack of room, drop the incomplete character */
}

int utf8_complete_len(const char *s, int len)
{
    /* Find the lead byte of the last character and check that the whole character is present */
    int start = len;
    while (start > 0 && ((unsigned char)s[start - 1] & 0xC0) == 0x80) {
        start--;
    }
    if (start == 0) {
        return 0;   /* Only continuation bytes: not a character */
    }
    start--;
    unsigned char lead = (unsigned char)s[start];
    int need = lead < 0x80 ? 1 : (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : (lead & 0xF8) == 0xF0 ? 4 : 0;
    return need != 0 && len - start >= need ? len : start;
}

int utf8_char_count(const char *s)
{
    int n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; n++) {
        unsigned char c = *p++;
        int more = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
        if (more < 0 || c < 0x20 || c == 0x7F || c == 0xC0 || c == 0xC1 || c > 0xF4) {
            return -1;
        }
        uint32_t code = more ? c & (0x3F >> more) : c;
        for (int i = 0; i < more; i++, p++) {
            if ((*p & 0xC0) != 0x80) {
                return -1;
            }
            code = (code << 6) | (*p & 0x3F);
        }
        /* Overlong forms, UTF-16 surrogates and code points past U+10FFFF are not UTF-8 */
        static const uint32_t min[] = {0, 0x80, 0x800, 0x10000};
        if (code < min[more] || (code >= 0xD800 && code <= 0xDFFF) || code > 0x10FFFF) {
            return -1;
        }
    }
    return n;
}
