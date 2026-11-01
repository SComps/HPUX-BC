/*
 * basic_file.c -- File I/O (OPEN/CLOSE/READ/WRITE/GET/PUT/EOF/LOC/LOF).
 * C89 compatible.
 *
 * BASIC file numbers are 1-based (1..255).  Index 0 is unused.
 *
 * Open modes (constants defined in basic_runtime.h):
 *   BSFILE_MODE_INPUT   1   sequential read  ("r")
 *   BSFILE_MODE_OUTPUT  2   sequential write ("w")
 *   BSFILE_MODE_APPEND  3   sequential append ("a")
 *   BSFILE_MODE_RANDOM  4   fixed-length record r/w ("r+b", created if absent)
 *   BSFILE_MODE_BINARY  5   byte-level r/w  ("r+b", created if absent)
 *
 * WRITE # format (CSV):
 *   Numbers written as plain text.
 *   Strings written in double-quotes.
 *   Items separated by commas; each record ends with newline.
 *
 * INPUT # format:
 *   Parses CSV produced by WRITE #; strips quotes from strings.
 *   Numbers parsed with sscanf; trailing whitespace/comma skipped.
 *
 * PRINT # format:
 *   Numbers: same leading-space / minus-sign rule as console PRINT.
 *   No delimiter between items; newline written by bsfile_print_newline.
 *
 * GET / PUT (RANDOM / BINARY):
 *   recno is 1-based.  recno <= 0 means "use current position".
 *   For RANDOM: seek to (recno-1) * reclen before r/w.
 *   For BINARY: seek to (recno-1) bytes before r/w.
 *
 * Error code 52 = "Bad file number" (raised via bs_raise_error).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "basic_runtime.h"

/* ------------------------------------------------------------------ */
/* File table                                                           */
/* ------------------------------------------------------------------ */

#define BSFILE_MAX_FIELDS 64

typedef struct {
    BString *var;
    int      offset;
    int      width;
} BsFieldDef;

typedef struct {
    FILE      *fp;
    int        mode;      /* BSFILE_MODE_* */
    long       reclen;    /* record length for RANDOM mode */
    int        is_open;
    char      *recbuf;    /* random-access record buffer */
    BsFieldDef fields[BSFILE_MAX_FIELDS];
    int        nfields;
    int        field_offset;
} BsFileEntry;

static BsFileEntry s_files[BSFILE_MAX]; /* index 0 unused; 1..255 valid */

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

static int valid_fnum(int n)
{
    return n >= 1 && n < BSFILE_MAX;
}

/*
 * get_open_file -- validate filenum and return a pointer to the entry.
 * Calls bs_raise_error(52, 0) and returns NULL on failure.
 */
static BsFileEntry *get_open_file(int filenum)
{
    if (!valid_fnum(filenum) || !s_files[filenum].is_open) {
        bs_raise_error(52, 0); /* Bad file number */
        return NULL;
    }
    return &s_files[filenum];
}

/* ------------------------------------------------------------------ */
/* OPEN / CLOSE                                                         */
/* ------------------------------------------------------------------ */

void bsfile_open(int filenum, const BString *path, int mode, long reclen)
{
    const char *cmode;
    FILE       *fp;

    if (!valid_fnum(filenum) || !path || !path->data) {
        bs_raise_error(52, 0);
        return;
    }

    /* Close any existing file in that slot */
    if (s_files[filenum].is_open) {
        bsfile_close(filenum);
    }

    switch (mode) {
    case BSFILE_MODE_INPUT:  cmode = "r";   break;
    case BSFILE_MODE_OUTPUT: cmode = "w";   break;
    case BSFILE_MODE_APPEND: cmode = "a";   break;
    case BSFILE_MODE_RANDOM:
    case BSFILE_MODE_BINARY:
        /* Try to open for read+write; if file doesn't exist, create it */
        fp = fopen(path->data, "r+b");
        if (!fp) fp = fopen(path->data, "w+b");
        if (!fp) { bs_raise_error(53, 0); return; } /* error 53 = File not found */
        s_files[filenum].fp           = fp;
        s_files[filenum].mode         = mode;
        s_files[filenum].reclen       = (reclen > 0) ? reclen :
                                        (mode == BSFILE_MODE_RANDOM ? 128L : 1L);
        s_files[filenum].is_open      = 1;
        s_files[filenum].nfields      = 0;
        s_files[filenum].field_offset = 0;
        s_files[filenum].recbuf       = (char *)malloc((size_t)s_files[filenum].reclen);
        if (s_files[filenum].recbuf) {
            memset(s_files[filenum].recbuf, ' ', (size_t)s_files[filenum].reclen);
        }
        return;
    default:
        cmode = "r";
        break;
    }

    fp = fopen(path->data, cmode);
    if (!fp) { bs_raise_error(53, 0); return; } /* File not found / cannot open */
    s_files[filenum].fp           = fp;
    s_files[filenum].mode         = mode;
    s_files[filenum].reclen       = (reclen > 0) ? reclen : 1;
    s_files[filenum].is_open      = 1;
    s_files[filenum].nfields      = 0;
    s_files[filenum].field_offset = 0;
    s_files[filenum].recbuf       = NULL;
}

void bsfile_close(int filenum)
{
    if (filenum == 0) {
        bsfile_close_all();
        return;
    }
    if (!valid_fnum(filenum)) {
        bs_raise_error(52, 0);
        return;
    }
    if (s_files[filenum].is_open) {
        fclose(s_files[filenum].fp);
        s_files[filenum].fp           = NULL;
        s_files[filenum].is_open      = 0;
        s_files[filenum].nfields      = 0;
        s_files[filenum].field_offset = 0;
        if (s_files[filenum].recbuf) {
            free(s_files[filenum].recbuf);
            s_files[filenum].recbuf = NULL;
        }
    }
}

void bsfile_close_all(void)
{
    int i;
    for (i = 1; i < BSFILE_MAX; i++) {
        if (s_files[i].is_open) {
            bsfile_close(i);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Sequential WRITE # (CSV format)                                      */
/* ------------------------------------------------------------------ */

/*
 * bsfile_write_sep -- write comma separator if needed.
 * We use a per-file "need comma" flag stored in a parallel array.
 * To keep this C89 / no dynamic allocation, we use a second static array.
 */
static int s_write_sep[BSFILE_MAX]; /* non-zero => next item needs comma */

static void write_sep(int filenum)
{
    if (s_write_sep[filenum]) {
        fputc(',', s_files[filenum].fp);
    }
    s_write_sep[filenum] = 1;
}

void bsfile_write_int(int filenum, int v)
{
    BsFileEntry *e = get_open_file(filenum);
    if (!e) return;
    write_sep(filenum);
    fprintf(e->fp, "%d", v);
}

void bsfile_write_long(int filenum, long v)
{
    BsFileEntry *e = get_open_file(filenum);
    if (!e) return;
    write_sep(filenum);
    fprintf(e->fp, "%ld", v);
}

void bsfile_write_float(int filenum, float v)
{
    BsFileEntry *e = get_open_file(filenum);
    if (!e) return;
    write_sep(filenum);
    fprintf(e->fp, "%.7g", (double)v);
}

void bsfile_write_double(int filenum, double v)
{
    BsFileEntry *e = get_open_file(filenum);
    if (!e) return;
    write_sep(filenum);
    fprintf(e->fp, "%.15g", v);
}

void bsfile_write_string(int filenum, const BString *s)
{
    BsFileEntry *e = get_open_file(filenum);
    const char  *p;
    if (!e) return;
    write_sep(filenum);
    fputc('"', e->fp);
    if (s && s->data) {
        /* escape embedded double-quotes as "" */
        for (p = s->data; *p; p++) {
            if (*p == '"') fputc('"', e->fp);
            fputc(*p, e->fp);
        }
    }
    fputc('"', e->fp);
}

void bsfile_write_newline(int filenum)
{
    BsFileEntry *e = get_open_file(filenum);
    if (!e) return;
    fputc('\n', e->fp);
    s_write_sep[filenum] = 0; /* reset separator for next record */
}

/* ------------------------------------------------------------------ */
/* Sequential INPUT # (parse CSV)                                       */
/* ------------------------------------------------------------------ */

#define FREAD_BUF 1024

/*
 * skip_file_comma -- skip whitespace and one optional comma in fp.
 */
static void skip_file_comma(FILE *fp)
{
    int c;
    /* skip leading whitespace (but not newline) */
    do { c = fgetc(fp); } while (c == ' ' || c == '\t');
    if (c != ',') ungetc(c, fp); /* put back non-comma */
    /* skip trailing whitespace after comma */
    do { c = fgetc(fp); } while (c == ' ' || c == '\t');
    if (c != EOF) ungetc(c, fp);
}

void bsfile_read_int(int filenum, int *v)
{
    BsFileEntry *e = get_open_file(filenum);
    if (!e) return;
    skip_file_comma(e->fp);
    fscanf(e->fp, "%d", v);
}

void bsfile_read_long(int filenum, long *v)
{
    BsFileEntry *e = get_open_file(filenum);
    if (!e) return;
    skip_file_comma(e->fp);
    fscanf(e->fp, "%ld", v);
}

void bsfile_read_float(int filenum, float *v)
{
    BsFileEntry *e = get_open_file(filenum);
    if (!e) return;
    skip_file_comma(e->fp);
    fscanf(e->fp, "%f", v);
}

void bsfile_read_double(int filenum, double *v)
{
    BsFileEntry *e = get_open_file(filenum);
    if (!e) return;
    skip_file_comma(e->fp);
    fscanf(e->fp, "%lf", v);
}

/*
 * bsfile_read_string -- read a quoted or unquoted CSV token.
 * Quoted:   reads until closing '"', unescaping "" -> "
 * Unquoted: reads until comma or newline.
 */
void bsfile_read_string(int filenum, BString *v)
{
    BsFileEntry *e = get_open_file(filenum);
    char         buf[FREAD_BUF];
    int          i = 0;
    int          c;

    if (!e) return;
    skip_file_comma(e->fp);
    c = fgetc(e->fp);

    if (c == '"') {
        /* Quoted string: read until unescaped closing quote */
        while ((c = fgetc(e->fp)) != EOF && i < FREAD_BUF - 1) {
            if (c == '"') {
                int next = fgetc(e->fp);
                if (next == '"') {
                    buf[i++] = '"'; /* escaped "" -> " */
                } else {
                    if (next != EOF) ungetc(next, e->fp);
                    break; /* end of quoted string */
                }
            } else {
                if (c == '\n') { ungetc(c, e->fp); break; }
                buf[i++] = (char)c;
            }
        }
    } else {
        /* Unquoted: read until comma or newline */
        if (c != EOF && c != ',' && c != '\n') {
            buf[i++] = (char)c;
            while ((c = fgetc(e->fp)) != EOF && c != ',' && c != '\n' &&
                   i < FREAD_BUF - 1) {
                buf[i++] = (char)c;
            }
            if (c == ',') ungetc(c, e->fp); /* leave comma for next read */
        }
    }
    buf[i] = '\0';
    bs_string_assign_cstr(v, buf);
}

/*
 * bsfile_line_input -- read an entire line from filenum up to newline/EOF.
 * Strips trailing \r and \n.
 */
void bsfile_line_input(int filenum, BString *v)
{
    BsFileEntry *e = get_open_file(filenum);
    char         buf[FREAD_BUF];
    int          len;

    if (!v) return;
    if (!e || !e->fp) {
        bs_string_assign_cstr(v, "");
        return;
    }
    if (!fgets(buf, FREAD_BUF, e->fp)) {
        bs_string_assign_cstr(v, "");
        return;
    }
    len = (int)strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        buf[--len] = '\0';
    }
    bs_string_assign_cstr(v, buf);
}

/* ------------------------------------------------------------------ */
/* Sequential PRINT # (undelimited)                                     */
/* ------------------------------------------------------------------ */

void bsfile_print_int(int filenum, int v)
{
    BsFileEntry *e = get_open_file(filenum);
    if (!e) return;
    if (v >= 0) fprintf(e->fp, " %d", v);
    else        fprintf(e->fp, "%d", v);
}

void bsfile_print_long(int filenum, long v)
{
    BsFileEntry *e = get_open_file(filenum);
    if (!e) return;
    if (v >= 0) fprintf(e->fp, " %ld", v);
    else        fprintf(e->fp, "%ld", v);
}

void bsfile_print_float(int filenum, float v)
{
    BsFileEntry *e = get_open_file(filenum);
    if (!e) return;
    if (v >= 0.0f) fprintf(e->fp, " %.7g", (double)v);
    else           fprintf(e->fp, "%.7g",  (double)v);
}

void bsfile_print_double(int filenum, double v)
{
    BsFileEntry *e = get_open_file(filenum);
    if (!e) return;
    if (v >= 0.0) fprintf(e->fp, " %.15g", v);
    else          fprintf(e->fp, "%.15g",  v);
}

void bsfile_print_string(int filenum, const BString *s)
{
    BsFileEntry *e = get_open_file(filenum);
    if (!e) return;
    if (s && s->data) fwrite(s->data, 1, (size_t)s->len, e->fp);
}

void bsfile_print_newline(int filenum)
{
    BsFileEntry *e = get_open_file(filenum);
    if (!e) return;
    fputc('\n', e->fp);
}

/* ------------------------------------------------------------------ */
/* Random / Binary FIELD, GET and PUT                                   */
/* ------------------------------------------------------------------ */

void bsfile_field_reset(int filenum)
{
    BsFileEntry *e = get_open_file(filenum);
    if (!e) return;
    e->nfields      = 0;
    e->field_offset = 0;
    if (!e->recbuf && e->reclen > 0) {
        e->recbuf = (char *)malloc((size_t)e->reclen);
        if (e->recbuf) memset(e->recbuf, ' ', (size_t)e->reclen);
    }
}

void bsfile_field_add(int filenum, int width, BString *var)
{
    BsFileEntry *e = get_open_file(filenum);
    if (!e || !var) return;
    if (width < 0 || e->field_offset + width > (int)e->reclen ||
        e->nfields >= BSFILE_MAX_FIELDS) {
        bs_raise_error(50, 0); /* error 50 = Field overflow */
        return;
    }
    e->fields[e->nfields].var    = var;
    e->fields[e->nfields].offset = e->field_offset;
    e->fields[e->nfields].width  = width;
    e->nfields++;
    if (e->recbuf) {
        bs_string_assign_buf(var, e->recbuf + e->field_offset, width);
    } else {
        bs_space(var, width);
    }
    e->field_offset += width;
}

void bsfile_get(int filenum, long recno, void *buf, int len)
{
    BsFileEntry *e = get_open_file(filenum);
    int i;
    if (!e) return;
    if (recno > 0) {
        long offset = (recno - 1L) * e->reclen;
        fseek(e->fp, offset, SEEK_SET);
    }
    if (buf == NULL) {
        /* GW-BASIC FIELD mode: read record into e->recbuf and unpack fields */
        if (!e->recbuf && e->reclen > 0) {
            e->recbuf = (char *)malloc((size_t)e->reclen);
        }
        if (e->recbuf) {
            memset(e->recbuf, ' ', (size_t)e->reclen);
            fread(e->recbuf, 1, (size_t)e->reclen, e->fp);
            for (i = 0; i < e->nfields; i++) {
                if (e->fields[i].var) {
                    bs_string_assign_buf(e->fields[i].var,
                                         e->recbuf + e->fields[i].offset,
                                         e->fields[i].width);
                }
            }
        }
    } else {
        fread(buf, 1, (size_t)len, e->fp);
    }
}

void bsfile_put(int filenum, long recno, const void *buf, int len)
{
    BsFileEntry *e = get_open_file(filenum);
    int i;
    if (!e) return;
    if (recno > 0) {
        long offset = (recno - 1L) * e->reclen;
        fseek(e->fp, offset, SEEK_SET);
    }
    if (buf == NULL) {
        /* GW-BASIC FIELD mode: pack fields into e->recbuf and write record */
        if (!e->recbuf && e->reclen > 0) {
            e->recbuf = (char *)malloc((size_t)e->reclen);
        }
        if (e->recbuf) {
            memset(e->recbuf, ' ', (size_t)e->reclen);
            for (i = 0; i < e->nfields; i++) {
                BString *v = e->fields[i].var;
                int w = e->fields[i].width;
                if (v && v->data && v->len > 0 && w > 0) {
                    int n = (v->len < w) ? v->len : w;
                    memcpy(e->recbuf + e->fields[i].offset, v->data, (size_t)n);
                }
            }
            fwrite(e->recbuf, 1, (size_t)e->reclen, e->fp);
            fflush(e->fp);
        }
    } else {
        fwrite(buf, 1, (size_t)len, e->fp);
        fflush(e->fp);
    }
}

/* ------------------------------------------------------------------ */
/* Status: EOF, LOC, LOF                                                */
/* ------------------------------------------------------------------ */

/*
 * bsfile_eof -- return non-zero if at or past EOF.
 * We peek one byte; if it fails we are at EOF.  Otherwise put it back.
 */
int bsfile_eof(int filenum)
{
    BsFileEntry *e = get_open_file(filenum);
    int c;
    if (!e) return -1;
    if (feof(e->fp)) return -1;
    c = fgetc(e->fp);
    if (c == EOF) return -1;
    ungetc(c, e->fp);
    return 0;
}

/*
 * bsfile_loc -- current record position (1-based for RANDOM,
 *               byte position for other modes).
 */
long bsfile_loc(int filenum)
{
    BsFileEntry *e = get_open_file(filenum);
    long pos;
    if (!e) return 0;
    pos = ftell(e->fp);
    if (e->mode == BSFILE_MODE_RANDOM && e->reclen > 0)
        return pos / e->reclen; /* 0-based record index; BASIC adds 1 */
    return pos;
}

/*
 * bsfile_lof -- return file size in bytes.
 */
long bsfile_lof(int filenum)
{
    BsFileEntry *e = get_open_file(filenum);
    long pos, end;
    if (!e) return 0;
    pos = ftell(e->fp);
    fseek(e->fp, 0L, SEEK_END);
    end = ftell(e->fp);
    fseek(e->fp, pos, SEEK_SET);
    return end;
}
