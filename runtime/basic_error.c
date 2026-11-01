/*
 * basic_error.c -- TRY/CATCH and ON ERROR error handling.
 * C89 compatible.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "basic_runtime.h"

/* ------------------------------------------------------------------ */
/* Error code table                                                    */
/* ------------------------------------------------------------------ */
typedef struct {
    int         code;
    const char *msg;
} BsErrEntry;

static const BsErrEntry s_err_table[] = {
    {  1, "NEXT without FOR"          },
    {  2, "Syntax error"              },
    {  3, "RETURN without GOSUB"      },
    {  4, "Out of DATA"               },
    {  5, "Illegal function call"     },
    {  6, "Overflow"                  },
    {  7, "Out of memory"             },
    {  9, "Subscript out of range"    },
    { 10, "Duplicate definition"      },
    { 11, "Division by zero"          },
    { 13, "Type mismatch"             },
    { 14, "Out of string space"       },
    { 19, "No RESUME"                 },
    { 20, "RESUME without error"      },
    { 24, "Device timeout"            },
    { 27, "Device I/O error"          },
    { 50, "Field overflow"            },
    { 52, "Bad file number"           },
    { 53, "File not found"            },
    { 54, "Bad file mode"             },
    { 55, "File already open"         },
    { 57, "Device I/O error"          },
    { 58, "File already exists"       },
    { 61, "Disk full"                 },
    { 62, "Input past end of file"    },
    { 63, "Bad record number"         },
    { 64, "Bad file name"             },
    { 67, "Too many files"            },
    {  0, NULL                        }  /* sentinel */
};

/* Scratch buffer for unknown codes: "Error NNN\0" */
#define BS_ERR_BUF_LEN 32
static char s_err_buf[BS_ERR_BUF_LEN];

static const char *error_msg_for(int code)
{
    /* Declarations first (C89) */
    const BsErrEntry *e;
    char         tmp[16];
    int          i = 0, j;
    unsigned int u;

    for (e = s_err_table; e->msg != NULL; ++e) {
        if (e->code == code) {
            return e->msg;
        }
    }

    /* Unknown code -- format "Error NNN" into static buffer */
    s_err_buf[0] = 'E'; s_err_buf[1] = 'r'; s_err_buf[2] = 'r';
    s_err_buf[3] = 'o'; s_err_buf[4] = 'r'; s_err_buf[5] = ' ';
    s_err_buf[6] = '\0';

    u = (unsigned int)(code < 0 ? -code : code);
    if (u == 0) {
        tmp[i++] = '0';
    } else {
        while (u) { tmp[i++] = (char)('0' + u % 10); u /= 10; }
    }
    /* tmp digits are in reverse order -- copy them forward */
    for (j = i - 1; j >= 0; --j) {
        int k = 6 + (i - 1 - j);
        if (k < BS_ERR_BUF_LEN - 1) s_err_buf[k] = tmp[j];
    }
    s_err_buf[6 + i] = '\0';

    return s_err_buf;
}

/* ------------------------------------------------------------------ */
/* Tier 1: TRY/CATCH -- BsErrorFrame stack                            */
/* ------------------------------------------------------------------ */
BsErrorFrame *bs_error_top = NULL;

void bs_error_push(BsErrorFrame *f)
{
    memset(&f->msg, 0, sizeof(BString));
    f->code = 0;
    f->line = 0;
    f->prev = bs_error_top;
    bs_error_top = f;
}

void bs_error_pop(void)
{
    if (bs_error_top) {
        bs_string_free(&bs_error_top->msg);
        bs_error_top = bs_error_top->prev;
    }
}

/* ------------------------------------------------------------------ */
/* Error state accessors (ERR, ERL, ERROR$ built-ins)                 */
/* ------------------------------------------------------------------ */
static int    s_err_code = 0;
static int    s_erl_line = 0;
static BString s_err_msg_str; /* initialised to zero by C static storage */

int bs_err_code(void) { return s_err_code; }
int bs_erl_line(void) { return s_erl_line; }

void bs_err_msg(BString *dst)
{
    bs_string_assign(dst, &s_err_msg_str);
}

void bs_error_str(BString *dst, int code)
{
    bs_string_assign_cstr(dst, error_msg_for(code));
}

/* ------------------------------------------------------------------ */
/* Tier 2: ON ERROR GOTO / RESUME NEXT                                */
/* ------------------------------------------------------------------ */
static int     s_on_error_active = 0;
static jmp_buf s_on_error_jmp;
static int     s_on_error_code   = 0;
static int     s_on_error_line   = 0;
static BString s_on_error_msg;   /* initialised to zero by C static storage */

/* RESUME NEXT jump buffer -- registered by emitter before each protected block */
static jmp_buf  s_resume_next_buf;
static jmp_buf *s_resume_next_jmp = NULL;

jmp_buf *bs_resume_next_jmpbuf(void)
{
    s_resume_next_jmp = &s_resume_next_buf;
    return &s_resume_next_buf;
}

/*
 * bs_on_error_arm() -- called by emitter just after the setjmp that captures
 * the ON ERROR destination.  The setjmp itself is done in emitted code via:
 *
 *   if (setjmp(*bs_on_error_jmpbuf()) != 0) { goto label_NAME; }
 *
 * so this function merely marks the handler as armed.
 */
void bs_on_error_arm(void)
{
    s_on_error_active = 1;
}

void bs_on_error_off(void)
{
    s_on_error_active = 0;
}

int bs_on_error_is_armed(void)
{
    return s_on_error_active;
}

jmp_buf *bs_on_error_jmpbuf(void)
{
    return &s_on_error_jmp;
}

/*
 * bs_set_resume_next_jmp() -- emitter calls this to register the jmp_buf
 * that RESUME NEXT should jump back to.
 */
void bs_set_resume_next_jmp(jmp_buf *j)
{
    s_resume_next_jmp = j;
}

/*
 * bs_resume_next() -- called from within an ON ERROR handler body.
 * Jumps back to the resume-next point established by bs_set_resume_next_jmp().
 */
void bs_resume_next(void)
{
    if (s_resume_next_jmp) {
        longjmp(*s_resume_next_jmp, 1);
    }
    /* No resume point registered -- fall through (no-op) */
}

/* ------------------------------------------------------------------ */
/* Core raise function                                                 */
/* ------------------------------------------------------------------ */
void bs_raise_error(int code, int line)
{
    const char *msg = error_msg_for(code);

    /* Update global error state for ERR / ERL / ERROR$ */
    s_err_code = code;
    s_erl_line = line;
    bs_string_assign_cstr(&s_err_msg_str, msg);

    /* Tier 1: innermost TRY/CATCH frame */
    if (bs_error_top != NULL) {
        bs_error_top->code = code;
        bs_error_top->line = line;
        bs_string_assign_cstr(&bs_error_top->msg, msg);
        longjmp(bs_error_top->jmp, 1);
    }

    /* Tier 2: module-level ON ERROR GOTO handler */
    if (s_on_error_active) {
        s_on_error_code = code;
        s_on_error_line = line;
        bs_string_assign_cstr(&s_on_error_msg, msg);
        longjmp(s_on_error_jmp, 1);
    }

    /* No handler -- print and abort */
    fprintf(stderr, "Runtime error %d at line %d: %s\n", code, line, msg);
    exit(1);
}

/* Legacy entry point: bs_on_error_goto(jmp) copies the jmp_buf directly.
 * Kept for source compatibility with earlier stubs. */
void bs_on_error_goto(jmp_buf *jmp)
{
    memcpy(s_on_error_jmp, *jmp, sizeof(jmp_buf));
    s_on_error_active = 1;
}
