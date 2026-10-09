#pragma once

#include <stdbool.h>
#include <stddef.h>

/*
 * 평평한 JSON 객체(값이 문자열, 숫자, 불리언뿐인 한 단계 객체)를 다루는 작은 도우미.
 * 라이브러리 없이 쓰려고 만들었다. 중첩 객체와 배열은 다루지 않는다.
 */

/* 문자열 값을 꺼낸다. 이스케이프는 풀고, ASCII가 아닌 \u 코드는 받지 않는다 */
bool json_get_string(const char *json, const char *key, char *out, size_t out_len);

/* 정수 값을 꺼낸다 */
bool json_get_int(const char *json, const char *key, int *out);

/* true 또는 false 값을 꺼낸다 */
bool json_get_bool(const char *json, const char *key, bool *out);

/* 응답에 넣을 문자열을 JSON에 맞게 이스케이프한다. 한글(UTF-8)은 그대로 둔다 */
void json_escape(const char *in, char *out, size_t out_len);

/* 바이트 길이로 자른 UTF-8 문자열에서, 끝에 덜 들어온 글자를 뺀 길이 (잘린 이름을 JSON에 깨지지 않게 싣는다) */
int utf8_complete_len(const char *s, int len);
