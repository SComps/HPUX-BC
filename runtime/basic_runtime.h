/*
 * basic_runtime.h -- Public API for the HP-UX BASIC runtime library.
 * Included by all emitted C files.
 * C89 compatible.
 */

#ifndef BASIC_RUNTIME_H
#define BASIC_RUNTIME_H

#include <stdio.h>
#include <stdarg.h>
#include <setjmp.h>

/* ------------------------------------------------------------------ */
/* BString -- dynamic heap string                                      */
/* ------------------------------------------------------------------ */
typedef struct {
    char *data;  /* NUL-terminated buffer */
    int   len;   /* current length (bytes, not counting NUL) */
    int   cap;   /* allocated capacity including NUL */
} BString;

/* basic_string.c */
/* Lifecycle */
void     bs_string_init(BString *b);
void     bs_string_free(BString *b);
BString  bs_string_from_cstr(const char *s);
void     bs_string_assign(BString *dst, const BString *src);
void     bs_string_assign_cstr(BString *dst, const char *s);
void     bs_string_assign_buf(BString *dst, const char *buf, int len);
void     bs_string_grow(BString *b, int new_cap);

/* Legacy names kept for compatibility */
BString *bs_new(const char *s);
void     bs_free(BString *b);
void     bs_assign(BString *dst, const BString *src);
void     bs_assign_cstr(BString *dst, const char *s);

/* Core ops */
void     bs_concat(BString *dst, const BString *a, const BString *b);
int      bs_compare(const BString *a, const BString *b);

/* Built-in functions */
int      bs_len(const BString *b);
void     bs_mid(BString *dst, const BString *s, int start, int len);
void     bs_mid_assign(BString *dst, int start, int len, const BString *src);
void     bs_left(BString *dst, const BString *s, int n);
void     bs_right(BString *dst, const BString *s, int n);
void     bs_ucase(BString *dst, const BString *s);
void     bs_lcase(BString *dst, const BString *s);
void     bs_ltrim(BString *dst, const BString *s);
void     bs_rtrim(BString *dst, const BString *s);
void     bs_trim(BString *dst, const BString *s);
int      bs_instr(int start, const BString *hay, const BString *needle);
void     bs_str(BString *dst, double val);
double   bs_val(const BString *s);
void     bs_chr(BString *dst, int code);
int      bs_asc(const BString *s);
void     bs_space(BString *dst, int n);
void     bs_string_fn(BString *dst, int n, int ch);
void     bs_hex(BString *dst, long val);
void     bs_oct(BString *dst, long val);

/* GW-BASIC random-access record string helpers */
void     bs_lset(BString *dst, const BString *src);
void     bs_rset(BString *dst, const BString *src);
void     bs_mki(BString *dst, int val);
void     bs_mkl(BString *dst, long val);
void     bs_mks(BString *dst, float val);
void     bs_mkd(BString *dst, double val);
int      bs_cvi(const BString *s);
long     bs_cvl(const BString *s);
float    bs_cvs(const BString *s);
double   bs_cvd(const BString *s);

/* Runtime init/cleanup -- basic_runtime.c */
void     bs_runtime_set_args(int argc, char **argv);
void     bs_runtime_init(void);
void     bs_runtime_cleanup(void);
void     bs_command(BString *dst, int idx);

/* ------------------------------------------------------------------ */
/* I/O -- basic_io.c                                                   */
/* ------------------------------------------------------------------ */

/* PRINT USING argument type tags */
#define BSPU_NUM 0
#define BSPU_STR 1

void bs_print_int(int v);
void bs_print_long(long v);
void bs_print_float(float v);
void bs_print_double(double v);
void bs_print_string(const BString *s);
void bs_print_newline(void);
void bs_print_tab(int col);
void bs_print_semicolon(void);
void bs_print_using(const BString *fmt, ...);
void bs_vprint_using(const BString *fmt, va_list ap);

/* Terminal control */
void bs_cls(void);
void bs_locate(int row, int col);
void bs_color(int fg, int bg);
void bs_bgcolor(int bg);
int  bs_term_rows(void);
int  bs_term_cols(void);

/* ------------------------------------------------------------------ */
/* LPRINT -- line printer via HP-UX lp spooler -- basic_lprint.c      */
/* ------------------------------------------------------------------ */
void bs_lprint_open(const BString *printer_name); /* NULL = default   */
void bs_lprint_close(void);                       /* flush + submit    */
void bs_lprint_begin(void);                       /* called pre-stmt   */
void bs_lprint_cleanup(void);                     /* called at exit    */
void bs_lprint_int(int v);
void bs_lprint_long(long v);
void bs_lprint_float(float v);
void bs_lprint_double(double v);
void bs_lprint_string(const BString *s);
void bs_lprint_newline(void);
void bs_lprint_using(const BString *fmt, ...);

/* ------------------------------------------------------------------ */
/* SHELL / SHELL$ / INKEY$ / ERRLEVEL -- basic_shell.c                 */
/* ------------------------------------------------------------------ */
void    bs_shell(const BString *cmd);        /* SHELL [cmd]            */
int     bs_errlevel(void);                   /* ERRLEVEL function      */
void    bs_shell_str(BString *dst, const BString *cmd); /* SHELL$(cmd) */
BString bs_shell_str_tmp(const BString *cmd);/* returns by value       */
void    bs_inkey(BString *dst);              /* INKEY$ (raw)           */
BString bs_inkey_tmp(void);                  /* returns by value       */
void    bs_shell_cleanup(void);              /* restore terminal       */

void bs_input_prompt(const BString *prompt, int show_question);
void bs_input_int(int *v);
void bs_input_long(long *v);
void bs_input_float(float *v);
void bs_input_double(double *v);
void bs_input_string(BString *v);
void bs_line_input(BString *v);

/* ------------------------------------------------------------------ */
/* File I/O -- basic_file.c                                            */
/* ------------------------------------------------------------------ */
#define BSFILE_MAX 256  /* BASIC file numbers 1..255 */

/* Open modes */
#define BSFILE_MODE_INPUT   1
#define BSFILE_MODE_OUTPUT  2
#define BSFILE_MODE_APPEND  3
#define BSFILE_MODE_RANDOM  4
#define BSFILE_MODE_BINARY  5

/* Legacy aliases kept for source compatibility */
#define BSFILE_INPUT   BSFILE_MODE_INPUT
#define BSFILE_OUTPUT  BSFILE_MODE_OUTPUT
#define BSFILE_APPEND  BSFILE_MODE_APPEND
#define BSFILE_RANDOM  BSFILE_MODE_RANDOM
#define BSFILE_BINARY  BSFILE_MODE_BINARY

void bsfile_open(int filenum, const BString *path, int mode, long reclen);
void bsfile_close(int filenum);
void bsfile_close_all(void);

/* Sequential WRITE # */
void bsfile_write_int(int filenum, int v);
void bsfile_write_long(int filenum, long v);
void bsfile_write_float(int filenum, float v);
void bsfile_write_double(int filenum, double v);
void bsfile_write_string(int filenum, const BString *s);
void bsfile_write_newline(int filenum);

/* Sequential INPUT # */
void bsfile_read_int(int filenum, int *v);
void bsfile_read_long(int filenum, long *v);
void bsfile_read_float(int filenum, float *v);
void bsfile_read_double(int filenum, double *v);
void bsfile_read_string(int filenum, BString *v);
void bsfile_line_input(int filenum, BString *v);

/* Sequential PRINT # */
void bsfile_print_int(int filenum, int v);
void bsfile_print_long(int filenum, long v);
void bsfile_print_float(int filenum, float v);
void bsfile_print_double(int filenum, double v);
void bsfile_print_string(int filenum, const BString *s);
void bsfile_print_newline(int filenum);

/* Random / Binary access */
void bsfile_field_reset(int filenum);
void bsfile_field_add(int filenum, int width, BString *var);
void bsfile_get(int filenum, long recno, void *buf, int len);
void bsfile_put(int filenum, long recno, const void *buf, int len);

/* Status */
int  bsfile_eof(int filenum);
long bsfile_loc(int filenum);
long bsfile_lof(int filenum);

/* ------------------------------------------------------------------ */
/* Math -- basic_math.c                                                */
/* ------------------------------------------------------------------ */
double bs_sin(double x);
double bs_cos(double x);
double bs_tan(double x);
double bs_atn(double x);
double bs_sqr(double x);
double bs_log(double x);
double bs_exp(double x);
double bs_abs_d(double x);
int    bs_abs_i(int x);
long   bs_abs_l(long x);
double bs_int_fn(double x);    /* INT() -- floor toward -infinity     */
double bs_fix(double x);       /* FIX() -- truncate toward zero       */
double bs_rnd(int n);          /* RND(n): n>0 next, n=0 last, n<0 reseed */
void   bs_randomize(long seed); /* RANDOMIZE seed (-1 = time)         */
int    bs_sgn(double x);
int    bs_cint(double x);      /* round to nearest int                */
long   bs_clng(double x);      /* round to nearest long               */
float  bs_csng(double x);      /* cast to float                       */
double bs_cdbl(float x);       /* cast to double                      */

/* ------------------------------------------------------------------ */
/* Error handling -- basic_error.c                                     */
/* ------------------------------------------------------------------ */
typedef struct BsErrorFrame {
    jmp_buf              jmp;
    int                  code;
    int                  line;
    BString              msg;
    struct BsErrorFrame *prev;
} BsErrorFrame;

extern BsErrorFrame *bs_error_top;

/* TRY/CATCH frame stack */
void bs_error_push(BsErrorFrame *f);
void bs_error_pop(void);

/* Core raise */
void bs_raise_error(int code, int line);

/* ERR / ERL / ERROR$ accessors */
int  bs_err_code(void);
int  bs_erl_line(void);
void bs_err_msg(BString *dst);
void bs_error_str(BString *dst, int code);

/* ON ERROR GOTO -- new emitter API */
void     bs_on_error_arm(void);         /* arm after setjmp in emitted code     */
void     bs_on_error_off(void);         /* ON ERROR OFF                         */
int      bs_on_error_is_armed(void);    /* query active flag                    */
jmp_buf *bs_on_error_jmpbuf(void);      /* returns &s_on_error_jmp for setjmp   */

/* RESUME NEXT */
jmp_buf *bs_resume_next_jmpbuf(void);    /* returns &s_resume_next_buf           */
void bs_set_resume_next_jmp(jmp_buf *j); /* register resume point               */
void bs_resume_next(void);               /* jump to resume point                */

/* Legacy entry kept for source compatibility */
void bs_on_error_goto(jmp_buf *jmp);

/* ------------------------------------------------------------------ */
/* Arrays -- basic_array.c                                             */
/* ------------------------------------------------------------------ */
typedef struct {
    void *data;
    int   ndims;
    int  *lbound;
    int  *ubound;
    int   elem_size;
} BArray;

void  bsarray_alloc(BArray *a, int elem_size, int ndims,
                    const int *lbounds, const int *ubounds);
void  bsarray_free(BArray *a);
void  bsarray_realloc(BArray *a, int ndims, const int *lbounds,
                      const int *ubounds, int preserve);
void *bsarray_index(const BArray *a, const int *indices);
void  bsarray_check_bounds(const BArray *a, int dim, int idx);

#endif /* BASIC_RUNTIME_H */
