/*
 * main.c -- HP-UX BASIC Compiler (bcc) entry point.
 * C89 -- no // comments, no C99 features.
 *
 * DEVELOPMENT ENVIRONMENT NOTE:
 *   Authored on Windows. Target: HP-UX 11i, gcc 4.2, gmake.
 *   Do not attempt to build on the authoring machine.
 *   There is no gcc on the authoring machine -- do not invoke it here.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32) || defined(_WIN64)
#  include <process.h>
__declspec(dllimport) unsigned long __stdcall GetModuleFileNameA(
    void *hModule, char *lpFilename, unsigned long nSize);
#endif

#include "error.h"
#include "lexer.h"
#include "parser.h"
#include "symtab.h"
#include "typesys.h"
#include "emitter.h"

/* ------------------------------------------------------------------ */
/* Portability: POSIX dirname / basename via manual string ops.        */
/* ------------------------------------------------------------------ */

/*
 * Copy the directory portion of PATH into BUF (at most BUFLEN-1 chars).
 * Returns BUF.  If PATH has no slash, returns ".".
 */
static char *my_dirname(const char *path, char *buf, size_t buflen)
{
    const char *last_sep;
    size_t len;

    /* Accept both UNIX '/' and Windows '\' separators. */
    last_sep = NULL;
    {
        const char *p = path;
        while (*p) {
            if (*p == '/' || *p == '\\') last_sep = p;
            p++;
        }
    }

    if (!last_sep) {
        /* No separator found — directory is current directory. */
        buf[0] = '.';
        buf[1] = '\0';
        return buf;
    }

    len = (size_t)(last_sep - path);
    if (len == 0) len = 1; /* leading '/' */
    if (len >= buflen) len = buflen - 1;
    memcpy(buf, path, len);
    buf[len] = '\0';
    return buf;
}

/*
 * Copy the base file name of PATH (no directory) into BUF.
 * Returns BUF.
 */
static char *my_basename(const char *path, char *buf, size_t buflen)
{
    const char *last_sep = NULL;
    const char *p = path;
    size_t len;

    while (*p) {
        if (*p == '/' || *p == '\\') last_sep = p;
        p++;
    }

    if (last_sep) path = last_sep + 1;
    len = strlen(path);
    if (len >= buflen) len = buflen - 1;
    memcpy(buf, path, len);
    buf[len] = '\0';
    return buf;
}

/*
 * Strip the extension from STEM (modifies in place).
 * E.g. "hello.bas" -> "hello".
 */
static void strip_extension(char *stem)
{
    char *dot = strrchr(stem, '.');
    if (dot) *dot = '\0';
}

/* ------------------------------------------------------------------ */
/* Usage                                                               */
/* ------------------------------------------------------------------ */

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [options] file.bas\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -o <file>     Output binary (default: basename of input)\n");
    fprintf(stderr, "  -c            Emit C only; do not invoke gcc\n");
    fprintf(stderr, "  -v            Verbose: print pipeline steps to stderr\n");
    fprintf(stderr, "  --lex         Dump token stream and exit\n");
    fprintf(stderr, "  --parse       Dump AST and exit\n");
    fprintf(stderr, "  -I <path>     Pass -I<path> to gcc\n");
    fprintf(stderr, "  -O0/-O1/-O2   Optimisation level passed to gcc (default -O2)\n");
    fprintf(stderr, "  --help        Show this help message and exit\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Environment:\n");
    fprintf(stderr, "  BCCRUNTIME    Directory containing basic_runtime.h and libbasic.a\n");
    fprintf(stderr, "                (default: directory of the bcc binary)\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Build date: %s %s\n", __DATE__, __TIME__);
    fprintf(stderr, "For assistance contact ScottJ at westdalefarmer@gmail.com\n");
}

/* ------------------------------------------------------------------ */
/* Read whole file into a heap string                                   */
/* ------------------------------------------------------------------ */

static char *read_source_file(const char *path)
{
    FILE   *f;
    long    size;
    char   *buf;

    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "bcc: cannot open '%s'\n", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    fread(buf, 1, (size_t)size, f);
    buf[size] = '\0';
    fclose(f);
    return buf;
}

/* ------------------------------------------------------------------ */
/* Token dump (--lex)                                                  */
/* ------------------------------------------------------------------ */

static void dump_tokens(const char *filename, const char *src)
{
    Lexer *lex = lexer_new(src, filename);
    Token *tok;

    if (!lex) {
        fprintf(stderr, "bcc: out of memory\n");
        return;
    }

    for (;;) {
        tok = lexer_next(lex);
        if (!tok) break;

        if (tok->type == TOK_NEWLINE) {
            printf("%-16s %-20s  %d:%d\n",
                   token_type_name(tok->type), "\\n",
                   tok->line, tok->col);
        } else {
            printf("%-16s %-20s  %d:%d\n",
                   token_type_name(tok->type),
                   tok->text ? tok->text : "",
                   tok->line, tok->col);
        }

        if (tok->type == TOK_EOF) {
            token_free(tok);
            break;
        }
        token_free(tok);
    }

    lexer_free(lex);
}

/* ------------------------------------------------------------------ */
/* Build the path to the runtime directory                             */
/* ------------------------------------------------------------------ */

/*
 * Determine the runtime directory.
 * Priority:
 *   1. BCCRUNTIME environment variable
 *   2. Directory containing the bcc executable (argv[0])
 *
 * The result is copied into BUF (up to BUFLEN-1 chars).
 */
static void strip_trailing_sep(char *buf)
{
    size_t len = strlen(buf);
    while (len > 1 && (buf[len - 1] == '/' || buf[len - 1] == '\\')) {
        if (len == 3 && buf[1] == ':') break; /* keep "C:\" root */
        buf[--len] = '\0';
    }
}

#if defined(_WIN32) || defined(_WIN64)
static int dir_has_win_runtime(const char *dir)
{
    char path[512];
    FILE *f;
    if (!dir || !dir[0] || strlen(dir) + 16 >= sizeof(path)) return 0;
    sprintf(path, "%s\\basic.lib", dir);
    f = fopen(path, "rb");
    if (f) {
        fclose(f);
        return 1;
    }
    return 0;
}
#endif

static void get_runtime_dir(const char *argv0, char *buf, size_t buflen)
{
    const char *env = getenv("BCCRUNTIME");
    const char *p;
    int has_sep = 0;

    if (env && env[0]) {
        strncpy(buf, env, buflen - 1);
        buf[buflen - 1] = '\0';
        strip_trailing_sep(buf);
        return;
    }

#if defined(_WIN32) || defined(_WIN64)
    {
        char exe_path[512];
        char exe_dir[512];
        unsigned long n = GetModuleFileNameA(NULL, exe_path, (unsigned long)sizeof(exe_path));
        if (n > 0 && n < (unsigned long)sizeof(exe_path)) {
            my_dirname(exe_path, exe_dir, sizeof(exe_dir));
            strip_trailing_sep(exe_dir);
            if (dir_has_win_runtime(exe_dir)) {
                strncpy(buf, exe_dir, buflen - 1);
                buf[buflen - 1] = '\0';
                return;
            }
        }
        if (dir_has_win_runtime(".")) {
            strncpy(buf, ".", buflen - 1);
            buf[buflen - 1] = '\0';
            return;
        }
        if (dir_has_win_runtime("..")) {
            strncpy(buf, "..", buflen - 1);
            buf[buflen - 1] = '\0';
            return;
        }
        if (n > 0 && n < (unsigned long)sizeof(exe_path)) {
            strncpy(buf, exe_dir, buflen - 1);
            buf[buflen - 1] = '\0';
            return;
        }
    }
#endif

    if (argv0) {
        for (p = argv0; *p; p++) {
            if (*p == '/' || *p == '\\') {
                has_sep = 1;
                break;
            }
        }
    }

    if (has_sep) {
        my_dirname(argv0, buf, buflen);
        strip_trailing_sep(buf);
        return;
    }

    /* Invoked via PATH (no slash in argv0): check if ./libbasic.a (or basic.lib)
     * exists in current working directory (e.g. during build/test), otherwise
     * fall back to compiled-in BCC_PREFIX/bin. */
    {
#if defined(_WIN32) || defined(_WIN64)
        FILE *f = fopen("basic.lib", "rb");
        if (!f) f = fopen("libbasic.a", "rb");
#else
        FILE *f = fopen("libbasic.a", "rb");
#endif
        if (f) {
            fclose(f);
            strncpy(buf, ".", buflen - 1);
            buf[buflen - 1] = '\0';
            return;
        }
    }
#ifdef BCC_PREFIX
    if (strlen(BCC_PREFIX) + 5 < buflen) {
        sprintf(buf, "%s/bin", BCC_PREFIX);
        return;
    }
#endif
    my_dirname(argv0 ? argv0 : ".", buf, buflen);
}

/* ------------------------------------------------------------------ */
/* Invoke C compiler to compile the emitted C file                     */
/* ------------------------------------------------------------------ */

/*
 * Run gcc (POSIX) or cl.exe (Windows MSVC) to compile CFILE into OUTNAME.
 *
 * Returns 0 on success, non-zero on failure.
 */
#if defined(_WIN32) || defined(_WIN64)
static const char *find_vcvars64(void)
{
    static char vcvars_path[512] = "";
    static int  searched = 0;
    static const char * const candidates[] = {
        "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat",
        "C:\\Program Files\\Microsoft Visual Studio\\18\\Professional\\VC\\Auxiliary\\Build\\vcvars64.bat",
        "C:\\Program Files\\Microsoft Visual Studio\\18\\Enterprise\\VC\\Auxiliary\\Build\\vcvars64.bat",
        "C:\\Program Files\\Microsoft Visual Studio\\18\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat",
        "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat",
        "C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\VC\\Auxiliary\\Build\\vcvars64.bat",
        "C:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise\\VC\\Auxiliary\\Build\\vcvars64.bat",
        "C:\\Program Files\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat",
        NULL
    };
    int i;
    FILE *f;

    if (searched) return vcvars_path[0] ? vcvars_path : NULL;
    searched = 1;

    for (i = 0; candidates[i] != NULL; i++) {
        f = fopen(candidates[i], "rb");
        if (f) {
            fclose(f);
            strncpy(vcvars_path, candidates[i], sizeof(vcvars_path) - 1);
            vcvars_path[sizeof(vcvars_path) - 1] = '\0';
            return vcvars_path;
        }
    }

    /* Fallback: query vswhere.exe */
    f = _popen("\"C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe\" -latest -property installationPath 2>NUL", "r");
    if (f) {
        char line[384];
        if (fgets(line, (int)sizeof(line), f)) {
            size_t len = strlen(line);
            while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n'))
                line[--len] = '\0';
            if (len > 0 && len + 45 < sizeof(vcvars_path)) {
                FILE *vf;
                sprintf(vcvars_path, "%s\\VC\\Auxiliary\\Build\\vcvars64.bat", line);
                vf = fopen(vcvars_path, "rb");
                if (vf) {
                    fclose(vf);
                    _pclose(f);
                    return vcvars_path;
                }
                vcvars_path[0] = '\0';
            }
        }
        _pclose(f);
    }
    return NULL;
}
#endif

static int invoke_gcc(
        const char *cfile,
        const char *outname,
        const char *rtdir,
        const char *opt_flag,    /* e.g. "-O2" */
        const char *extra_i[],  /* array of -I paths, NULL-terminated */
        int         verbose)
{
    char cmd[4096];
    int  pos = 0;
    int  i;
    int  rc;

#define APPEND(s) \
    do { \
        size_t _n = strlen(s); \
        if (pos + (int)_n + 2 < (int)sizeof(cmd)) { \
            memcpy(cmd + pos, s, _n); \
            pos += (int)_n; \
        } \
    } while (0)

#if defined(_WIN32) || defined(_WIN64)
    {
        const char *bcc_cc = getenv("BCC_CC");
        if (!bcc_cc || strstr(bcc_cc, "gcc") == NULL) {
            /* MSVC cl.exe mode */
            char obj_file[256];
            char log_file[512];
            const char *vcvars = NULL;
            int need_vcvars = (getenv("VCToolsInstallDir") == NULL);
            if (need_vcvars) {
                vcvars = find_vcvars64();
            }
            sprintf(log_file, "%s.log", cfile);
            if (vcvars) {
                APPEND("cmd /s /c \"\"");
                APPEND(vcvars);
                APPEND("\" >NUL && ");
            }
            APPEND("cl /nologo /W0 /TC /D_CRT_SECURE_NO_WARNINGS ");
            if (strcmp(opt_flag, "-O0") == 0)
                APPEND("/Od ");
            else
                APPEND("/O2 ");
            APPEND("/Fe\"");
            APPEND(outname);
            APPEND("\" \"");
            APPEND(cfile);
            APPEND("\" /I\"");
            APPEND(rtdir);
            APPEND("\" /I\"");
            APPEND(rtdir);
            APPEND("\\runtime\" /I\"");
            APPEND(rtdir);
            APPEND("\\..\\include\"");
            if (extra_i) {
                for (i = 0; extra_i[i] != NULL; i++) {
                    APPEND(" /I\"");
                    APPEND(extra_i[i]);
                    APPEND("\"");
                }
            }
            APPEND(" /link /LIBPATH:\"");
            APPEND(rtdir);
            APPEND("\" /LIBPATH:\"");
            APPEND(rtdir);
            APPEND("\\..\\lib\" basic.lib");
            if (!verbose) {
                APPEND(" > \"");
                APPEND(log_file);
                APPEND("\" 2>&1");
            }
            if (vcvars) {
                APPEND("\"");
            }
            cmd[pos] = '\0';

            if (verbose)
                fprintf(stderr, "bcc: invoking: %s\n", cmd);

            rc = system(cmd);

            /* Clean up intermediate .obj left in current directory by cl.exe */
            my_basename(cfile, obj_file, sizeof(obj_file));
            strip_extension(obj_file);
            strncat(obj_file, ".obj", sizeof(obj_file) - strlen(obj_file) - 1);
            remove(obj_file);

            if (rc != 0) {
                if (!verbose) {
                    FILE *lf = fopen(log_file, "r");
                    if (lf) {
                        char lbuf[256];
                        while (fgets(lbuf, (int)sizeof(lbuf), lf))
                            fputs(lbuf, stderr);
                        fclose(lf);
                    }
                }
                fprintf(stderr, "bcc: cl failed (exit %d)\n", rc);
            }
            if (!verbose)
                remove(log_file);
            return rc;
        }
    }
#endif

    APPEND("gcc ");
    APPEND(opt_flag);
    APPEND(" -o ");
    APPEND(outname);
    APPEND(" ");
    APPEND(cfile);
    APPEND(" -I");
    APPEND(rtdir);
    APPEND(" -I");
    APPEND(rtdir);
    APPEND("/runtime -I");
    APPEND(rtdir);
    APPEND("/../include -L");
    APPEND(rtdir);
    APPEND(" -L");
    APPEND(rtdir);
    APPEND("/../lib -lbasic -lm");

    /* Pass through any -I paths from the command line */
    if (extra_i) {
        for (i = 0; extra_i[i] != NULL; i++) {
            APPEND(" -I");
            APPEND(extra_i[i]);
        }
    }

    cmd[pos] = '\0';
#undef APPEND

    if (verbose)
        fprintf(stderr, "bcc: invoking: %s\n", cmd);

    rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "bcc: gcc failed (exit %d)\n", rc);
    }
    return rc;
}

/* ------------------------------------------------------------------ */
/* Generate a temp file name for the emitted C                         */
/* ------------------------------------------------------------------ */

#if defined(_WIN32) || defined(_WIN64)
static int getpid_safe(void) { return (int)_getpid(); }
#else
#include <unistd.h>
static int getpid_safe(void) { return (int)getpid(); }
#endif

static void make_tmp_path(char *buf, size_t buflen)
{
    static int counter = 0;
    counter++;
    (void)buflen;
#if defined(_WIN32) || defined(_WIN64)
    {
        const char *tmpdir = getenv("TEMP");
        if (!tmpdir || !tmpdir[0]) tmpdir = getenv("TMP");
        if (!tmpdir || !tmpdir[0]) tmpdir = ".";
        sprintf(buf, "%s\\bcc_%d_%d.c", tmpdir, getpid_safe(), counter);
    }
#else
    /* C89: use sprintf; buf is 512 bytes, path cannot overflow */
    sprintf(buf, "/tmp/bcc_%d_%d.c", getpid_safe(), counter);
#endif
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    int         i;
    const char *input_file  = NULL;
    const char *output_file = NULL;  /* -o: final binary name */
    int         flag_c_only  = 0;
    int         flag_verbose = 0;
    int         flag_lex     = 0;
    int         flag_parse   = 0;
    const char *opt_level    = "-O2";

    /* Collect -I paths (up to 64) */
    const char *include_paths[65];
    int         num_includes = 0;
    include_paths[0] = NULL;

    /* ---- argument parsing ---- */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (strcmp(argv[i], "-c") == 0) {
            flag_c_only = 1;
        } else if (strcmp(argv[i], "-v") == 0) {
            flag_verbose = 1;
        } else if (strcmp(argv[i], "--lex") == 0) {
            flag_lex = 1;
        } else if (strcmp(argv[i], "--parse") == 0) {
            flag_parse = 1;
        } else if (strcmp(argv[i], "-O0") == 0 ||
                   strcmp(argv[i], "-O1") == 0 ||
                   strcmp(argv[i], "-O2") == 0) {
            opt_level = argv[i];
        } else if (strcmp(argv[i], "-I") == 0 && i + 1 < argc) {
            if (num_includes < 64)
                include_paths[num_includes++] = argv[++i];
        } else if (strncmp(argv[i], "-I", 2) == 0 && argv[i][2] != '\0') {
            if (num_includes < 64)
                include_paths[num_includes++] = argv[i] + 2;
        } else if (argv[i][0] != '-') {
            input_file = argv[i];
        } else {
            fprintf(stderr, "bcc: unknown option '%s'\n", argv[i]);
            usage(argv[0]);
            return 2;
        }
    }
    include_paths[num_includes] = NULL;

    if (input_file == NULL) {
        usage(argv[0]);
        return 2;
    }

    /* ---- --lex debug mode ---- */
    if (flag_lex) {
        char *src = read_source_file(input_file);
        if (!src) return 1;
        dump_tokens(input_file, src);
        free(src);
        return error_count() > 0 ? 1 : 0;
    }

    /* ---- --parse debug mode ---- */
    if (flag_parse) {
        char   *src;
        Lexer  *lex;
        Parser *par;
        AstNode *root;
        int had_err;

        src = read_source_file(input_file);
        if (!src) return 1;
        lex = lexer_new(src, input_file);
        if (!lex) { free(src); fprintf(stderr, "bcc: out of memory\n"); return 1; }
        par = parser_new(lex);
        if (!par) { lexer_free(lex); free(src); fprintf(stderr, "bcc: out of memory\n"); return 1; }
        root = parser_run(par);
        if (root) {
            print_ast(root, 0);
            free_ast(root);
        }
        had_err = parser_had_error(par);
        parser_free(par);
        lexer_free(lex);
        free(src);
        return (error_count() > 0 || had_err) ? 1 : 0;
    }

    /* ---- Full compilation pipeline ---- */
    {
        char    *src;
        Lexer   *lex;
        Parser  *par;
        AstNode *root;
        SymTab  *st;
        TypeSys *ts;
        FILE    *c_out;
        int      had_err;
        int      type_errors;

        /* Paths for .c output and final binary */
        char c_path[512];    /* path to emitted .c file      */
        char bin_name[256];  /* final binary name            */
        char base_buf[256];  /* scratch for basename/stem    */
        int  c_is_tmp = 0;   /* should we delete c_path?     */

        /* Determine final binary / .c output path */
        my_basename(input_file, base_buf, sizeof(base_buf));
        strip_extension(base_buf);

        if (flag_c_only) {
            /* -c: write .c next to input (or use -o if given) */
            if (output_file) {
                strncpy(c_path, output_file, sizeof(c_path) - 1);
                c_path[sizeof(c_path) - 1] = '\0';
            } else {
                sprintf(c_path, "%s.c", base_buf);  /* C89; buf is 512, stem <= 256 */
            }
            c_is_tmp = 0;
            /* bin_name not used in -c mode */
            bin_name[0] = '\0';
        } else {
            /* Normal: write .c to /tmp, compile to binary */
            make_tmp_path(c_path, sizeof(c_path));
            c_is_tmp = 1;
            if (output_file) {
                strncpy(bin_name, output_file, sizeof(bin_name) - 1);
                bin_name[sizeof(bin_name) - 1] = '\0';
            } else {
                strncpy(bin_name, base_buf, sizeof(bin_name) - 1);
                bin_name[sizeof(bin_name) - 1] = '\0';
            }
        }

        if (flag_verbose)
            fprintf(stderr, "bcc: reading '%s'\n", input_file);

        src = read_source_file(input_file);
        if (!src) return 1;

        /* Lexer */
        if (flag_verbose)
            fprintf(stderr, "bcc: lexing\n");
        lex = lexer_new(src, input_file);
        if (!lex) {
            fprintf(stderr, "bcc: out of memory\n");
            free(src);
            return 1;
        }

        /* Parser */
        if (flag_verbose)
            fprintf(stderr, "bcc: parsing\n");
        par = parser_new(lex);
        if (!par) {
            fprintf(stderr, "bcc: out of memory\n");
            lexer_free(lex);
            free(src);
            return 1;
        }

        root    = parser_run(par);
        had_err = parser_had_error(par);
        parser_free(par);
        lexer_free(lex);
        free(src);

        if (!root || had_err || error_count() > 0) {
            if (root) free_ast(root);
            return 1;
        }

        /* Type-check pass */
        if (flag_verbose)
            fprintf(stderr, "bcc: type-checking\n");

        st = symtab_new();
        if (!st) {
            fprintf(stderr, "bcc: out of memory\n");
            free_ast(root);
            return 1;
        }

        ts = typesys_new(st);
        if (!ts) {
            fprintf(stderr, "bcc: out of memory\n");
            symtab_free(st);
            free_ast(root);
            return 1;
        }

        type_errors = typesys_check(ts, root);
        if (type_errors > 0 || error_count() > 0) {
            if (flag_verbose)
                fprintf(stderr, "bcc: %d type error(s)\n", type_errors);
            typesys_free(ts);
            symtab_free(st);
            free_ast(root);
            return 1;
        }

        /* Open C output file */
        if (flag_verbose)
            fprintf(stderr, "bcc: emitting C to '%s'\n", c_path);

        c_out = fopen(c_path, "w");
        if (!c_out) {
            fprintf(stderr, "bcc: cannot create '%s'\n", c_path);
            typesys_free(ts);
            symtab_free(st);
            free_ast(root);
            return 1;
        }

        /* Emit C */
        emitter_run(root, st, c_out);
        fclose(c_out);

        typesys_free(ts);
        symtab_free(st);
        free_ast(root);

        if (error_count() > 0) {
            if (c_is_tmp) remove(c_path);
            return 1;
        }

        /* Invoke gcc (unless -c) */
        if (!flag_c_only) {
            char rtdir[512];
            int  gcc_rc;

            get_runtime_dir(argv[0], rtdir, sizeof(rtdir));

            if (flag_verbose)
                fprintf(stderr, "bcc: runtime dir '%s'\n", rtdir);

            gcc_rc = invoke_gcc(c_path, bin_name, rtdir,
                                opt_level, include_paths, flag_verbose);

            /* Remove temp .c unless verbose (keep for debugging) */
            if (c_is_tmp && !flag_verbose)
                remove(c_path);

            if (gcc_rc != 0)
                return 1;
        }
    }

    return error_count() > 0 ? 1 : 0;
}
