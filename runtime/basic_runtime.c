/*
 * basic_runtime.c -- Global runtime initialisation and cleanup.
 * C89 compatible.  Compile with: gcc -std=c89 -Wall
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "basic_runtime.h"

static int    s_argc = 0;
static char **s_argv = NULL;

void bs_runtime_set_args(int argc, char **argv)
{
    s_argc = argc;
    s_argv = argv;
}

void bs_command(BString *dst, int idx)
{
    int i;
    int total = 0;
    char *buf;

    if (!dst) return;
    if (s_argc <= 0 || !s_argv) {
        bs_assign_cstr(dst, "");
        return;
    }

    /* idx >= 0: return specific argv[idx] (COMMAND$(1), COMMAND$(0), etc.) */
    if (idx >= 0) {
        if (idx < s_argc && s_argv[idx]) {
            bs_assign_cstr(dst, s_argv[idx]);
        } else {
            bs_assign_cstr(dst, "");
        }
        return;
    }

    /* idx < 0: return all arguments argv[1..argc-1] joined by spaces */
    if (s_argc <= 1) {
        bs_assign_cstr(dst, "");
        return;
    }
    for (i = 1; i < s_argc; i++) {
        if (s_argv[i]) total += (int)strlen(s_argv[i]);
        if (i + 1 < s_argc) total += 1;
    }
    buf = (char *)malloc((size_t)(total + 1));
    if (!buf) {
        bs_assign_cstr(dst, "");
        return;
    }
    buf[0] = '\0';
    for (i = 1; i < s_argc; i++) {
        if (s_argv[i]) strcat(buf, s_argv[i]);
        if (i + 1 < s_argc) strcat(buf, " ");
    }
    bs_assign_cstr(dst, buf);
    free(buf);
}

/*
 * bs_runtime_init -- called once at program start.
 *   Seeds the random number generator with the current time so that
 *   RND produces different sequences on each run (unless the program
 *   calls RANDOMIZE itself).
 */
void bs_runtime_init(void)
{
    srand((unsigned int)time(NULL));
    atexit(bs_runtime_cleanup);
}

/*
 * bs_runtime_cleanup -- called once at program exit.
 *   Currently a no-op; reserved for future resource teardown
 *   (open file handles, arena allocators, etc.).
 */
void bs_runtime_cleanup(void)
{
    bs_shell_cleanup();
    bs_lprint_cleanup();
    bsfile_close_all();
}
