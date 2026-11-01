/*
 * basic_shell.c -- SHELL, SHELL$, INKEY$, ERRLEVEL built-ins.
 * C89 compatible.  Target: HP-UX 11i V1 PA-RISC, gcc 4.2.
 *
 * SHELL [cmd]
 *   Run a shell command (or spawn an interactive shell if no cmd).
 *   Stores the exit code so ERRLEVEL() can retrieve it.
 *
 * SHELL$(cmd$)
 *   Run cmd$ and capture its stdout into a string.
 *
 * INKEY$
 *   Non-blocking keyboard read.  Returns a 1-char string, a
 *   multi-char escape sequence string (arrow / function keys), or ""
 *   if no key is pending.  Uses termios on HP-UX to avoid the
 *   ncurses screen model (we only need raw character reads, not a
 *   full screen manager).
 *
 * ERRLEVEL
 *   Returns the exit code of the most recent SHELL call.
 */

#if !defined(__hpux) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200112L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * POSIX / HP-UX headers.
 * Not available on the Windows authoring machine, but guarded so the
 * file parses without errors there as well.
 */
#if !defined(_WIN32) && !defined(_WIN64)
#  include <unistd.h>
#  include <termios.h>
#  include <fcntl.h>
#  include <sys/wait.h>
#  include <sys/ioctl.h>
#  include <sys/time.h>
#else
#  include <windows.h>
#  include <conio.h>
#  define popen  _popen
#  define pclose _pclose
#endif

#include "basic_runtime.h"

/* ------------------------------------------------------------------ */
/* Module state                                                        */
/* ------------------------------------------------------------------ */

static int s_errlevel = 0;          /* exit code of last SHELL call   */

#if !defined(_WIN32) && !defined(_WIN64)
static int            s_raw_active = 0;   /* 1 if raw mode is active  */
static struct termios s_saved_termios;    /* saved original settings   */
#endif

/* ------------------------------------------------------------------ */
/* Internal: raw non-blocking terminal mode for INKEY$                */
/* ------------------------------------------------------------------ */

/*
 * Put stdin into raw, non-blocking mode.
 * Safe to call multiple times -- only acts on the first call.
 */
static void enter_raw_mode(void)
{
#if !defined(_WIN32) && !defined(_WIN64)
    struct termios raw;
    int flags;

    if (s_raw_active) return;
    if (!isatty(STDIN_FILENO)) return;

    if (tcgetattr(STDIN_FILENO, &s_saved_termios) != 0) return;

    raw = s_saved_termios;
    /* Disable XON/XOFF flow control so Ctrl+S and Ctrl+Q reach the program */
    raw.c_iflag &= (unsigned long)(~(IXON));
    /* Disable canonical (line-buffered) mode and echo */
    raw.c_lflag &= (unsigned long)(~(ICANON | ECHO));
    /* Return immediately even if no character is ready */
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) return;

    /* Also set O_NONBLOCK so read() returns -1/EAGAIN when no data */
    flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags >= 0)
        (void)fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    s_raw_active = 1;
#endif
}

/*
 * Restore stdin to its original state.
 * Called from bs_runtime_cleanup() via bs_shell_cleanup().
 */
void bs_shell_cleanup(void)
{
#if !defined(_WIN32) && !defined(_WIN64)
    int flags;

    if (!s_raw_active) return;

    /* Clear O_NONBLOCK */
    flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags >= 0)
        (void)fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);

    (void)tcsetattr(STDIN_FILENO, TCSANOW, &s_saved_termios);
    s_raw_active = 0;
#endif
}

/* ------------------------------------------------------------------ */
/* SHELL [cmd]                                                         */
/* ------------------------------------------------------------------ */

/*
 * bs_shell -- execute CMD (or an interactive shell if CMD is NULL/empty).
 *
 * If INKEY$ has put stdin into raw mode we temporarily restore the
 * terminal before calling system() so the child shell sees a normal
 * tty, then re-enter raw mode afterwards.
 */
void bs_shell(const BString *cmd)
{
    int was_raw = 0;
    int rc;

#if !defined(_WIN32) && !defined(_WIN64)
    was_raw = s_raw_active;
    if (was_raw) bs_shell_cleanup(); /* restore normal tty first */
#endif

    if (!cmd || cmd->len == 0) {
        /* Spawn an interactive shell */
#if defined(_WIN32) || defined(_WIN64)
        const char *sh = getenv("COMSPEC");
        if (!sh || sh[0] == '\0') sh = "cmd.exe";
#else
        const char *sh = getenv("SHELL");
        if (!sh || sh[0] == '\0') sh = "/bin/sh";
#endif
        rc = system(sh);
    } else {
        /* cmd->data is always NUL-terminated by BString invariant */
        rc = system(cmd->data);
    }

    /*
     * Normalise the return value.
     * POSIX system() returns the wait-status; we want the exit code.
     */
#if !defined(_WIN32) && !defined(_WIN64)
    if (rc != -1 && WIFEXITED(rc))
        s_errlevel = WEXITSTATUS(rc);
    else
        s_errlevel = rc;
#else
    s_errlevel = rc;
#endif

#if !defined(_WIN32) && !defined(_WIN64)
    if (was_raw) enter_raw_mode(); /* re-enter raw mode if INKEY$ was used */
#endif
}

/* ------------------------------------------------------------------ */
/* ERRLEVEL -- exit code of the most recent SHELL call                */
/* ------------------------------------------------------------------ */

int bs_errlevel(void)
{
    return s_errlevel;
}

/* ------------------------------------------------------------------ */
/* SHELL$(cmd$) -- capture stdout of a shell command                  */
/* ------------------------------------------------------------------ */

void bs_shell_str(BString *dst, const BString *cmd)
{
    FILE *fp;
    char  line[256];
    int   was_raw = 0;

    bs_assign_cstr(dst, "");
    if (!cmd || !cmd->data || cmd->len == 0) return;

#if !defined(_WIN32) && !defined(_WIN64)
    was_raw = s_raw_active;
    if (was_raw) bs_shell_cleanup(); /* restore normal blocking tty before popen */
#endif

    /* cmd->data is NUL-terminated */
    fp = popen(cmd->data, "r");
    if (!fp) {
#if !defined(_WIN32) && !defined(_WIN64)
        if (was_raw) enter_raw_mode();
#endif
        return;
    }

    while (fgets(line, (int)sizeof(line), fp) != NULL) {
        BString chunk = {NULL, 0, 0};
        bs_assign_cstr(&chunk, line);
        bs_concat(dst, dst, &chunk);
        bs_string_free(&chunk);
    }
    pclose(fp);

#if !defined(_WIN32) && !defined(_WIN64)
    if (was_raw) enter_raw_mode(); /* re-enter raw mode if INKEY$ was active */
#endif

    /* Strip trailing newline / carriage return */
    if (dst && dst->data) {
        while (dst->len > 0 &&
               (dst->data[dst->len - 1] == '\n' || dst->data[dst->len - 1] == '\r')) {
            dst->len--;
            dst->data[dst->len] = '\0';
        }
    }
}

/*
 * Convenience wrapper returning BString by value.
 * Used in emitted C code as an expression.
 */
BString bs_shell_str_tmp(const BString *cmd)
{
    BString result = {NULL, 0, 0};
    bs_assign_cstr(&result, "");
    bs_shell_str(&result, cmd);
    return result;
}

/* ------------------------------------------------------------------ */
/* INKEY$ -- non-blocking keyboard read                                */
/* ------------------------------------------------------------------ */

/*
 * bs_inkey -- fill DST with the next pending keypress.
 *
 * Return values (QuickBASIC conventions):
 *   ""                -- no key pending
 *   single character  -- normal printable / control key
 *   NUL + char        -- extended key (arrow, function key)
 *
 * Extended key mapping (ANSI CSI sequences -> QB scan codes):
 *   Up    ESC[A  -> NUL + H (0x00 0x48)
 *   Down  ESC[B  -> NUL + P (0x00 0x50)
 *   Right ESC[C  -> NUL + M (0x00 0x4D)
 *   Left  ESC[D  -> NUL + K (0x00 0x4B)
 *   F1    ESC[11~ -> NUL + ; (0x00 0x3B)
 *   F2    ESC[12~ -> NUL + < (0x00 0x3C)
 *   ... F10 ESC[21~ -> NUL + D (0x00 0x44)
 */
/*
 * Wait up to MS milliseconds for a byte to become available on STDIN_FILENO.
 * Used after reading ESC (0x1B) so multi-byte ANSI escape sequences are not
 * split across reads on fast local PTYs (e.g. Linux xterm/tty).
 */
#if !defined(_WIN32) && !defined(_WIN64)
static int wait_stdin_ms(int ms)
{
    fd_set rfds;
    struct timeval tv;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    tv.tv_sec  = 0;
    tv.tv_usec = (long)ms * 1000L;
    return select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv) > 0 ? 1 : 0;
}
#endif

void bs_inkey(BString *dst)
{
    bs_assign_cstr(dst, "");

#if !defined(_WIN32) && !defined(_WIN64)
    {
        unsigned char buf[1];
        int           n;
        char          tmp[3];

        enter_raw_mode();

        n = (int)read(STDIN_FILENO, buf, 1);
        if (n <= 0) return; /* no key pending */

        if (buf[0] != 0x1B) {
            /* Normal character (normalize LF->CR for Enter, DEL->BS for Backspace) */
            if (buf[0] == '\n') buf[0] = '\r';
            else if (buf[0] == 0x7F) buf[0] = 0x08;
            tmp[0] = (char)buf[0];
            tmp[1] = '\0';
            bs_string_assign_cstr(dst, tmp);
            return;
        }

        /* Possible ANSI escape sequence -- peek at next byte */
        {
            unsigned char seq[1];
            int           sn = (int)read(STDIN_FILENO, seq, 1);
            if (sn <= 0 && wait_stdin_ms(20)) {
                sn = (int)read(STDIN_FILENO, seq, 1);
            }

            if (sn <= 0) {
                /* Bare ESC key */
                tmp[0] = (char)0x1B;
                tmp[1] = '\0';
                bs_string_assign_cstr(dst, tmp);
                return;
            }

            if (seq[0] == 'O') {
                /* SS3 sequence: ESC O P..S (F1..F4) or ESC O H/F (Home/End) */
                unsigned char ss3[1];
                int ssn = (int)read(STDIN_FILENO, ss3, 1);
                char sc = '\0';
                if (ssn <= 0 && wait_stdin_ms(20)) {
                    ssn = (int)read(STDIN_FILENO, ss3, 1);
                }
                if (ssn > 0) {
                    switch (ss3[0]) {
                        case 'P': sc = ';'; break; /* F1 */
                        case 'Q': sc = '<'; break; /* F2 */
                        case 'R': sc = '='; break; /* F3 */
                        case 'S': sc = '>'; break; /* F4 */
                        case 'H': sc = 'G'; break; /* Home */
                        case 'F': sc = 'O'; break; /* End */
                        case 'A': sc = 'H'; break; /* Up */
                        case 'B': sc = 'P'; break; /* Down */
                        case 'C': sc = 'M'; break; /* Right */
                        case 'D': sc = 'K'; break; /* Left */
                        default: break;
                    }
                }
                if (sc != '\0') {
                    free(dst->data);
                    dst->data    = (char *)malloc(3);
                    if (!dst->data) { dst->len = 0; dst->cap = 0; return; }
                    dst->data[0] = '\0';
                    dst->data[1] = sc;
                    dst->data[2] = '\0';
                    dst->len     = 2;
                    dst->cap     = 3;
                    return;
                }
            }

            if (seq[0] != '[') {
                /* ESC + other char -- return as two-char string */
                free(dst->data);
                dst->data    = (char *)malloc(3);
                if (!dst->data) { dst->len = 0; dst->cap = 0; return; }
                dst->data[0] = (char)0x1B;
                dst->data[1] = (char)seq[0];
                dst->data[2] = '\0';
                dst->len     = 2;
                dst->cap     = 3;
                return;
            }

            /* CSI sequence: ESC [ ... */
            {
                unsigned char csi[8];
                int           cn;
                char          scancode = '\0';
                int           i;

                cn = (int)read(STDIN_FILENO, csi, (int)sizeof(csi) - 1);
                if (cn <= 0 && wait_stdin_ms(20)) {
                    cn = (int)read(STDIN_FILENO, csi, (int)sizeof(csi) - 1);
                }
                if (cn <= 0) {
                    /* ESC [ with nothing: return bare ESC */
                    tmp[0] = (char)0x1B;
                    tmp[1] = '\0';
                    bs_string_assign_cstr(dst, tmp);
                    return;
                }
                csi[cn] = '\0';

                /* Arrow / Home / End keys: single letter final byte */
                if (cn == 1) {
                    switch (csi[0]) {
                        case 'A': scancode = 'H'; break; /* Up    (72) */
                        case 'B': scancode = 'P'; break; /* Down  (80) */
                        case 'C': scancode = 'M'; break; /* Right (77) */
                        case 'D': scancode = 'K'; break; /* Left  (75) */
                        case 'H': scancode = 'G'; break; /* Home  (71) */
                        case 'F': scancode = 'O'; break; /* End   (79) */
                        default:  break;
                    }
                }

                /* Tilde-terminated keys: digits + ~ */
                if (scancode == '\0' && cn >= 2 && csi[cn - 1] == '~') {
                    int fnum = 0;
                    for (i = 0; i < cn - 1; i++)
                        fnum = fnum * 10 + ((int)csi[i] - '0');
                    switch (fnum) {
                        case 1:
                        case 7: scancode = 'G'; break; /* Home   (71) */
                        case 2: scancode = 'R'; break; /* Insert (82) */
                        case 3: scancode = 'S'; break; /* Delete (83) */
                        case 4:
                        case 8: scancode = 'O'; break; /* End    (79) */
                        case 5: scancode = 'I'; break; /* PgUp   (73) */
                        case 6: scancode = 'Q'; break; /* PgDn   (81) */
                        default:
                            /* F1=11..F10=21 (with gap at 16) */
                            if (fnum >= 11 && fnum <= 21 && fnum != 16) {
                                int idx = (fnum <= 15) ? (fnum - 11)
                                                       : (fnum - 12);
                                scancode = (char)(';' + idx); /* F1=0x3B (59) */
                            }
                            break;
                    }
                }

                if (scancode != '\0') {
                    /* Extended key: NUL + scancode (two bytes) */
                    free(dst->data);
                    dst->data    = (char *)malloc(3);
                    if (!dst->data) { dst->len = 0; dst->cap = 0; return; }
                    dst->data[0] = '\0';
                    dst->data[1] = scancode;
                    dst->data[2] = '\0';
                    dst->len     = 2;
                    dst->cap     = 3;
                    return;
                }

                /* Unknown sequence: return raw ESC [ ... */
                {
                    int total = 2 + cn;
                    free(dst->data);
                    dst->data = (char *)malloc((size_t)(total + 1));
                    if (!dst->data) { dst->len = 0; dst->cap = 0; return; }
                    dst->data[0] = (char)0x1B;
                    dst->data[1] = '[';
                    memcpy(dst->data + 2, csi, (size_t)cn);
                    dst->data[total] = '\0';
                    dst->len = total;
                    dst->cap = total + 1;
                }
            }
        }
    }
#else
    /* Native Windows Console: use _kbhit() and _getch() */
    if (_kbhit()) {
        int ch = _getch();
        if (ch == 0 || ch == 0xE0) {
            /* Extended key: second byte is already the PC/QuickBASIC scan code */
            int sc = _getch();
            free(dst->data);
            dst->data    = (char *)malloc(3);
            if (!dst->data) { dst->len = 0; dst->cap = 0; return; }
            dst->data[0] = '\0';
            dst->data[1] = (char)sc;
            dst->data[2] = '\0';
            dst->len     = 2;
            dst->cap     = 3;
        } else {
            char tmp[2];
            if (ch == '\n') ch = '\r';
            tmp[0] = (char)ch;
            tmp[1] = '\0';
            bs_string_assign_cstr(dst, tmp);
        }
    }
#endif
}

/*
 * Convenience wrapper returning BString by value.
 */
BString bs_inkey_tmp(void)
{
    BString result = {NULL, 0, 0};
    bs_assign_cstr(&result, "");
    bs_inkey(&result);
    return result;
}

/* ------------------------------------------------------------------ */
/* TERMROWS / TERMCOLS -- query terminal dimensions                    */
/* ------------------------------------------------------------------ */

int bs_term_rows(void)
{
    const char *env;
#if defined(_WIN32) || defined(_WIN64)
    {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
            int rows = (int)(csbi.srWindow.Bottom - csbi.srWindow.Top + 1);
            if (rows > 0) return rows;
        }
    }
#elif defined(TIOCGWINSZ)
    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0)
        return (int)ws.ws_row;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0)
        return (int)ws.ws_row;
    if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0)
        return (int)ws.ws_row;
#endif
    env = getenv("LINES");
    if (env && atoi(env) > 0)
        return atoi(env);
    return 24;
}

int bs_term_cols(void)
{
    const char *env;
#if defined(_WIN32) || defined(_WIN64)
    {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
            int cols = (int)(csbi.srWindow.Right - csbi.srWindow.Left + 1);
            if (cols > 0) return cols;
        }
    }
#elif defined(TIOCGWINSZ)
    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return (int)ws.ws_col;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return (int)ws.ws_col;
    if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return (int)ws.ws_col;
#endif
    env = getenv("COLUMNS");
    if (env && atoi(env) > 0)
        return atoi(env);
    return 80;
}
