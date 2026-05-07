/*
 * Key-Value Store CRUD 콘솔 애플리케이션
 * JSON 파일(store.json)로 데이터를 영속 관리합니다.
 *
 * 데이터 구조:
 * {
 *   "store": [
 *     { "id": 1, "key": "설정명", "value": "값" },
 *     ...
 *   ],
 *   "next_id": 2
 * }
 */

#define _CRT_SECURE_NO_WARNINGS

#ifdef _WIN32
#include <windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "json.h"

#define DATA_FILE  "store.json"
#define MAX_INPUT  1024

/* ── 입력 버퍼 비우기 ── */
static void clear_input(void)
{
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

/* ── 구분선 출력 ── */
static void print_line(void)
{
    printf("--------------------------------------------------------\n");
}

/* ───────────────────────────────────────────────────────────
 *  파일 I/O
 * ─────────────────────────────────────────────────────────── */

static json_value_t *load_store(void)
{
    /* 파일 존재 여부를 먼저 확인 — 없으면 빈 스토어 생성 */
    FILE *fp = fopen(DATA_FILE, "rb");
    if (!fp) {
        json_value_t *root = json_make_object();
        json_object_set(root, "store",   json_make_array());
        json_object_set(root, "next_id", json_make_number(1));
        return root;
    }
    fclose(fp);

    json_value_t *root = json_parse_file(DATA_FILE);
    if (!root || !json_is_object(root)) {
        if (root) json_free(root);
        root = json_make_object();
        json_object_set(root, "store",   json_make_array());
        json_object_set(root, "next_id", json_make_number(1));
    }
    return root;
}

static int save_store(json_value_t *root)
{
    if (json_save_file(root, DATA_FILE, 2) != 0) {
        printf("[오류] 파일 저장 실패: %s\n", DATA_FILE);
        return -1;
    }
    return 0;
}

/* ───────────────────────────────────────────────────────────
 *  내부 헬퍼
 * ─────────────────────────────────────────────────────────── */

/* ID로 배열 내 인덱스 반환, 없으면 -1 */
static int find_by_id(json_value_t *arr, int id)
{
    size_t n = json_array_length(arr);
    for (size_t i = 0; i < n; i++) {
        json_value_t *item  = json_array_get(arr, i);
        json_value_t *id_v  = json_object_get(item, "id");
        if (id_v && (int)json_number_val(id_v) == id)
            return (int)i;
    }
    return -1;
}

/* 키 문자열로 배열 내 인덱스 반환, 없으면 -1 */
static int find_by_key(json_value_t *arr, const char *key)
{
    size_t n = json_array_length(arr);
    for (size_t i = 0; i < n; i++) {
        json_value_t *item  = json_array_get(arr, i);
        json_value_t *key_v = json_object_get(item, "key");
        if (key_v && strcmp(json_string_val(key_v), key) == 0)
            return (int)i;
    }
    return -1;
}

/* 배열에서 특정 인덱스 항목 제거 및 메모리 해제 */
static void array_remove_at(json_value_t *arr, size_t idx)
{
    size_t n = arr->u.array.count;
    json_free(arr->u.array.items[idx]);
    for (size_t i = idx; i + 1 < n; i++)
        arr->u.array.items[i] = arr->u.array.items[i + 1];
    arr->u.array.count--;
}

/* ───────────────────────────────────────────────────────────
 *  CRUD 함수
 * ─────────────────────────────────────────────────────────── */

/* [C] Create - 새 항목 추가 */
static void op_create(json_value_t *root)
{
    char key[MAX_INPUT], value[MAX_INPUT];

    printf("\n[추가 - Create]\n");
    print_line();

    printf("키(Key)   : ");
    if (!fgets(key, sizeof(key), stdin)) return;
    key[strcspn(key, "\n")] = '\0';
    if (key[0] == '\0') { printf("[오류] 키는 비울 수 없습니다.\n"); return; }

    json_value_t *store = json_object_get(root, "store");

    if (find_by_key(store, key) >= 0) {
        printf("[오류] 키 '%s'가 이미 존재합니다. 수정(4번)을 이용하세요.\n", key);
        return;
    }

    printf("값(Value) : ");
    if (!fgets(value, sizeof(value), stdin)) return;
    value[strcspn(value, "\n")] = '\0';

    json_value_t *next_v = json_object_get(root, "next_id");
    int new_id = (int)json_number_val(next_v);

    json_value_t *item = json_make_object();
    json_object_set(item, "id",    json_make_number(new_id));
    json_object_set(item, "key",   json_make_string(key));
    json_object_set(item, "value", json_make_string(value));

    json_array_push(store, item);
    json_object_set(root, "next_id", json_make_number(new_id + 1));

    if (save_store(root) == 0)
        printf("[완료] ID %d | key='%s' | value='%s' 저장됨\n",
               new_id, key, value);
}

/* [R] Read - 전체 목록 조회 */
static void op_list(json_value_t *root)
{
    json_value_t *store = json_object_get(root, "store");
    size_t n = json_array_length(store);

    printf("\n[목록 - Read All]\n");
    print_line();

    if (n == 0) {
        printf("저장된 항목이 없습니다.\n");
        print_line();
        return;
    }

    printf("%-6s  %-30s  %s\n", "ID", "Key", "Value");
    print_line();
    for (size_t i = 0; i < n; i++) {
        json_value_t *item = json_array_get(store, i);
        int    id  = (int)json_number_val(json_object_get(item, "id"));
        const char *k = json_string_val(json_object_get(item, "key"));
        const char *v = json_string_val(json_object_get(item, "value"));
        printf("%-6d  %-30s  %s\n", id, k, v);
    }
    print_line();
    printf("총 %zu개\n", n);
}

/* [R] Read - 단일 조회 (ID 또는 Key) */
static void op_read(json_value_t *root)
{
    char input[MAX_INPUT];

    printf("\n[단일 조회 - Read]\n");
    print_line();
    printf("ID 또는 Key 입력: ");
    if (!fgets(input, sizeof(input), stdin)) return;
    input[strcspn(input, "\n")] = '\0';

    json_value_t *store = json_object_get(root, "store");
    int idx = -1;

    /* 숫자면 ID로 검색, 아니면 Key로 검색 */
    char *end;
    long id_try = strtol(input, &end, 10);
    if (*end == '\0' && end != input)
        idx = find_by_id(store, (int)id_try);
    if (idx < 0)
        idx = find_by_key(store, input);

    if (idx < 0) {
        printf("[결과] '%s'에 해당하는 항목을 찾을 수 없습니다.\n", input);
        return;
    }

    json_value_t *item = json_array_get(store, (size_t)idx);
    printf("ID    : %d\n",  (int)json_number_val(json_object_get(item, "id")));
    printf("Key   : %s\n",  json_string_val(json_object_get(item, "key")));
    printf("Value : %s\n",  json_string_val(json_object_get(item, "value")));
}

/* [U] Update - 값 수정 */
static void op_update(json_value_t *root)
{
    char input[MAX_INPUT], new_value[MAX_INPUT];

    printf("\n[수정 - Update]\n");
    print_line();
    printf("ID 또는 Key 입력: ");
    if (!fgets(input, sizeof(input), stdin)) return;
    input[strcspn(input, "\n")] = '\0';

    json_value_t *store = json_object_get(root, "store");
    int idx = -1;

    char *end;
    long id_try = strtol(input, &end, 10);
    if (*end == '\0' && end != input)
        idx = find_by_id(store, (int)id_try);
    if (idx < 0)
        idx = find_by_key(store, input);

    if (idx < 0) {
        printf("[오류] '%s'에 해당하는 항목을 찾을 수 없습니다.\n", input);
        return;
    }

    json_value_t *item = json_array_get(store, (size_t)idx);
    const char *cur_key = json_string_val(json_object_get(item, "key"));
    const char *cur_val = json_string_val(json_object_get(item, "value"));

    printf("현재 Key   : %s\n", cur_key);
    printf("현재 Value : %s\n", cur_val);
    printf("새 Value   : ");
    if (!fgets(new_value, sizeof(new_value), stdin)) return;
    new_value[strcspn(new_value, "\n")] = '\0';

    if (new_value[0] == '\0') { printf("변경 없이 종료합니다.\n"); return; }

    json_object_set(item, "value", json_make_string(new_value));

    if (save_store(root) == 0)
        printf("[완료] key='%s' 값이 '%s'로 변경되었습니다.\n", cur_key, new_value);
}

/* [U] Update Key - 키 이름 변경 */
static void op_rename(json_value_t *root)
{
    char input[MAX_INPUT], new_key[MAX_INPUT];

    printf("\n[키 변경 - Rename Key]\n");
    print_line();
    printf("ID 또는 현재 Key 입력: ");
    if (!fgets(input, sizeof(input), stdin)) return;
    input[strcspn(input, "\n")] = '\0';

    json_value_t *store = json_object_get(root, "store");
    int idx = -1;

    char *end;
    long id_try = strtol(input, &end, 10);
    if (*end == '\0' && end != input)
        idx = find_by_id(store, (int)id_try);
    if (idx < 0)
        idx = find_by_key(store, input);

    if (idx < 0) {
        printf("[오류] '%s'에 해당하는 항목을 찾을 수 없습니다.\n", input);
        return;
    }

    json_value_t *item = json_array_get(store, (size_t)idx);
    const char *cur_key = json_string_val(json_object_get(item, "key"));

    printf("현재 Key: %s\n", cur_key);
    printf("새 Key  : ");
    if (!fgets(new_key, sizeof(new_key), stdin)) return;
    new_key[strcspn(new_key, "\n")] = '\0';

    if (new_key[0] == '\0') { printf("변경 없이 종료합니다.\n"); return; }

    if (find_by_key(store, new_key) >= 0) {
        printf("[오류] 키 '%s'가 이미 존재합니다.\n", new_key);
        return;
    }

    json_object_set(item, "key", json_make_string(new_key));

    if (save_store(root) == 0)
        printf("[완료] 키가 '%s' → '%s'로 변경되었습니다.\n", cur_key, new_key);
}

/* [D] Delete - 항목 삭제 */
static void op_delete(json_value_t *root)
{
    char input[MAX_INPUT];

    printf("\n[삭제 - Delete]\n");
    print_line();
    printf("ID 또는 Key 입력: ");
    if (!fgets(input, sizeof(input), stdin)) return;
    input[strcspn(input, "\n")] = '\0';

    json_value_t *store = json_object_get(root, "store");
    int idx = -1;

    char *end;
    long id_try = strtol(input, &end, 10);
    if (*end == '\0' && end != input)
        idx = find_by_id(store, (int)id_try);
    if (idx < 0)
        idx = find_by_key(store, input);

    if (idx < 0) {
        printf("[오류] '%s'에 해당하는 항목을 찾을 수 없습니다.\n", input);
        return;
    }

    json_value_t *item = json_array_get(store, (size_t)idx);
    const char *k = json_string_val(json_object_get(item, "key"));
    const char *v = json_string_val(json_object_get(item, "value"));

    printf("삭제 대상: key='%s', value='%s'\n", k, v);
    printf("정말 삭제하시겠습니까? (y/n): ");

    int c = getchar();
    clear_input();

    if (c != 'y' && c != 'Y') {
        printf("삭제가 취소되었습니다.\n");
        return;
    }

    array_remove_at(store, (size_t)idx);

    if (save_store(root) == 0)
        printf("[완료] key='%s' 항목이 삭제되었습니다.\n", k);
}

/* ───────────────────────────────────────────────────────────
 *  메뉴
 * ─────────────────────────────────────────────────────────── */

static void print_menu(void)
{
    printf("\n════════ Key-Value Store (JSON CRUD) ════════\n");
    printf(" 1. 목록 보기      (Read All)\n");
    printf(" 2. 항목 추가      (Create)\n");
    printf(" 3. 단일 조회      (Read One)\n");
    printf(" 4. 값 수정        (Update Value)\n");
    printf(" 5. 키 이름 변경   (Rename Key)\n");
    printf(" 6. 항목 삭제      (Delete)\n");
    printf(" 0. 종료           (Exit)\n");
    printf("════════════════════════════════════════════\n");
    printf("선택: ");
}

/* ───────────────────────────────────────────────────────────
 *  진입점
 * ─────────────────────────────────────────────────────────── */

int main(void)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    printf("Key-Value Store CRUD 애플리케이션\n");
    printf("데이터 파일: %s\n", DATA_FILE);

    json_value_t *root = load_store();

    int choice;
    for (;;) {
        print_menu();
        if (scanf("%d", &choice) != 1) {
            clear_input();
            printf("[오류] 숫자를 입력하세요.\n");
            continue;
        }
        clear_input();

        switch (choice) {
            case 1: op_list(root);   break;
            case 2: op_create(root); break;
            case 3: op_read(root);   break;
            case 4: op_update(root); break;
            case 5: op_rename(root); break;
            case 6: op_delete(root); break;
            case 0:
                printf("프로그램을 종료합니다.\n");
                json_free(root);
                return 0;
            default:
                printf("[오류] 0~6 사이의 숫자를 입력하세요.\n");
        }
    }
}
