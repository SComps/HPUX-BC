/*
 * basic_io.c -- Console I/O (PRINT, INPUT, LINE INPUT).
 * C89 compatible.
 *
 * PRINT number formatting (HP-UX BASIC compatible):
 *   Positive / zero : leading space, no trailing space  e.g. " 42"
 *   Negative        : minus sign, no leading space       e.g. "-42"
 *   Float / double  : %g format, same sign rule applies
 *
 * bs_print_tab(col): advance output to 1-based column col (spaces only).
 *   If the cursor is already at or past that column, print one space.
 *
 * PRINT USING format characters supported:
 *   #   digit placeholder
 *   .   decimal point
 *   +   leading/trailing plus sign
 *   -   trailing minus for negatives
 *   $   dollar sign (single or double $$ = floating dollar)
 *   *   fill with asterisks
 *   \n\ string field n+2 wide  (backslash ... backslash)
 *   &   variable-width string field
 *   !   single character from string
 *   ^^^^  scientific notation suffix
 *
 * INPUT line buffer:
 *   bs_input_prompt() prints prompt + "? " then reads one line into buffer.
 *   Subsequent bs_input_*() calls consume comma-separated tokens from that
 *   buffer.  When the buffer is exhausted a new line is read automatically.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <math.h>
#include "basic_runtime.h"

/* ------------------------------------------------------------------ */
/* Output column tracking (for TAB)                                    */
/* ------------------------------------------------------------------ */
static int s_col = 0; /* 1-based column of *next* character to print  */

static void track_write(const char *buf, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        if (buf[i] == '\n') s_col = 0;
        else                s_col++;
    }
}

static void print_chars(const char *buf, int n)
{
    fwrite(buf, 1, (size_t)n, stdout);
    track_write(buf, n);
}

static void print_cstr(const char *s)
{
    print_chars(s, (int)strlen(s));
}

/* ------------------------------------------------------------------ */
/* PRINT scalar helpers                                                 */
/* ------------------------------------------------------------------ */

void bs_print_int(int v)
{
    char buf[32];
    if (v >= 0)
        sprintf(buf, " %d", v);
    else
        sprintf(buf, "%d", v);
    print_cstr(buf);
}

void bs_print_long(long v)
{
    char buf[32];
    if (v >= 0)
        sprintf(buf, " %ld", v);
    else
        sprintf(buf, "%ld", v);
    print_cstr(buf);
}

void bs_print_float(float v)
{
    char buf[64];
    if (v >= 0.0f)
        sprintf(buf, " %.7g", (double)v);
    else
        sprintf(buf, "%.7g", (double)v);
    print_cstr(buf);
}

void bs_print_double(double v)
{
    char buf[64];
    if (v >= 0.0)
        sprintf(buf, " %.15g", v);
    else
        sprintf(buf, "%.15g", v);
    print_cstr(buf);
}

void bs_print_string(const BString *s)
{
    if (s && s->data)
        print_chars(s->data, s->len);
}

void bs_print_newline(void)
{
    putchar('\n');
    s_col = 0;
}

/*
 * bs_print_tab -- advance to 1-based column col.
 * If we are already at or past col, print one space.
 */
void bs_print_tab(int col)
{
    int cur = s_col + 1; /* convert to 1-based */
    int spaces;
    if (cur >= col) spaces = 1;
    else            spaces = col - cur;
    while (spaces-- > 0) {
        putchar(' ');
        s_col++;
    }
}

/* bs_print_semicolon -- no-op; semicolons suppress newline in the emitter */
void bs_print_semicolon(void) { /* nothing */ }

/* ------------------------------------------------------------------ */
/* PRINT USING                                                          */
/* ------------------------------------------------------------------ */

/*
 * Numeric format field descriptor parsed from the format string.
 */
typedef struct {
    int  digits_before;   /* # and , placeholders before decimal */
    int  digits_after;    /* # placeholders after decimal (0 = no decimal) */
    int  has_dot;
    int  has_commas;      /* 1 if thousands-separator commas are requested */
    int  force_sign;      /* '+' prefix or suffix */
    int  trail_minus;     /* trailing minus for negatives */
    int  dollar;          /* 1 = single $, 2 = floating $$ */
    int  star_fill;       /* fill with '*' instead of spaces */
    int  sci;             /* number of '^' after (0 or 4) */
    int  field_width;     /* total character width of this format field */
} NumFmt;

/*
 * parse_num_fmt -- parse one numeric format field from *pp, fill nf.
 * Returns 0 on failure.
 */
static int parse_num_fmt(const char *p, const char **end, NumFmt *nf)
{
    int w = 0;
    int digits_before = 0;
    int digits_after  = 0;
    int has_dot       = 0;
    int has_commas    = 0;
    int force_sign    = 0;
    int trail_minus   = 0;
    int dollar        = 0;
    int star_fill     = 0;
    int sci           = 0;
    int seen_digit    = 0;

    memset(nf, 0, sizeof(*nf));

    /* Leading '+' */
    if (*p == '+') { force_sign = 1; p++; w++; }

    /* '$$' or '$' before digits */
    if (p[0] == '$' && p[1] == '$') {
        dollar = 2; p += 2; w += 2;
    } else if (*p == '$') {
        dollar = 1; p++; w++;
    }

    /* '**' (asterisk fill — implies two leading digits or so) */
    if (p[0] == '*' && p[1] == '*') {
        star_fill = 1;
        digits_before += 2; /* ** counts as two digit positions */
        p += 2; w += 2;
    }

    /* digit placeholders (and embedded thousands commas) before decimal */
    while (*p == '#' || (seen_digit && *p == ',' && (p[1] == '#' || p[1] == '.'))) {
        if (*p == ',') has_commas = 1;
        else           seen_digit = 1;
        digits_before++;
        p++;
        w++;
    }

    if (!seen_digit && digits_before == 0) return 0; /* not a numeric field */

    /* decimal point */
    if (*p == '.') {
        has_dot = 1; p++; w++;
        while (*p == '#') { digits_after++; p++; w++; }
    }

    /* trailing sign modifiers */
    if (*p == '+') { force_sign = 1; trail_minus = 0; p++; w++; }
    else if (*p == '-') { trail_minus = 1; p++; w++; }

    /* scientific notation suffix ^^^^ (exactly 4) */
    if (p[0] == '^' && p[1] == '^' && p[2] == '^' && p[3] == '^') {
        sci = 4; p += 4; w += 4;
    }

    nf->digits_before = digits_before;
    nf->digits_after  = digits_after;
    nf->has_dot       = has_dot;
    nf->has_commas    = has_commas;
    nf->force_sign    = force_sign;
    nf->trail_minus   = trail_minus;
    nf->dollar        = dollar;
    nf->star_fill     = star_fill;
    nf->sci           = sci;
    nf->field_width   = w;

    *end = p;
    return 1;
}

/*
 * format_number -- render double val using NumFmt into out (caller provides
 * a large-enough buffer).  Returns pointer to out.
 */
static char *format_number(double val, const NumFmt *nf, char *out)
{
    char rawbuf[128];
    char numbuf[160];
    char result[256];
    int  negative = (val < 0.0);
    int  rlen, width, i, pad;
    char fill_ch;
    char *dst;

    /* Build the bare number string */
    if (nf->sci) {
        if (nf->has_dot)
            sprintf(rawbuf, "%.*E", nf->digits_after, fabs(val));
        else
            sprintf(rawbuf, "%E", fabs(val));
    } else if (nf->has_dot) {
        sprintf(rawbuf, "%.*f", nf->digits_after, fabs(val));
    } else {
        sprintf(rawbuf, "%.0f", fabs(val));
    }

    /* Insert thousands commas into integer part if requested */
    if (nf->has_commas && !nf->sci) {
        char *dp = strchr(rawbuf, '.');
        int int_len = dp ? (int)(dp - rawbuf) : (int)strlen(rawbuf);
        int ni = 0;
        for (i = 0; i < int_len; i++) {
            if (i > 0 && ((int_len - i) % 3) == 0) {
                numbuf[ni++] = ',';
            }
            numbuf[ni++] = rawbuf[i];
        }
        if (dp) {
            strcpy(numbuf + ni, dp);
        } else {
            numbuf[ni] = '\0';
        }
    } else {
        strcpy(numbuf, rawbuf);
    }

    /* Total field width (without sign and dollar) */
    fill_ch = nf->star_fill ? '*' : ' ';

    /* Start assembling result */
    dst = result;

    /* Leading sign */
    if (!nf->trail_minus && !nf->force_sign) {
        if (negative) *dst++ = '-';
        /* positive: leading space already part of field (added as padding) */
    } else if (nf->force_sign && !nf->trail_minus) {
        *dst++ = negative ? '-' : '+';
    }

    /* Dollar sign (single fixed position) */
    if (nf->dollar == 1) *dst++ = '$';

    /* Padding + floating dollar or star fill */
    rlen = (int)strlen(numbuf);
    /* digits_before positions for integral part */
    {
        int dot_pos = 0;
        char *dp = strchr(numbuf, '.');
        int  int_digits;
        if (dp) dot_pos = (int)(dp - numbuf);
        else    dot_pos = rlen;
        int_digits = dot_pos;

        pad = nf->digits_before - int_digits;
        if (pad < 0) pad = 0;

        if (nf->dollar == 2) {
            /* floating dollar: fill pad-1 chars, then '$', then rest */
            int k;
            for (k = 0; k < pad - 1; k++) *dst++ = fill_ch;
            *dst++ = '$';
        } else {
            int k;
            for (k = 0; k < pad; k++) *dst++ = fill_ch;
        }
    }

    /* The number itself */
    {
        int k;
        for (k = 0; numbuf[k]; k++) *dst++ = numbuf[k];
    }

    /* Trailing sign */
    if (nf->trail_minus) { *dst++ = negative ? '-' : ' '; }
    else if (nf->force_sign && nf->trail_minus) { *dst++ = negative ? '-' : '+'; }

    *dst = '\0';

    /* Copy to out */
    width = (int)(dst - result);
    for (i = 0; i < width; i++) out[i] = result[i];
    out[width] = '\0';
    return out;
}

/*
 * bs_print_using -- format string interpreter.
 *
 * Usage: bs_print_using(fmt, argc, arg0, arg1, ...)
 * where each arg is passed as a double (numeric) or BString* (string).
 * The caller must pass argc and then interleave type tags and values:
 *
 *   bs_print_using(fmt, n,
 *       BSPU_NUM, (double)val,
 *       BSPU_STR, (BString*)ptr, ...);
 *
 * However the plan specifies the prototype as (const BString *fmt, ...)
 * with int argc as first vararg.  We implement that convention.
 *
 * Tag values:
 *   0 = double numeric
 *   1 = BString* string
 */
void bs_vprint_using(const BString *fmt, va_list ap)
{
    int     argc;
    int     argi;
    /* Arg storage (up to 64 args) */
    int      arg_type[64];
    double   arg_num[64];
    BString *arg_str[64];

    const char *p;
    char outbuf[256];

    if (!fmt || !fmt->data) return;

    argc = va_arg(ap, int);
    if (argc > 64) argc = 64;
    for (argi = 0; argi < argc; argi++) {
        arg_type[argi] = va_arg(ap, int);
        if (arg_type[argi] == BSPU_STR) {
            arg_str[argi] = va_arg(ap, BString *);
            arg_num[argi] = 0.0;
        } else {
            arg_num[argi] = va_arg(ap, double);
            arg_str[argi] = NULL;
        }
    }

    argi = 0;
    p    = fmt->data;

    while (*p) {
        /* --- Numeric field: starts with +, $, *, # --- */
        if (*p == '+' || *p == '$' || *p == '#' ||
            (*p == '*' && *(p+1) == '*')) {
            NumFmt nf;
            const char *after;
            if (parse_num_fmt(p, &after, &nf)) {
                double val = 0.0;
                if (argi < argc) {
                    if (arg_type[argi] == BSPU_NUM) {
                        val = arg_num[argi++];
                    } else if (arg_type[argi] == BSPU_STR && arg_str[argi]) {
                        val = bs_val(arg_str[argi]);
                        argi++;
                    } else {
                        argi++;
                    }
                }
                format_number(val, &nf, outbuf);
                print_cstr(outbuf);
                p = after;
                continue;
            }
        }

        /* --- String field: \ ... \ (backslash-n spaces-backslash = n+2 wide) --- */
        if (*p == '\\') {
            const char *q = p + 1;
            int width = 2;
            while (*q && *q != '\\') { q++; width++; }
            if (*q == '\\') {
                /* string field of width chars */
                char num_tmp[64];
                const char *sdata = "";
                int slen = 0;
                if (argi < argc && arg_type[argi] == BSPU_STR && arg_str[argi]) {
                    sdata = arg_str[argi]->data ? arg_str[argi]->data : "";
                    slen  = arg_str[argi]->len;
                    argi++;
                } else if (argi < argc && arg_type[argi] == BSPU_NUM) {
                    sprintf(num_tmp, "%.15g", arg_num[argi++]);
                    sdata = num_tmp;
                    slen  = (int)strlen(num_tmp);
                } else if (argi < argc) { argi++; }
                if (slen > width) slen = width;
                print_chars(sdata, slen);
                /* pad with spaces */
                { int k; for (k = slen; k < width; k++) { putchar(' '); s_col++; } }
                p = q + 1;
                continue;
            }
            /* unmatched backslash — fall through to literal */
        }

        /* --- & : variable-width string --- */
        if (*p == '&') {
            if (argi < argc && arg_type[argi] == BSPU_STR && arg_str[argi]) {
                print_chars(arg_str[argi]->data, arg_str[argi]->len);
                argi++;
            } else if (argi < argc && arg_type[argi] == BSPU_NUM) {
                char num_tmp[64];
                sprintf(num_tmp, "%.15g", arg_num[argi++]);
                print_cstr(num_tmp);
            } else if (argi < argc) { argi++; }
            p++;
            continue;
        }

        /* --- ! : single character from string --- */
        if (*p == '!') {
            if (argi < argc && arg_type[argi] == BSPU_STR && arg_str[argi] &&
                    arg_str[argi]->len > 0) {
                char c = arg_str[argi]->data[0];
                putchar(c);
                s_col++;
                argi++;
            } else if (argi < argc) { argi++; }
            p++;
            continue;
        }

        /* --- Literal character --- */
        putchar(*p);
        if (*p == '\n') s_col = 0; else s_col++;
        p++;
    }
}

void bs_print_using(const BString *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    bs_vprint_using(fmt, ap);
    va_end(ap);
}

/* ------------------------------------------------------------------ */
/* INPUT line buffer                                                    */
/* ------------------------------------------------------------------ */

#define INPUT_BUF_SIZE 1024

static char  s_input_buf[INPUT_BUF_SIZE];
static char *s_input_ptr = NULL;  /* current parse position in buffer */

/*
 * refill_input -- read a new line from stdin into s_input_buf.
 * Called when the buffer is empty.
 */
static void refill_input(void)
{
    bs_shell_cleanup(); /* ensure normal blocking/echo tty mode for INPUT */
    if (!fgets(s_input_buf, INPUT_BUF_SIZE, stdin)) {
        s_input_buf[0] = '\0';
    }
    s_input_ptr = s_input_buf;
}

/*
 * skip_comma -- skip leading whitespace and an optional comma separator.
 */
static void skip_comma(void)
{
    int at_line_start;
    if (!s_input_ptr) { refill_input(); return; }
    at_line_start = (s_input_ptr == s_input_buf);
    /* skip spaces */
    while (*s_input_ptr == ' ' || *s_input_ptr == '\t')
        s_input_ptr++;
    if (!at_line_start) {
        /* skip comma */
        if (*s_input_ptr == ',') s_input_ptr++;
        /* skip spaces again */
        while (*s_input_ptr == ' ' || *s_input_ptr == '\t')
            s_input_ptr++;
        /* if we hit end of line, refill */
        if (*s_input_ptr == '\0' || *s_input_ptr == '\n' || *s_input_ptr == '\r') {
            refill_input();
        }
    }
}

/*
 * bs_input_prompt -- print prompt string, optionally append "? ", then read a line.
 *   show_question: 1 = semicolon separator (print "? "), 0 = comma separator (no "? ")
 */
void bs_input_prompt(const BString *prompt, int show_question)
{
    if (prompt && prompt->data) {
        fputs(prompt->data, stdout);
    }
    if (show_question) {
        fputs("? ", stdout);
    }
    fflush(stdout);
    refill_input();
}

void bs_input_int(int *v)
{
    int n = 0;
    skip_comma();
    if (s_input_ptr) {
        sscanf(s_input_ptr, "%d%n", v, &n);
        s_input_ptr += n;
    }
}

void bs_input_long(long *v)
{
    int n = 0;
    skip_comma();
    if (s_input_ptr) {
        sscanf(s_input_ptr, "%ld%n", v, &n);
        s_input_ptr += n;
    }
}

void bs_input_float(float *v)
{
    int n = 0;
    skip_comma();
    if (s_input_ptr) {
        sscanf(s_input_ptr, "%f%n", v, &n);
        s_input_ptr += n;
    }
}

void bs_input_double(double *v)
{
    int n = 0;
    skip_comma();
    if (s_input_ptr) {
        sscanf(s_input_ptr, "%lf%n", v, &n);
        s_input_ptr += n;
    }
}

/*
 * bs_input_string -- read next comma-delimited token (or quoted string).
 */
void bs_input_string(BString *v)
{
    char tokbuf[INPUT_BUF_SIZE];
    int  i = 0;
    char *p;

    skip_comma();
    if (!s_input_ptr) { bs_string_assign_cstr(v, ""); return; }
    p = s_input_ptr;

    if (*p == '"') {
        /* Quoted string — read until closing quote */
        p++;
        while (*p && *p != '"' && *p != '\n' && i < INPUT_BUF_SIZE - 1)
            tokbuf[i++] = *p++;
        if (*p == '"') p++;
    } else {
        /* Unquoted — read until comma or newline */
        while (*p && *p != ',' && *p != '\n' && *p != '\r' && i < INPUT_BUF_SIZE - 1)
            tokbuf[i++] = *p++;
    }
    tokbuf[i] = '\0';
    s_input_ptr = p;
    bs_string_assign_cstr(v, tokbuf);
}

/*
 * bs_line_input -- read entire line (no prompt, no comma splitting).
 */
void bs_line_input(BString *v)
{
    int len;
    if (!s_input_ptr) {
        refill_input();
    }
    /* strip trailing newline */
    len = (int)strlen(s_input_ptr);
    while (len > 0 && (s_input_ptr[len - 1] == '\n' || s_input_ptr[len - 1] == '\r')) {
        s_input_ptr[--len] = '\0';
    }
    bs_string_assign_cstr(v, s_input_ptr);
    s_input_ptr = NULL;
}

/* -------------------------------------------------------------------------
 * Terminal control -- CLS, LOCATE, COLOR
 * Uses ANSI/VT100 escape sequences, which HP-UX terminals (xterm, hpterm)
 * support.  If the terminal is not ANSI-compatible the escape codes appear
 * as literal characters but cause no crash.
 * ------------------------------------------------------------------------- */

/*
 * bs_cls -- clear the screen and home the cursor.
 */
void bs_cls(void)
{
    /* ESC[2J clears screen; ESC[H homes cursor */
    fputs("\033[2J\033[H", stdout);
    fflush(stdout);
    s_col = 1;
}

/*
 * bs_locate -- move cursor to 1-based (row, col).
 * If col <= 0, only the row is changed (col defaults to 1).
 */
void bs_locate(int row, int col)
{
    if (row < 1) row = 1;
    if (col < 1) col = 1;
    fprintf(stdout, "\033[%d;%dH", row, col);
    fflush(stdout);
    s_col = col;
}

/*
 * bs_color -- set foreground and background colour using ANSI colour codes.
 *
 * Mapping (matches QuickBASIC / ANSI colour numbers):
 *   0=Black  1=Red(dark)  2=Green(dark)  3=Yellow/Brown  4=Blue(dark)
 *   5=Magenta(dark)  6=Cyan(dark)  7=White/Gray
 *   8=Dark Gray  9=Red(bright)  10=Green(bright)  11=Yellow(bright)
 *   12=Blue(bright)  13=Magenta(bright)  14=Cyan(bright)  15=White(bright)
 *
 * bg <= 0 means leave background unchanged.
 */
void bs_color(int fg, int bg)
{
    /*
     * ANSI foreground: 30-37 (normal), 90-97 (bright)
     * ANSI background: 40-47 (normal), 100-107 (bright)
     */
    static const int ansi_fg[16] = {
        30, 31, 32, 33, 34, 35, 36, 37,   /* 0-7  dark */
        90, 91, 92, 93, 94, 95, 96, 97    /* 8-15 bright */
    };
    static const int ansi_bg[8] = {
        40, 41, 42, 43, 44, 45, 46, 47
    };

    if (fg >= 0 && fg <= 15) {
        if (bg >= 0 && bg <= 7) {
            fprintf(stdout, "\033[%d;%dm", ansi_fg[fg], ansi_bg[bg]);
        } else {
            fprintf(stdout, "\033[%dm", ansi_fg[fg]);
        }
    } else if (bg >= 0 && bg <= 7) {
        fprintf(stdout, "\033[%dm", ansi_bg[bg]);
    }
    fflush(stdout);
}

/*
 * bs_bgcolor -- set background colour only; leave foreground unchanged.
 * bg: 0-7 (same ANSI colour table as bs_color).
 */
void bs_bgcolor(int bg)
{
    static const int ansi_bg[8] = {
        40, 41, 42, 43, 44, 45, 46, 47
    };
    if (bg >= 0 && bg <= 7) {
        fprintf(stdout, "\033[%dm", ansi_bg[bg]);
        fflush(stdout);
    }
}
