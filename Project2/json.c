#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>

/* MSVC 호환: POSIX strdup → _strdup */
#ifdef _MSC_VER
#  define strdup _strdup
#endif

/* ═══════════════════════════════════════════════════════════
 *  내부 파서 상태
 * ═══════════════════════════════════════════════════════════ */

typedef struct {
    const char *src;
    size_t      pos;
    size_t      len;
    char        error[256];
} parser_t;

/* ─── 내부 유틸리티 ─── */

static json_value_t *alloc_value(json_type_t type)
{
    json_value_t *v = (json_value_t *)calloc(1, sizeof(json_value_t));
    if (v) v->type = type;
    return v;
}

static char *strdup_n(const char *s, size_t n)
{
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

/* ─── 파서 헬퍼 ─── */

static void skip_whitespace(parser_t *p)
{
    while (p->pos < p->len && isspace((unsigned char)p->src[p->pos]))
        p->pos++;
}

static int peek(parser_t *p)
{
    skip_whitespace(p);
    if (p->pos >= p->len) return -1;
    return (unsigned char)p->src[p->pos];
}

static int consume(parser_t *p)
{
    if (p->pos >= p->len) return -1;
    return (unsigned char)p->src[p->pos++];
}

static int expect(parser_t *p, char c)
{
    skip_whitespace(p);
    if (p->pos >= p->len || p->src[p->pos] != c) {
        snprintf(p->error, sizeof(p->error),
                 "위치 %zu: '%c' 예상, '%c' 발견",
                 p->pos, c,
                 p->pos < p->len ? p->src[p->pos] : '?');
        return 0;
    }
    p->pos++;
    return 1;
}

/* ─── 유니코드 이스케이프 → UTF-8 변환 ─── */

static int decode_utf8(uint32_t cp, char *out)
{
    if (cp <= 0x7F) {
        out[0] = (char)cp;
        return 1;
    } else if (cp <= 0x7FF) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp <= 0xFFFF) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    } else if (cp <= 0x10FFFF) {
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

static uint32_t parse_hex4(parser_t *p)
{
    uint32_t val = 0;
    for (int i = 0; i < 4; i++) {
        if (p->pos >= p->len) return (uint32_t)-1;
        char c = p->src[p->pos++];
        val <<= 4;
        if      (c >= '0' && c <= '9') val |= (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') val |= (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') val |= (uint32_t)(c - 'A' + 10);
        else return (uint32_t)-1;
    }
    return val;
}

/* ─── 각 타입 파서 (전방 선언) ─── */

static json_value_t *parse_value(parser_t *p);

static json_value_t *parse_string_value(parser_t *p)
{
    /* 닫힌 따옴표까지 내용 추적 */
    size_t start = p->pos; /* '"' 다음 위치 */
    size_t cap = 64;
    char  *buf = (char *)malloc(cap);
    size_t blen = 0;
    if (!buf) return NULL;

    while (p->pos < p->len) {
        unsigned char c = (unsigned char)p->src[p->pos++];
        if (c == '"') {
            /* 문자열 종료 */
            buf[blen] = '\0';
            json_value_t *v = alloc_value(JSON_STRING);
            if (!v) { free(buf); return NULL; }
            v->u.string = buf;
            (void)start;
            return v;
        }
        if (c == '\\') {
            if (p->pos >= p->len) break;
            char esc = p->src[p->pos++];
            char decoded;
            char utf8[4];
            int  utf8_len = 0;
            switch (esc) {
                case '"':  decoded = '"';  goto single;
                case '\\': decoded = '\\'; goto single;
                case '/':  decoded = '/';  goto single;
                case 'b':  decoded = '\b'; goto single;
                case 'f':  decoded = '\f'; goto single;
                case 'n':  decoded = '\n'; goto single;
                case 'r':  decoded = '\r'; goto single;
                case 't':  decoded = '\t'; goto single;
                case 'u': {
                    uint32_t cp = parse_hex4(p);
                    if (cp == (uint32_t)-1) goto error;
                    /* 서로게이트 쌍 처리 */
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (p->pos + 1 < p->len &&
                            p->src[p->pos] == '\\' &&
                            p->src[p->pos + 1] == 'u') {
                            p->pos += 2;
                            uint32_t low = parse_hex4(p);
                            if (low == (uint32_t)-1) goto error;
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        }
                    }
                    utf8_len = decode_utf8(cp, utf8);
                    if (utf8_len == 0) goto error;
                    /* buf에 utf8 추가 */
                    while (blen + utf8_len + 1 > cap) {
                        cap *= 2;
                        char *tmp = (char *)realloc(buf, cap);
                        if (!tmp) goto error;
                        buf = tmp;
                    }
                    memcpy(buf + blen, utf8, (size_t)utf8_len);
                    blen += (size_t)utf8_len;
                    continue;
                }
                default: goto error;
            }
        single:
            while (blen + 2 > cap) {
                cap *= 2;
                char *tmp = (char *)realloc(buf, cap);
                if (!tmp) goto error;
                buf = tmp;
            }
            buf[blen++] = decoded;
            continue;
        }
        /* 일반 문자 */
        while (blen + 2 > cap) {
            cap *= 2;
            char *tmp = (char *)realloc(buf, cap);
            if (!tmp) goto error;
            buf = tmp;
        }
        buf[blen++] = (char)c;
    }
error:
    free(buf);
    snprintf(p->error, sizeof(p->error), "문자열 파싱 실패 (위치 %zu)", p->pos);
    return NULL;
}

static json_value_t *parse_number(parser_t *p)
{
    size_t start = p->pos;
    if (p->pos < p->len && p->src[p->pos] == '-') p->pos++;
    if (p->pos < p->len && p->src[p->pos] == '0') {
        p->pos++;
    } else {
        while (p->pos < p->len && isdigit((unsigned char)p->src[p->pos])) p->pos++;
    }
    if (p->pos < p->len && p->src[p->pos] == '.') {
        p->pos++;
        while (p->pos < p->len && isdigit((unsigned char)p->src[p->pos])) p->pos++;
    }
    if (p->pos < p->len && (p->src[p->pos] == 'e' || p->src[p->pos] == 'E')) {
        p->pos++;
        if (p->pos < p->len && (p->src[p->pos] == '+' || p->src[p->pos] == '-')) p->pos++;
        while (p->pos < p->len && isdigit((unsigned char)p->src[p->pos])) p->pos++;
    }

    char tmp[64];
    size_t slen = p->pos - start;
    if (slen >= sizeof(tmp)) {
        snprintf(p->error, sizeof(p->error), "숫자가 너무 깁니다 (위치 %zu)", start);
        return NULL;
    }
    memcpy(tmp, p->src + start, slen);
    tmp[slen] = '\0';

    char *end;
    errno = 0;
    double num = strtod(tmp, &end);
    if (errno != 0 || end == tmp) {
        snprintf(p->error, sizeof(p->error), "유효하지 않은 숫자: %s", tmp);
        return NULL;
    }

    json_value_t *v = alloc_value(JSON_NUMBER);
    if (!v) return NULL;
    v->u.number = num;
    return v;
}

static json_value_t *parse_array(parser_t *p)
{
    json_value_t *arr = alloc_value(JSON_ARRAY);
    if (!arr) return NULL;

    arr->u.array.capacity = 8;
    arr->u.array.items = (json_value_t **)malloc(8 * sizeof(json_value_t *));
    if (!arr->u.array.items) { free(arr); return NULL; }

    skip_whitespace(p);
    if (p->pos < p->len && p->src[p->pos] == ']') {
        p->pos++;
        return arr;
    }

    for (;;) {
        json_value_t *item = parse_value(p);
        if (!item) { json_free(arr); return NULL; }

        if (arr->u.array.count == arr->u.array.capacity) {
            size_t newcap = arr->u.array.capacity * 2;
            json_value_t **tmp = (json_value_t **)realloc(
                arr->u.array.items, newcap * sizeof(json_value_t *));
            if (!tmp) { json_free(item); json_free(arr); return NULL; }
            arr->u.array.items    = tmp;
            arr->u.array.capacity = newcap;
        }
        arr->u.array.items[arr->u.array.count++] = item;

        int ch = peek(p);
        if (ch == ']') { p->pos++; break; }
        if (ch != ',') {
            snprintf(p->error, sizeof(p->error),
                     "배열: ',' 또는 ']' 예상 (위치 %zu)", p->pos);
            json_free(arr);
            return NULL;
        }
        p->pos++;
    }
    return arr;
}

static json_value_t *parse_object(parser_t *p)
{
    json_value_t *obj = alloc_value(JSON_OBJECT);
    if (!obj) return NULL;

    skip_whitespace(p);
    if (p->pos < p->len && p->src[p->pos] == '}') {
        p->pos++;
        return obj;
    }

    for (;;) {
        /* 키 */
        skip_whitespace(p);
        if (p->pos >= p->len || p->src[p->pos] != '"') {
            snprintf(p->error, sizeof(p->error),
                     "객체 키: '\"' 예상 (위치 %zu)", p->pos);
            json_free(obj);
            return NULL;
        }
        p->pos++;
        json_value_t *key_val = parse_string_value(p);
        if (!key_val) { json_free(obj); return NULL; }
        char *key = key_val->u.string;
        key_val->u.string = NULL;
        free(key_val);

        if (!expect(p, ':')) { free(key); json_free(obj); return NULL; }

        json_value_t *val = parse_value(p);
        if (!val) { free(key); json_free(obj); return NULL; }

        /* 기존 키 덮어쓰기 */
        json_member_t *m = obj->u.object.head;
        int found = 0;
        while (m) {
            if (strcmp(m->key, key) == 0) {
                json_free(m->value);
                m->value = val;
                free(key);
                found = 1;
                break;
            }
            m = m->next;
        }
        if (!found) {
            json_member_t *nm = (json_member_t *)malloc(sizeof(json_member_t));
            if (!nm) { free(key); json_free(val); json_free(obj); return NULL; }
            nm->key   = key;
            nm->value = val;
            nm->next  = NULL;
            /* 링크드 리스트 끝에 추가 */
            if (!obj->u.object.head) {
                obj->u.object.head = nm;
            } else {
                json_member_t *tail = obj->u.object.head;
                while (tail->next) tail = tail->next;
                tail->next = nm;
            }
            obj->u.object.count++;
        }

        int ch = peek(p);
        if (ch == '}') { p->pos++; break; }
        if (ch != ',') {
            snprintf(p->error, sizeof(p->error),
                     "객체: ',' 또는 '}' 예상 (위치 %zu)", p->pos);
            json_free(obj);
            return NULL;
        }
        p->pos++;
    }
    return obj;
}

static json_value_t *parse_value(parser_t *p)
{
    int ch = peek(p);
    if (ch < 0) {
        snprintf(p->error, sizeof(p->error), "예상치 못한 입력 끝");
        return NULL;
    }

    switch (ch) {
        case 'n':
            if (p->pos + 4 <= p->len &&
                memcmp(p->src + p->pos, "null", 4) == 0) {
                p->pos += 4;
                return alloc_value(JSON_NULL);
            }
            break;
        case 't':
            if (p->pos + 4 <= p->len &&
                memcmp(p->src + p->pos, "true", 4) == 0) {
                p->pos += 4;
                json_value_t *v = alloc_value(JSON_BOOL);
                if (v) v->u.boolean = 1;
                return v;
            }
            break;
        case 'f':
            if (p->pos + 5 <= p->len &&
                memcmp(p->src + p->pos, "false", 5) == 0) {
                p->pos += 5;
                json_value_t *v = alloc_value(JSON_BOOL);
                if (v) v->u.boolean = 0;
                return v;
            }
            break;
        case '"':
            p->pos++;
            return parse_string_value(p);
        case '[':
            p->pos++;
            return parse_array(p);
        case '{':
            p->pos++;
            return parse_object(p);
        default:
            if (ch == '-' || isdigit(ch))
                return parse_number(p);
            break;
    }

    snprintf(p->error, sizeof(p->error),
             "위치 %zu: 유효하지 않은 토큰 '%c'", p->pos, (char)ch);
    return NULL;
}

/* ═══════════════════════════════════════════════════════════
 *  공개 파싱 API
 * ═══════════════════════════════════════════════════════════ */

json_value_t *json_parse(const char *src)
{
    if (!src) return NULL;
    parser_t p;
    p.src   = src;
    p.pos   = 0;
    p.len   = strlen(src);
    p.error[0] = '\0';

    json_value_t *val = parse_value(&p);
    if (!val) {
        fprintf(stderr, "[json] 파싱 오류: %s\n", p.error);
        return NULL;
    }
    /* 후행 공백 이후 잔여 문자 경고 */
    skip_whitespace(&p);
    if (p.pos < p.len)
        fprintf(stderr, "[json] 경고: 위치 %zu 이후 잔여 문자가 있습니다.\n", p.pos);
    return val;
}

json_value_t *json_parse_file(const char *path)
{
    if (!path) return NULL;
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "[json] 파일 열기 실패: %s\n", path);
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    rewind(fp);
    if (fsize <= 0) { fclose(fp); return NULL; }

    char *buf = (char *)malloc((size_t)fsize + 1);
    if (!buf) { fclose(fp); return NULL; }
    size_t read = fread(buf, 1, (size_t)fsize, fp);
    fclose(fp);
    buf[read] = '\0';

    json_value_t *val = json_parse(buf);
    free(buf);
    return val;
}

/* ═══════════════════════════════════════════════════════════
 *  직렬화
 * ═══════════════════════════════════════════════════════════ */

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} strbuf_t;

static int sb_init(strbuf_t *sb)
{
    sb->cap = 256;
    sb->len = 0;
    sb->buf = (char *)malloc(sb->cap);
    return sb->buf ? 0 : -1;
}

static int sb_grow(strbuf_t *sb, size_t need)
{
    while (sb->len + need + 1 > sb->cap) {
        sb->cap *= 2;
        char *tmp = (char *)realloc(sb->buf, sb->cap);
        if (!tmp) return -1;
        sb->buf = tmp;
    }
    return 0;
}

static int sb_push(strbuf_t *sb, char c)
{
    if (sb_grow(sb, 1) < 0) return -1;
    sb->buf[sb->len++] = c;
    return 0;
}

static int sb_append(strbuf_t *sb, const char *s, size_t n)
{
    if (sb_grow(sb, n) < 0) return -1;
    memcpy(sb->buf + sb->len, s, n);
    sb->len += n;
    return 0;
}

static int sb_printf(strbuf_t *sb, const char *fmt, ...)
{
    char tmp[128];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0) return -1;
    return sb_append(sb, tmp, (size_t)n);
}

static void write_indent(strbuf_t *sb, int indent, int depth)
{
    if (indent < 0) return;
    sb_push(sb, '\n');
    for (int i = 0; i < depth * indent; i++) sb_push(sb, ' ');
}

static int write_string_escaped(strbuf_t *sb, const char *s)
{
    sb_push(sb, '"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
            case '"':  sb_append(sb, "\\\"", 2); break;
            case '\\': sb_append(sb, "\\\\", 2); break;
            case '\b': sb_append(sb, "\\b",  2); break;
            case '\f': sb_append(sb, "\\f",  2); break;
            case '\n': sb_append(sb, "\\n",  2); break;
            case '\r': sb_append(sb, "\\r",  2); break;
            case '\t': sb_append(sb, "\\t",  2); break;
            default:
                if (c < 0x20) {
                    char esc[8];
                    snprintf(esc, sizeof(esc), "\\u%04x", c);
                    sb_append(sb, esc, 6);
                } else {
                    sb_push(sb, (char)c);
                }
                break;
        }
    }
    sb_push(sb, '"');
    return 0;
}

static int write_value(strbuf_t *sb, const json_value_t *val, int indent, int depth)
{
    if (!val) return sb_append(sb, "null", 4);

    switch (val->type) {
        case JSON_NULL:
            return sb_append(sb, "null", 4);

        case JSON_BOOL:
            return val->u.boolean
                ? sb_append(sb, "true",  4)
                : sb_append(sb, "false", 5);

        case JSON_NUMBER: {
            double n = val->u.number;
            char tmp[64];
            if (n == (long long)n && !isinf(n) && !isnan(n))
                snprintf(tmp, sizeof(tmp), "%.0f", n);
            else
                snprintf(tmp, sizeof(tmp), "%.17g", n);
            return sb_append(sb, tmp, strlen(tmp));
        }

        case JSON_STRING:
            return write_string_escaped(sb, val->u.string ? val->u.string : "");

        case JSON_ARRAY: {
            sb_push(sb, '[');
            size_t cnt = val->u.array.count;
            for (size_t i = 0; i < cnt; i++) {
                if (indent >= 0) write_indent(sb, indent, depth + 1);
                write_value(sb, val->u.array.items[i], indent, depth + 1);
                if (i + 1 < cnt) sb_push(sb, ',');
            }
            if (cnt > 0 && indent >= 0) write_indent(sb, indent, depth);
            sb_push(sb, ']');
            return 0;
        }

        case JSON_OBJECT: {
            sb_push(sb, '{');
            json_member_t *m = val->u.object.head;
            int first = 1;
            while (m) {
                if (!first) sb_push(sb, ',');
                first = 0;
                if (indent >= 0) write_indent(sb, indent, depth + 1);
                write_string_escaped(sb, m->key);
                sb_push(sb, ':');
                if (indent >= 0) sb_push(sb, ' ');
                write_value(sb, m->value, indent, depth + 1);
                m = m->next;
            }
            if (!first && indent >= 0) write_indent(sb, indent, depth);
            sb_push(sb, '}');
            return 0;
        }
    }
    return 0;
}

char *json_stringify(const json_value_t *val, int indent)
{
    strbuf_t sb;
    if (sb_init(&sb) < 0) return NULL;
    write_value(&sb, val, indent, 0);
    sb.buf[sb.len] = '\0';
    return sb.buf;
}

int json_save_file(const json_value_t *val, const char *path, int indent)
{
    char *s = json_stringify(val, indent);
    if (!s) return -1;

    FILE *fp = fopen(path, "wb");
    if (!fp) { free(s); return -1; }

    size_t written = fwrite(s, 1, strlen(s), fp);
    int ok = (written == strlen(s)) ? 0 : -1;
    fclose(fp);
    free(s);
    return ok;
}

/* ═══════════════════════════════════════════════════════════
 *  생성 API
 * ═══════════════════════════════════════════════════════════ */

json_value_t *json_make_null(void)   { return alloc_value(JSON_NULL); }

json_value_t *json_make_bool(int b)
{
    json_value_t *v = alloc_value(JSON_BOOL);
    if (v) v->u.boolean = b ? 1 : 0;
    return v;
}

json_value_t *json_make_number(double n)
{
    json_value_t *v = alloc_value(JSON_NUMBER);
    if (v) v->u.number = n;
    return v;
}

json_value_t *json_make_string(const char *s)
{
    json_value_t *v = alloc_value(JSON_STRING);
    if (!v) return NULL;
    v->u.string = s ? strdup(s) : strdup_n("", 0);
    if (!v->u.string) { free(v); return NULL; }
    return v;
}

json_value_t *json_make_array(void)
{
    json_value_t *v = alloc_value(JSON_ARRAY);
    if (!v) return NULL;
    v->u.array.capacity = 8;
    v->u.array.items = (json_value_t **)malloc(8 * sizeof(json_value_t *));
    if (!v->u.array.items) { free(v); return NULL; }
    return v;
}

json_value_t *json_make_object(void) { return alloc_value(JSON_OBJECT); }

/* ═══════════════════════════════════════════════════════════
 *  배열 API
 * ═══════════════════════════════════════════════════════════ */

int json_array_push(json_value_t *arr, json_value_t *item)
{
    if (!arr || arr->type != JSON_ARRAY || !item) return -1;
    if (arr->u.array.count == arr->u.array.capacity) {
        size_t newcap = arr->u.array.capacity * 2;
        json_value_t **tmp = (json_value_t **)realloc(
            arr->u.array.items, newcap * sizeof(json_value_t *));
        if (!tmp) return -1;
        arr->u.array.items    = tmp;
        arr->u.array.capacity = newcap;
    }
    arr->u.array.items[arr->u.array.count++] = item;
    return 0;
}

json_value_t *json_array_get(const json_value_t *arr, size_t i)
{
    if (!arr || arr->type != JSON_ARRAY) return NULL;
    if (i >= arr->u.array.count) return NULL;
    return arr->u.array.items[i];
}

size_t json_array_length(const json_value_t *arr)
{
    if (!arr || arr->type != JSON_ARRAY) return 0;
    return arr->u.array.count;
}

/* ═══════════════════════════════════════════════════════════
 *  객체 API
 * ═══════════════════════════════════════════════════════════ */

int json_object_set(json_value_t *obj, const char *key, json_value_t *val)
{
    if (!obj || obj->type != JSON_OBJECT || !key || !val) return -1;

    /* 기존 키 업데이트 */
    json_member_t *m = obj->u.object.head;
    while (m) {
        if (strcmp(m->key, key) == 0) {
            json_free(m->value);
            m->value = val;
            return 0;
        }
        m = m->next;
    }

    /* 새 멤버 추가 */
    json_member_t *nm = (json_member_t *)malloc(sizeof(json_member_t));
    if (!nm) return -1;
    nm->key = strdup(key);
    if (!nm->key) { free(nm); return -1; }
    nm->value = val;
    nm->next  = NULL;

    if (!obj->u.object.head) {
        obj->u.object.head = nm;
    } else {
        json_member_t *tail = obj->u.object.head;
        while (tail->next) tail = tail->next;
        tail->next = nm;
    }
    obj->u.object.count++;
    return 0;
}

json_value_t *json_object_get(const json_value_t *obj, const char *key)
{
    if (!obj || obj->type != JSON_OBJECT || !key) return NULL;
    json_member_t *m = obj->u.object.head;
    while (m) {
        if (strcmp(m->key, key) == 0) return m->value;
        m = m->next;
    }
    return NULL;
}

int json_object_delete(json_value_t *obj, const char *key)
{
    if (!obj || obj->type != JSON_OBJECT || !key) return -1;
    json_member_t **pp = &obj->u.object.head;
    while (*pp) {
        if (strcmp((*pp)->key, key) == 0) {
            json_member_t *del = *pp;
            *pp = del->next;
            json_free(del->value);
            free(del->key);
            free(del);
            obj->u.object.count--;
            return 0;
        }
        pp = &(*pp)->next;
    }
    return -1;
}

size_t json_object_count(const json_value_t *obj)
{
    if (!obj || obj->type != JSON_OBJECT) return 0;
    return obj->u.object.count;
}

/* ═══════════════════════════════════════════════════════════
 *  메모리 해제 & 유틸리티
 * ═══════════════════════════════════════════════════════════ */

void json_free(json_value_t *val)
{
    if (!val) return;
    switch (val->type) {
        case JSON_STRING:
            free(val->u.string);
            break;
        case JSON_ARRAY:
            for (size_t i = 0; i < val->u.array.count; i++)
                json_free(val->u.array.items[i]);
            free(val->u.array.items);
            break;
        case JSON_OBJECT: {
            json_member_t *m = val->u.object.head;
            while (m) {
                json_member_t *next = m->next;
                json_free(m->value);
                free(m->key);
                free(m);
                m = next;
            }
            break;
        }
        default: break;
    }
    free(val);
}

const char *json_type_name(json_type_t type)
{
    switch (type) {
        case JSON_NULL:   return "null";
        case JSON_BOOL:   return "bool";
        case JSON_NUMBER: return "number";
        case JSON_STRING: return "string";
        case JSON_ARRAY:  return "array";
        case JSON_OBJECT: return "object";
    }
    return "unknown";
}

json_value_t *json_clone(const json_value_t *val)
{
    if (!val) return NULL;
    switch (val->type) {
        case JSON_NULL:   return json_make_null();
        case JSON_BOOL:   return json_make_bool(val->u.boolean);
        case JSON_NUMBER: return json_make_number(val->u.number);
        case JSON_STRING: return json_make_string(val->u.string);
        case JSON_ARRAY: {
            json_value_t *arr = json_make_array();
            if (!arr) return NULL;
            for (size_t i = 0; i < val->u.array.count; i++) {
                json_value_t *c = json_clone(val->u.array.items[i]);
                if (!c || json_array_push(arr, c) < 0) {
                    json_free(c); json_free(arr); return NULL;
                }
            }
            return arr;
        }
        case JSON_OBJECT: {
            json_value_t *obj = json_make_object();
            if (!obj) return NULL;
            json_member_t *m = val->u.object.head;
            while (m) {
                json_value_t *c = json_clone(m->value);
                if (!c || json_object_set(obj, m->key, c) < 0) {
                    json_free(c); json_free(obj); return NULL;
                }
                m = m->next;
            }
            return obj;
        }
    }
    return NULL;
}
