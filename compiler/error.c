/*
 * error.c -- Compiler error/warning reporting implementation.
 * C89 compatible.
 */

#include <stdio.h>
#include "error.h"

static int s_errors   = 0;
static int s_warnings = 0;

void error_report(const char *filename, int line, int col, const char *msg)
{
    fprintf(stderr, "%s:%d:%d: error: %s\n", filename, line, col, msg);
    s_errors++;
}

void warn_report(const char *filename, int line, int col, const char *msg)
{
    fprintf(stderr, "%s:%d:%d: warning: %s\n", filename, line, col, msg);
    s_warnings++;
}

int error_count(void)   { return s_errors; }
int warn_count(void)    { return s_warnings; }
void error_reset(void)  { s_errors = 0; s_warnings = 0; }
