/*
 * basic_string.c -- BString dynamic string type implementation.
 * C89 compatible.  Compile with: gcc -std=c89 -Wall
 *
 * Memory model:
 *   data  -- heap-allocated, always NUL-terminated
 *   len   -- current length in bytes (not counting NUL)
 *   cap   -- total allocated bytes (always >= len+1)
 *
 * All BASIC string indices are 1-based; this file translates to 0-based C.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "basic_runtime.h"

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

/*
 * bs__ensure -- ensure b->cap >= need.
 * Grows the buffer (doubling strategy) if required.
 * On allocation failure, terminates the process (no graceful recovery
 * is possible without a full error-frame mechanism here).
 */
static void bs__ensure(BString *b, int need)
{
    int    new_cap;
    char  *new_data;

    if (!b) return;
    if (b->data == NULL) {
        b->len = 0;
        b->cap = 0;
    }

    if (b->cap >= need && b->data != NULL)
        return;

    new_cap = (b->cap > 0) ? b->cap * 2 : 16;
    if (new_cap < need)
        new_cap = need;

    new_data = (char *)realloc(b->data, (size_t)new_cap);
    if (!new_data) {
        fprintf(stderr, "bs: out of memory\n");
        exit(1);
    }
    b->data = new_data;
    b->cap  = new_cap;
}

/*
 * bs__set_from_buf -- assign len bytes from buf into b, then NUL-terminate.
 */
static void bs__set_from_buf(BString *b, const char *buf, int len)
{
    if (!b) return;
    if (b->data == NULL) {
        b->len = 0;
        b->cap = 0;
    }
    if (b->data != NULL && buf >= b->data && buf < b->data + b->cap) {
        /* Source buffer is inside b's own buffer -- copy safely */
        char *tmp = (char *)malloc((size_t)(len + 1));
        if (!tmp) { fprintf(stderr, "bs: out of memory\n"); exit(1); }
        if (len > 0) memcpy(tmp, buf, (size_t)len);
        tmp[len] = '\0';
        free(b->data);
        b->data = tmp;
        b->len  = len;
        b->cap  = len + 1;
        return;
    }
    bs__ensure(b, len + 1);
    if (len > 0 && buf)
        memmove(b->data, buf, (size_t)len);
    b->data[len] = '\0';
    b->len = len;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

void bs_string_init(BString *b)
{
    b->data    = (char *)malloc(1);
    if (!b->data) { fprintf(stderr, "bs: out of memory\n"); exit(1); }
    b->data[0] = '\0';
    b->len     = 0;
    b->cap     = 1;
}

void bs_string_free(BString *b)
{
    if (!b) return;
    free(b->data);
    b->data = NULL;
    b->len  = 0;
    b->cap  = 0;
}

BString bs_string_from_cstr(const char *s)
{
    BString b;
    int     n;

    n       = s ? (int)strlen(s) : 0;
    b.cap   = n + 1;
    b.data  = (char *)malloc((size_t)b.cap);
    if (!b.data) { fprintf(stderr, "bs: out of memory\n"); exit(1); }
    if (s) memcpy(b.data, s, (size_t)(n + 1));
    else   b.data[0] = '\0';
    b.len   = n;
    return b;
}

void bs_string_assign(BString *dst, const BString *src)
{
    if (!dst) return;
    if (dst == src) return;
    if (!src || !src->data || src->len <= 0) {
        bs__set_from_buf(dst, "", 0);
    } else {
        bs__set_from_buf(dst, src->data, src->len);
    }
}

void bs_string_assign_cstr(BString *dst, const char *s)
{
    int n;
    if (!s) s = "";
    n = (int)strlen(s);
    bs__set_from_buf(dst, s, n);
}

void bs_string_assign_buf(BString *dst, const char *buf, int len)
{
    if (!dst) return;
    if (!buf || len <= 0) {
        bs__set_from_buf(dst, "", 0);
    } else {
        bs__set_from_buf(dst, buf, len);
    }
}

void bs_string_grow(BString *b, int new_cap)
{
    bs__ensure(b, new_cap);
}

/* ------------------------------------------------------------------ */
/* Legacy heap-allocated API (kept for compatibility)                  */
/* ------------------------------------------------------------------ */

BString *bs_new(const char *s)
{
    BString *b;
    int      n;

    b = (BString *)malloc(sizeof(BString));
    if (!b) return NULL;
    n       = s ? (int)strlen(s) : 0;
    b->cap  = n + 1;
    b->len  = n;
    b->data = (char *)malloc((size_t)b->cap);
    if (!b->data) { free(b); return NULL; }
    if (s) memcpy(b->data, s, (size_t)(n + 1));
    else   b->data[0] = '\0';
    return b;
}

void bs_free(BString *b)
{
    bs_string_free(b);
}

void bs_assign(BString *dst, const BString *src)
{
    bs_string_assign(dst, src);
}

void bs_assign_cstr(BString *dst, const char *s)
{
    bs_string_assign_cstr(dst, s);
}

/* ------------------------------------------------------------------ */
/* Core ops                                                            */
/* ------------------------------------------------------------------ */

void bs_concat(BString *dst, const BString *a, const BString *b)
{
    int alen;
    int blen;
    int total;

    if (!dst) return;
    alen = (a && a->data) ? a->len : 0;
    blen = (b && b->data) ? b->len : 0;
    total = alen + blen;

    if (dst == a || dst == b) {
        char *buf = (char *)malloc((size_t)(total + 1));
        if (!buf) {
            fprintf(stderr, "bs: out of memory\n");
            exit(1);
        }
        if (alen > 0 && a && a->data) memcpy(buf, a->data, (size_t)alen);
        if (blen > 0 && b && b->data) memcpy(buf + alen, b->data, (size_t)blen);
        buf[total] = '\0';
        free(dst->data);
        dst->data = buf;
        dst->len  = total;
        dst->cap  = total + 1;
        return;
    }

    bs__ensure(dst, total + 1);
    if (alen > 0 && a && a->data) memcpy(dst->data, a->data, (size_t)alen);
    if (blen > 0 && b && b->data) memcpy(dst->data + alen, b->data, (size_t)blen);
    dst->data[total] = '\0';
    dst->len = total;
}

int bs_compare(const BString *a, const BString *b)
{
    const char *da;
    const char *db;
    int alen;
    int blen;
    int minlen;
    int cmp;

    da   = (a && a->data) ? a->data : "";
    db   = (b && b->data) ? b->data : "";
    alen = (a && a->data) ? a->len  : 0;
    blen = (b && b->data) ? b->len  : 0;
    minlen = (alen < blen) ? alen : blen;

    if (minlen > 0) {
        cmp = memcmp(da, db, (size_t)minlen);
        if (cmp != 0) return cmp;
    }
    if (alen < blen) return -1;
    if (alen > blen) return  1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Built-in: length                                                    */
/* ------------------------------------------------------------------ */

int bs_len(const BString *s)
{
    return (s) ? s->len : 0;
}

/* ------------------------------------------------------------------ */
/* Built-in: MID$                                                      */
/* ------------------------------------------------------------------ */

/*
 * bs_mid -- MID$(s, start, length)
 *   start  : 1-based; clamped to [1, len+1]
 *   length : number of chars to copy; -1 means "to end of string"
 *            negative values other than -1 treated as 0
 */
void bs_mid(BString *dst, const BString *s, int start, int length)
{
    int slen;
    int from;    /* 0-based start index */
    int count;   /* number of chars to copy */

    slen = s ? s->len : 0;

    /* Clamp start to [1 .. slen+1] */
    if (start < 1) start = 1;

    from = start - 1;     /* convert to 0-based */

    if (from >= slen) {
        /* start beyond end -> empty string */
        bs__set_from_buf(dst, "", 0);
        return;
    }

    if (length < 0) {
        /* -1 (or any negative) means "to end" */
        count = slen - from;
    } else {
        count = length;
    }

    /* Clamp count so we don't go past the end */
    if (from + count > slen)
        count = slen - from;

    if (count <= 0) {
        bs__set_from_buf(dst, "", 0);
        return;
    }

    bs__set_from_buf(dst, s->data + from, count);
}

/* ------------------------------------------------------------------ */
/* Built-in: LEFT$                                                     */
/* ------------------------------------------------------------------ */

void bs_left(BString *dst, const BString *s, int n)
{
    int slen;

    if (n < 0) n = 0;
    slen = s ? s->len : 0;
    if (n > slen) n = slen;
    bs__set_from_buf(dst, (s && slen > 0) ? s->data : "", n);
}

/* ------------------------------------------------------------------ */
/* Built-in: RIGHT$                                                    */
/* ------------------------------------------------------------------ */

void bs_right(BString *dst, const BString *s, int n)
{
    int slen;
    int from;

    if (n < 0) n = 0;
    slen = s ? s->len : 0;
    if (n > slen) n = slen;
    from = slen - n;
    bs__set_from_buf(dst, (s && slen > 0) ? s->data + from : "", n);
}

/* ------------------------------------------------------------------ */
/* Built-in: UCASE$ / LCASE$                                          */
/* ------------------------------------------------------------------ */

void bs_ucase(BString *dst, const BString *s)
{
    int i;
    int n;

    n = (s && s->data) ? s->len : 0;
    bs__ensure(dst, n + 1);
    for (i = 0; i < n; i++)
        dst->data[i] = (char)toupper((unsigned char)s->data[i]);
    dst->data[n] = '\0';
    dst->len = n;
}

void bs_lcase(BString *dst, const BString *s)
{
    int i;
    int n;

    n = (s && s->data) ? s->len : 0;
    bs__ensure(dst, n + 1);
    for (i = 0; i < n; i++)
        dst->data[i] = (char)tolower((unsigned char)s->data[i]);
    dst->data[n] = '\0';
    dst->len = n;
}

/* ------------------------------------------------------------------ */
/* Built-in: LTRIM$ / RTRIM$ / TRIM$                                  */
/* ------------------------------------------------------------------ */

static int bs__is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

void bs_ltrim(BString *dst, const BString *s)
{
    int   start;
    int   n;
    const char *p;

    n = (s && s->data) ? s->len : 0;
    p = (s && s->data) ? s->data : "";
    start = 0;
    while (start < n && bs__is_space(p[start]))
        start++;
    bs__set_from_buf(dst, p + start, n - start);
}

void bs_rtrim(BString *dst, const BString *s)
{
    int   end;
    int   n;
    const char *p;

    n = (s && s->data) ? s->len : 0;
    p = (s && s->data) ? s->data : "";
    end = n;
    while (end > 0 && bs__is_space(p[end - 1]))
        end--;
    bs__set_from_buf(dst, p, end);
}

void bs_trim(BString *dst, const BString *s)
{
    int   start;
    int   end;
    int   n;
    const char *p;

    n = (s && s->data) ? s->len : 0;
    p = (s && s->data) ? s->data : "";
    start = 0;
    end   = n;
    while (start < end && bs__is_space(p[start]))
        start++;
    while (end > start && bs__is_space(p[end - 1]))
        end--;
    bs__set_from_buf(dst, p + start, end - start);
}

/* ------------------------------------------------------------------ */
/* Built-in: INSTR                                                     */
/* ------------------------------------------------------------------ */

/*
 * bs_instr -- INSTR([start,] haystack, needle)
 *   start   : 1-based starting position (0 treated as 1)
 *   returns : 1-based position of first occurrence, or 0 if not found
 */
int bs_instr(int start, const BString *hay, const BString *needle)
{
    int   hlen;
    int   nlen;
    int   from;   /* 0-based */
    int   i;
    const char *h;
    const char *n;

    hlen = hay    ? hay->len    : 0;
    nlen = needle ? needle->len : 0;
    h    = hay    ? hay->data   : "";
    n    = needle ? needle->data: "";

    if (start <= 0) start = 1;
    from = start - 1;  /* 0-based */

    /* Empty needle: return start position (QuickBASIC behaviour) */
    if (nlen == 0)
        return (from <= hlen) ? start : 0;

    if (from >= hlen)
        return 0;

    for (i = from; i <= hlen - nlen; i++) {
        if (memcmp(h + i, n, (size_t)nlen) == 0)
            return i + 1;  /* back to 1-based */
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Built-in: STR$                                                      */
/* ------------------------------------------------------------------ */

/*
 * bs_str -- STR$(val)
 *   QuickBASIC convention: positive numbers get a leading space (" 42"),
 *   negative numbers get a minus sign ("-42").
 *   Uses %g formatting (removes trailing zeros).
 */
void bs_str(BString *dst, double val)
{
    char buf[64];
    int  len;

    if (val >= 0.0) {
        buf[0] = ' ';
        sprintf(buf + 1, "%.15g", val);
    } else {
        sprintf(buf, "%.15g", val);
    }
    len = (int)strlen(buf);
    bs__set_from_buf(dst, buf, len);
}

/* ------------------------------------------------------------------ */
/* Built-in: VAL                                                       */
/* ------------------------------------------------------------------ */

double bs_val(const BString *s)
{
    if (!s || !s->data || s->len == 0)
        return 0.0;
    return atof(s->data);
}

/* ------------------------------------------------------------------ */
/* Built-in: CHR$                                                      */
/* ------------------------------------------------------------------ */

void bs_chr(BString *dst, int code)
{
    char buf[2];
    buf[0] = (char)(code & 0xFF);
    buf[1] = '\0';
    bs__set_from_buf(dst, buf, 1);
}

/* ------------------------------------------------------------------ */
/* Built-in: ASC                                                       */
/* ------------------------------------------------------------------ */

int bs_asc(const BString *s)
{
    if (!s || !s->data || s->len == 0)
        return 0;
    return (unsigned char)s->data[0];
}

/* ------------------------------------------------------------------ */
/* Built-in: SPACE$                                                    */
/* ------------------------------------------------------------------ */

void bs_space(BString *dst, int n)
{
    if (n < 0) n = 0;
    bs__ensure(dst, n + 1);
    memset(dst->data, ' ', (size_t)n);
    dst->data[n] = '\0';
    dst->len = n;
}

/* ------------------------------------------------------------------ */
/* Built-in: STRING$(n, ch)                                            */
/* ------------------------------------------------------------------ */

void bs_string_fn(BString *dst, int n, int ch)
{
    if (n < 0) n = 0;
    bs__ensure(dst, n + 1);
    memset(dst->data, ch & 0xFF, (size_t)n);
    dst->data[n] = '\0';
    dst->len = n;
}

/* ------------------------------------------------------------------ */
/* Built-in: HEX$ / OCT$                                              */
/* ------------------------------------------------------------------ */

void bs_hex(BString *dst, long val)
{
    char buf[32];
    int  len;

    sprintf(buf, "%lX", val);
    len = (int)strlen(buf);
    bs__set_from_buf(dst, buf, len);
}

void bs_oct(BString *dst, long val)
{
    char buf[32];
    int  len;

    sprintf(buf, "%lo", val);
    len = (int)strlen(buf);
    bs__set_from_buf(dst, buf, len);
}

/* ------------------------------------------------------------------ */
/* MID$ as LHS: MID$(target, start, length) = value                   */
/* ------------------------------------------------------------------ */

/*
 * bs_mid_assign -- replace characters in target in-place.
 *   start  : 1-based; must be within [1, target->len]
 *   length : number of chars to replace (-1 means "to end")
 *   value  : source string; only min(value->len, length) chars are copied
 *   Does NOT extend the target string.
 */
void bs_mid_assign(BString *target, int start, int length, const BString *value)
{
    int tlen;
    int from;     /* 0-based start in target */
    int avail;    /* chars available in target from 'from' */
    int vlen;     /* length of value */
    int count;    /* chars to copy */

    if (!target || !value) return;

    tlen = target->len;
    if (start < 1) start = 1;
    from = start - 1;

    if (from >= tlen) return;  /* start beyond end: no-op */

    avail = tlen - from;

    if (length < 0) {
        /* -1 means "use all remaining target positions" */
        count = avail;
    } else {
        count = length;
        if (count > avail) count = avail;
    }

    vlen = value->len;
    if (count > vlen) count = vlen;  /* only copy what value provides */

    if (count > 0)
        memcpy(target->data + from, value->data, (size_t)count);
    /* target->len and NUL terminator are unchanged */
}

/* ------------------------------------------------------------------ */
/* GW-BASIC random-access record string helpers:                      */
/*   LSET / RSET / MKI$ / MKL$ / MKS$ / MKD$ / CVI / CVL / CVS / CVD  */
/* ------------------------------------------------------------------ */

/*
 * bs_lset -- LSET dst$ = src$
 * Left-justifies src$ into dst$ at dst$'s current length, padding with spaces.
 * Does not change LEN(dst$).
 */
void bs_lset(BString *dst, const BString *src)
{
    int   tlen;
    int   slen;
    int   copy_len;
    char *tmp;

    if (!dst) return;
    tlen = (dst->data && dst->len > 0) ? dst->len : 0;
    if (tlen <= 0) {
        bs__set_from_buf(dst, "", 0);
        return;
    }

    slen = (src && src->data && src->len > 0) ? src->len : 0;
    copy_len = (slen < tlen) ? slen : tlen;

    tmp = (char *)malloc((size_t)tlen);
    if (!tmp) { fprintf(stderr, "bs: out of memory\n"); exit(1); }
    memset(tmp, ' ', (size_t)tlen);
    if (copy_len > 0) {
        memcpy(tmp, src->data, (size_t)copy_len);
    }
    bs__set_from_buf(dst, tmp, tlen);
    free(tmp);
}

/*
 * bs_rset -- RSET dst$ = src$
 * Right-justifies src$ into dst$ at dst$'s current length, padding with
 * leading spaces if LEN(src$) < LEN(dst$), or truncating on the right if
 * LEN(src$) > LEN(dst$).  Does not change LEN(dst$).
 */
void bs_rset(BString *dst, const BString *src)
{
    int   tlen;
    int   slen;
    char *tmp;

    if (!dst) return;
    tlen = (dst->data && dst->len > 0) ? dst->len : 0;
    if (tlen <= 0) {
        bs__set_from_buf(dst, "", 0);
        return;
    }

    slen = (src && src->data && src->len > 0) ? src->len : 0;

    tmp = (char *)malloc((size_t)tlen);
    if (!tmp) { fprintf(stderr, "bs: out of memory\n"); exit(1); }
    memset(tmp, ' ', (size_t)tlen);
    if (slen >= tlen) {
        memcpy(tmp, src->data, (size_t)tlen);
    } else if (slen > 0) {
        memcpy(tmp + (tlen - slen), src->data, (size_t)slen);
    }
    bs__set_from_buf(dst, tmp, tlen);
    free(tmp);
}

/*
 * MKI$(n%), MKL$(n&), MKS$(n!), MKD$(n#)
 * Pack binary representation of numeric value into a fixed-length BString.
 */
void bs_mki(BString *dst, int val)
{
    short s = (short)val;
    bs__set_from_buf(dst, (const char *)&s, 2);
}

void bs_mkl(BString *dst, long val)
{
    int v = (int)val;
    bs__set_from_buf(dst, (const char *)&v, 4);
}

void bs_mks(BString *dst, float val)
{
    bs__set_from_buf(dst, (const char *)&val, 4);
}

void bs_mkd(BString *dst, double val)
{
    bs__set_from_buf(dst, (const char *)&val, 8);
}

/*
 * CVI(s$), CVL(s$), CVS(s$), CVD(s$)
 * Unpack binary bytes from a BString back into a numeric value.
 */
int bs_cvi(const BString *s)
{
    short v = 0;
    if (!s || !s->data || s->len < 2) {
        bs_raise_error(5, 0); /* Illegal function call */
        return 0;
    }
    memcpy(&v, s->data, 2);
    return (int)v;
}

long bs_cvl(const BString *s)
{
    int v = 0;
    if (!s || !s->data || s->len < 4) {
        bs_raise_error(5, 0); /* Illegal function call */
        return 0L;
    }
    memcpy(&v, s->data, 4);
    return (long)v;
}

float bs_cvs(const BString *s)
{
    float v = 0.0f;
    if (!s || !s->data || s->len < 4) {
        bs_raise_error(5, 0); /* Illegal function call */
        return 0.0f;
    }
    memcpy(&v, s->data, 4);
    return v;
}

double bs_cvd(const BString *s)
{
    double v = 0.0;
    if (!s || !s->data || s->len < 8) {
        bs_raise_error(5, 0); /* Illegal function call */
        return 0.0;
    }
    memcpy(&v, s->data, 8);
    return v;
}
