/*
 * basic_lprint.c -- LPRINT / LPRINT USING support for HP-UX.
 *
 * On HP-UX 11i there is no directly-accessible printer device.  Printing
 * goes through the LP spooler.  This module opens a pipe to "lp" on first
 * use, buffers all output, and submits the print job when bs_lprint_close()
 * is called (or automatically at program exit via bs_runtime_cleanup()).
 *
 * Printer selection (first match wins):
 *   1. bs_lprint_open(&name) called explicitly by the program.
 *   2. Environment variable BCCPRINTER.
 *   3. System default (invoke "lp" with no -d flag).
 *
 * C89 compatible.
 */

#if !defined(__hpux) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200112L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#if !defined(_WIN32) && !defined(_WIN64)
#  include <unistd.h>
#endif
#include "basic_runtime.h"

/* ------------------------------------------------------------------ */
/* Internal state                                                      */
/* ------------------------------------------------------------------ */

static FILE *s_lp_pipe     = NULL;  /* pipe to lp process, or NULL     */
static char  s_printer[256]= "";    /* printer name; "" = use default   */
static int   s_job_open    = 0;     /* 1 if pipe is open                */

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

/*
 * open_pipe -- open the lp pipe if not already open.
 * Called lazily by every bs_lprint_* output function.
 */
static void open_pipe(void)
{
    char cmd[320];
    const char *env_printer;

    if (s_job_open) return;

    /* Determine printer name */
    if (s_printer[0] == '\0') {
        env_printer = getenv("BCCPRINTER");
        if (env_printer && env_printer[0] != '\0') {
            strncpy(s_printer, env_printer, sizeof(s_printer) - 1);
            s_printer[sizeof(s_printer) - 1] = '\0';
        }
    }

    /* Build the lp command string */
    if (s_printer[0] != '\0') {
        /* lp -d printername - */
        strncpy(cmd, "lp -d ", sizeof(cmd) - 1);
        strncat(cmd, s_printer, sizeof(cmd) - strlen(cmd) - 4);
        strncat(cmd, " -", sizeof(cmd) - strlen(cmd) - 1);
    } else {
        /* lp - (system default printer, reads from stdin) */
        strncpy(cmd, "lp -", sizeof(cmd) - 1);
    }
    cmd[sizeof(cmd) - 1] = '\0';

    s_lp_pipe = popen(cmd, "w");
    if (s_lp_pipe) {
        s_job_open = 1;
    }
    /* If popen fails silently discard output rather than crash the program */
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

/*
 * bs_lprint_open -- set the printer destination.
 * Pass NULL to revert to default.
 * Opens a fresh pipe (closes any existing one first).
 */
void bs_lprint_open(const BString *printer_name)
{
    bs_lprint_close(); /* flush any pending job */
    if (printer_name && printer_name->data && printer_name->len > 0) {
        strncpy(s_printer, printer_name->data, sizeof(s_printer) - 1);
        s_printer[sizeof(s_printer) - 1] = '\0';
    } else {
        s_printer[0] = '\0';
    }
}

/*
 * bs_lprint_close -- flush and close the lp pipe, submitting the print job.
 */
void bs_lprint_close(void)
{
    if (s_lp_pipe) {
        pclose(s_lp_pipe);
        s_lp_pipe  = NULL;
        s_job_open = 0;
    }
}

/*
 * bs_lprint_begin -- no-op placeholder called before each LPRINT statement.
 * Reserved for future buffering strategies.
 */
void bs_lprint_begin(void)
{
    open_pipe();
}

/* ------------------------------------------------------------------ */
/* Output functions                                                    */
/* ------------------------------------------------------------------ */

void bs_lprint_int(int v)
{
    open_pipe();
    if (!s_lp_pipe) return;
    if (v >= 0) fprintf(s_lp_pipe, " %d", v);
    else        fprintf(s_lp_pipe, "%d", v);
}

void bs_lprint_long(long v)
{
    open_pipe();
    if (!s_lp_pipe) return;
    if (v >= 0) fprintf(s_lp_pipe, " %ld", v);
    else        fprintf(s_lp_pipe, "%ld", v);
}

void bs_lprint_float(float v)
{
    open_pipe();
    if (!s_lp_pipe) return;
    if (v >= 0.0f) fprintf(s_lp_pipe, " %.7g", (double)v);
    else           fprintf(s_lp_pipe, "%.7g", (double)v);
}

void bs_lprint_double(double v)
{
    open_pipe();
    if (!s_lp_pipe) return;
    if (v >= 0.0) fprintf(s_lp_pipe, " %.15g", v);
    else          fprintf(s_lp_pipe, "%.15g", v);
}

void bs_lprint_string(const BString *s)
{
    open_pipe();
    if (!s_lp_pipe) return;
    if (s && s->data) fwrite(s->data, 1, (size_t)s->len, s_lp_pipe);
}

void bs_lprint_newline(void)
{
    open_pipe();
    if (!s_lp_pipe) return;
    fputc('\n', s_lp_pipe);
}

/*
 * bs_lprint_using -- LPRINT USING fmt; args
 * Delegates to bs_print_using logic but writes to the lp pipe via a
 * temporary file redirect.  We capture stdout, write the formatted
 * output to the pipe.
 *
 * Because redirecting stdout is not thread-safe or reliable in C89,
 * we use a simpler approach: format to a string buffer via sprintf,
 * then write to the pipe.
 *
 * For now we call the existing PRINT USING format engine on a BString
 * sink.  Since bs_print_using writes to stdout, we temporarily swap
 * the lp pipe as stdout via dup2 on HP-UX (POSIX).
 */
void bs_lprint_using(const BString *fmt, ...)
{
    /*
     * Implementation note: LPRINT USING is the hardest case because
     * bs_print_using() is variadic and writes to stdout.  On HP-UX
     * (POSIX) we can redirect stdout to the pipe temporarily using
     * dup2, call bs_print_using, then restore.
     *
     * This requires <unistd.h> which is POSIX, not C89 standard, but
     * is universally available on HP-UX.
     */
#if !defined(_WIN32) && !defined(_WIN64)
    {
        int saved_stdout;
        int pipe_fd;
        va_list ap;

        open_pipe();
        if (!s_lp_pipe) return;

        fflush(s_lp_pipe);
        fflush(stdout);
        pipe_fd      = fileno(s_lp_pipe);
        saved_stdout = dup(1);     /* save stdout fd */
        dup2(pipe_fd, 1);          /* redirect stdout to pipe */

        /* Call the console PRINT USING engine — it writes to stdout
         * which is now the lp pipe. */
        va_start(ap, fmt);
        bs_vprint_using(fmt, ap);
        va_end(ap);

        fflush(stdout);
        dup2(saved_stdout, 1);     /* restore stdout */
        close(saved_stdout);
    }
#else
    /* Fallback: write the format string as-is (no argument substitution) */
    open_pipe();
    if (s_lp_pipe && fmt && fmt->data)
        fwrite(fmt->data, 1, (size_t)fmt->len, s_lp_pipe);
#endif
    (void)fmt;
}

/*
 * bs_lprint_cleanup -- called by bs_runtime_cleanup to flush any pending job.
 */
void bs_lprint_cleanup(void)
{
    bs_lprint_close();
}
