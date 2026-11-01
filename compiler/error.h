/*
 * error.h -- Compiler error and warning reporting.
 * C89 compatible.
 */

#ifndef ERROR_H
#define ERROR_H

/* Report a fatal compile error (increments error count, does not abort). */
void error_report(const char *filename, int line, int col, const char *msg);

/* Report a warning (increments warning count). */
void warn_report(const char *filename, int line, int col, const char *msg);

/* Return total error count since last reset. */
int  error_count(void);

/* Return total warning count since last reset. */
int  warn_count(void);

/* Reset counts (e.g. between compilation units). */
void error_reset(void);

#endif /* ERROR_H */
