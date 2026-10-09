/*
 * json_mini.c 단위 시험: UTF-8 끝자리 정리 (GAP 이름을 23바이트로 자를 때), JSON 이스케이프.
 *   firmware/test/run.sh
 */
#include <stdio.h>
#include <string.h>

#include "../main/json_mini.h"

static int s_fail;
static int s_pass;

static void check_len(const char *name, const char *s, int len, int want)
{
    int got = utf8_complete_len(s, len);
    if (got == want) {
        s_pass++;
    } else {
        printf("FAIL  %-40s got %d want %d\n", name, got, want);
        s_fail++;
    }
}

int main(void)
{
    /* "Example’s Mac mini": ’ 는 E2 80 99 (3바이트) */
    const char *mac = "Example\xE2\x80\x99s Mac mini";
    check_len("ascii whole", "abc", 3, 3);
    check_len("empty", "", 0, 0);
    check_len("3-byte char complete", mac, 10, 10);
    check_len("3-byte char cut after 1", mac, 8, 7);
    check_len("3-byte char cut after 2", mac, 9, 7);
    check_len("whole mac name", mac, (int)strlen(mac), (int)strlen(mac));
    /* 한글 "키보드": 각 3바이트 */
    const char *ko = "\xED\x82\xA4\xEB\xB3\xB4\xEB\x93\x9C";
    check_len("hangul cut in 2nd char", ko, 5, 3);
    check_len("hangul whole", ko, 9, 9);
    /* 4바이트 (이모지) */
    const char *emoji = "a\xF0\x9F\x98\x80";
    check_len("4-byte cut", emoji, 4, 1);
    check_len("4-byte whole", emoji, 5, 5);
    check_len("only continuation bytes", "\x80\x80", 2, 0);
    check_len("bad lead byte dropped", "a\xFF", 2, 1);

    char out[64];
    /* 제어 문자는 공백으로 바꾼다 (줄 단위 프로토콜이 깨지지 않게) */
    json_escape("a\"b\\c\n", out, sizeof(out));
    if (strcmp(out, "a\\\"b\\\\c ") == 0) {
        s_pass++;
    } else {
        printf("FAIL  json_escape got [%s]\n", out);
        s_fail++;
    }
    /* 칸이 모자라면 글자 중간에서 끊지 않는다: "가나" (6바이트)를 5칸(문자 4바이트 + NUL)에 */
    char small[5];
    json_escape("\xEA\xB0\x80\xEB\x82\x98", small, sizeof(small));
    if (strcmp(small, "\xEA\xB0\x80") == 0) {
        s_pass++;
    } else {
        printf("FAIL  json_escape cut got %zu bytes\n", strlen(small));
        s_fail++;
    }
    if (s_fail) {
        printf("FAILED (%d passed, %d failed)\n", s_pass, s_fail);
        return 1;
    }
    printf("ALL PASSED (%d passed, 0 failed)\n", s_pass);
    return 0;
}
