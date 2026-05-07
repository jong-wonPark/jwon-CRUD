#ifndef JSON_H
#define JSON_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ───────────────────────── 타입 정의 ───────────────────────── */

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} json_type_t;

typedef struct json_value  json_value_t;
typedef struct json_member json_member_t;

struct json_member {
    char          *key;
    json_value_t  *value;
    json_member_t *next;
};

struct json_value {
    json_type_t type;
    union {
        int            boolean;   /* JSON_BOOL   */
        double         number;    /* JSON_NUMBER */
        char          *string;    /* JSON_STRING */
        struct {                  /* JSON_ARRAY  */
            json_value_t **items;
            size_t         count;
            size_t         capacity;
        } array;
        struct {                  /* JSON_OBJECT */
            json_member_t *head;
            size_t         count;
        } object;
    } u;
};

/* ───────────────────────── 파싱 API ───────────────────────── */

/* JSON 문자열을 파싱하여 json_value_t 트리를 반환합니다.
   실패 시 NULL. 성공 시 json_free()로 해제해야 합니다. */
json_value_t *json_parse(const char *src);

/* 파일에서 JSON을 읽어 파싱합니다. */
json_value_t *json_parse_file(const char *path);

/* ───────────────────────── 직렬화 API ─────────────────────── */

/* json_value_t 트리를 JSON 문자열로 직렬화합니다.
   indent < 0 이면 compact, 0 이상이면 해당 공백 수로 들여쓰기.
   반환된 문자열은 free()로 해제해야 합니다. */
char *json_stringify(const json_value_t *val, int indent);

/* JSON을 파일에 저장합니다. 성공 시 0, 실패 시 -1. */
int json_save_file(const json_value_t *val, const char *path, int indent);

/* ───────────────────────── 생성 API ───────────────────────── */

json_value_t *json_make_null(void);
json_value_t *json_make_bool(int boolean);
json_value_t *json_make_number(double number);
json_value_t *json_make_string(const char *str);
json_value_t *json_make_array(void);
json_value_t *json_make_object(void);

/* ───────────────────────── 배열 API ───────────────────────── */

/* 배열 끝에 값을 추가합니다 (소유권 이전). 성공 시 0. */
int json_array_push(json_value_t *array, json_value_t *item);

/* 배열의 i번째 요소를 반환합니다. 범위 초과 시 NULL. */
json_value_t *json_array_get(const json_value_t *array, size_t i);

/* 배열 요소 수를 반환합니다. */
size_t json_array_length(const json_value_t *array);

/* ───────────────────────── 객체 API ───────────────────────── */

/* 객체에 키-값 쌍을 추가하거나 덮어씁니다 (소유권 이전). 성공 시 0. */
int json_object_set(json_value_t *object, const char *key, json_value_t *val);

/* 키에 해당하는 값을 반환합니다. 없으면 NULL. */
json_value_t *json_object_get(const json_value_t *object, const char *key);

/* 키를 삭제합니다. 성공 시 0, 없으면 -1. */
int json_object_delete(json_value_t *object, const char *key);

/* 객체의 키 수를 반환합니다. */
size_t json_object_count(const json_value_t *object);

/* ───────────────────────── 유틸리티 API ───────────────────── */

/* json_value_t 트리 전체를 해제합니다. */
void json_free(json_value_t *val);

/* 타입 이름 문자열을 반환합니다 (정적 문자열). */
const char *json_type_name(json_type_t type);

/* 깊은 복사를 수행합니다. */
json_value_t *json_clone(const json_value_t *val);

/* 타입 확인 매크로 */
#define json_is_null(v)   ((v) && (v)->type == JSON_NULL)
#define json_is_bool(v)   ((v) && (v)->type == JSON_BOOL)
#define json_is_number(v) ((v) && (v)->type == JSON_NUMBER)
#define json_is_string(v) ((v) && (v)->type == JSON_STRING)
#define json_is_array(v)  ((v) && (v)->type == JSON_ARRAY)
#define json_is_object(v) ((v) && (v)->type == JSON_OBJECT)

/* 값 빠른 접근 매크로 */
#define json_bool_val(v)   ((v)->u.boolean)
#define json_number_val(v) ((v)->u.number)
#define json_string_val(v) ((v)->u.string)

#ifdef __cplusplus
}
#endif

#endif /* JSON_H */
