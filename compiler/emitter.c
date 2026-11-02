/*
 * emitter.c -- C code emitter for the HP-UX BASIC compiler.
 * Walks a type-annotated AST and writes valid C89 source to a FILE*.
 *
 * C89 compatible -- no // comments, all vars declared at top of block,
 * no C99 features.
 *
 * DEVELOPMENT ENVIRONMENT NOTE:
 *   Authored on Windows. Target: HP-UX 11i, gcc 4.2, gmake.
 *   Do not attempt to build on the authoring machine.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#include "emitter.h"
#include "token.h"

/* =========================================================================
 * Internal emitter state
 * ======================================================================== */

#define EMITTER_GOSUB_DEPTH 64

typedef struct Emitter {
    FILE           *out;        /* output stream                            */
    const SymTab   *st;         /* symbol table (may be NULL)               */
    const AstNode  *root;       /* root AST node                            */
    const ParamDef *cur_params; /* current SUB/FUNCTION parameter list      */
    int             indent_lvl; /* current indentation level (in units)    */
    int             uid;        /* monotone counter for unique label/tmp IDs */
    /* Name of the current function being emitted (for EXIT FUNCTION) */
    char            cur_func_name[256];
    int             in_function; /* 1 if emitting a FUNCTION, 0 for SUB     */
    BasicType       cur_ret_type; /* return type of current FUNCTION         */
    /* GOSUB return-label stack */
    int             gosub_stack[EMITTER_GOSUB_DEPTH];
    int             gosub_top;   /* stack pointer; 0 = empty                */
    int             typedef_idx; /* counter for TYPE_<idx> aliases          */
} Emitter;

/* =========================================================================
 * C keywords that must be prefixed to avoid collisions
 * ======================================================================== */
static const char *c_keywords[] = {
    "auto","break","case","char","const","continue","default","do",
    "double","else","enum","extern","float","for","goto","if","int",
    "long","register","return","short","signed","sizeof","static",
    "struct","switch","typedef","union","unsigned","void","volatile",
    "while", NULL
};

static int is_c_keyword(const char *name)
{
    int i;
    for (i = 0; c_keywords[i] != NULL; i++) {
        if (strcmp(name, c_keywords[i]) == 0) return 1;
    }
    return 0;
}

/* =========================================================================
 * Name-mangling helpers
 * ======================================================================== */

/*
 * Copy src lowercased into dst (dst must hold at least len bytes including NUL).
 * Strip any trailing BASIC type sigil (%, &, !, #, $).
 */
static void mangle_name(const char *src, char *dst, int len)
{
    int i = 0;
    int j = 0;
    int slen;

    if (!src || !dst || len <= 0) return;
    slen = (int)strlen(src);

    for (i = 0; i < slen && j < len - 1; i++) {
        char c = src[i];
        /* strip sigils */
        if (c == '%' || c == '!' || c == '#' || c == '$') continue;
        /* & is a sigil too — only when it is not mid-identifier (just strip it) */
        if (c == '&') continue;
        dst[j++] = (char)tolower((unsigned char)c);
    }
    dst[j] = '\0';

    /* prefix if it collides with a C keyword */
    if (is_c_keyword(dst)) {
        /* prepend _bas_ — shift right */
        int need = (int)strlen(dst) + 5 + 1; /* "_bas_" + old + NUL */
        if (need <= len) {
            memmove(dst + 5, dst, strlen(dst) + 1);
            memcpy(dst, "_bas_", 5);
        }
    }
}

/* Produce mangled variable/param name into a rotating static buffer ring. */
static const char *vname(const char *raw)
{
    static char bufs[16][256];
    static int idx = 0;
    char *buf = bufs[idx];
    idx = (idx + 1) % 16;
    mangle_name(raw, buf, 256);
    return buf;
}

/* Check if an identifier matches a STRING parameter in the current SUB/FUNCTION */
static int is_string_param(const Emitter *e, const char *raw_name)
{
    const ParamDef *p;
    char mn1[128], mn2[128];
    if (!e || !e->cur_params || !raw_name) return 0;
    mangle_name(raw_name, mn1, sizeof(mn1));
    for (p = e->cur_params; p != NULL; p = p->next) {
        if (p->type == BT_STRING) {
            mangle_name(p->name, mn2, sizeof(mn2));
            if (strcmp(mn1, mn2) == 0) return 1;
        }
    }
    return 0;
}

/* Check if an identifier matches a BYREF scalar parameter in the current SUB/FUNCTION */
static int is_byref_param(const Emitter *e, const char *raw_name)
{
    const ParamDef *p;
    char mn1[128], mn2[128];
    if (!e || !e->cur_params || !raw_name) return 0;
    mangle_name(raw_name, mn1, sizeof(mn1));
    for (p = e->cur_params; p != NULL; p = p->next) {
        if (p->is_byref && p->type != BT_STRING && !p->is_array) {
            mangle_name(p->name, mn2, sizeof(mn2));
            if (strcmp(mn1, mn2) == 0) return 1;
        }
    }
    return 0;
}

/* Produce sub_<name> into a static buffer. */
static const char *sub_name(const char *raw)
{
    static char buf[512];
    char mn[256];
    mangle_name(raw, mn, (int)sizeof(mn));
    sprintf(buf, "sub_%s", mn);
    return buf;
}

/* Produce func_<name> into a static buffer. */
static const char *func_name(const char *raw)
{
    static char buf[512];
    char mn[256];
    mangle_name(raw, mn, (int)sizeof(mn));
    sprintf(buf, "func_%s", mn);
    return buf;
}

/* Produce TYPE_<name> into a static buffer. */
static const char *type_name(const char *raw)
{
    static char buf[512];
    char mn[256];
    mangle_name(raw, mn, (int)sizeof(mn));
    sprintf(buf, "TYPE_%s", mn);
    return buf;
}

/* Produce label_<name> into a static buffer. */
static const char *lbl_name(const char *raw)
{
    static char buf[512];
    char mn[256];
    mangle_name(raw, mn, (int)sizeof(mn));
    sprintf(buf, "label_%s", mn);
    return buf;
}

/* =========================================================================
 * C type string for BasicType
 * ======================================================================== */
static const char *ctype_of(BasicType t, int type_idx, const AstNode *node)
{
    static char buf[256];
    (void)node; /* reserved for future use */
    switch (t) {
        case BT_INTEGER: return "int";
        case BT_LONG:    return "long";
        case BT_SINGLE:  return "float";
        case BT_DOUBLE:  return "double";
        case BT_STRING:  return "BString";
        case BT_VOID:    return "void";
        case BT_USER:
            sprintf(buf, "TYPE_%d", type_idx);
            return buf;
        default:
            return "int";
    }
}

/* Zero-initialiser literal for a type */
static const char *czero_of(BasicType t)
{
    switch (t) {
        case BT_INTEGER: return "0";
        case BT_LONG:    return "0L";
        case BT_SINGLE:  return "0.0f";
        case BT_DOUBLE:  return "0.0";
        case BT_STRING:  return "{NULL,0,0}";
        case BT_USER:    return "{0}";
        default:         return "0";
    }
}

/* =========================================================================
 * Low-level emit helpers
 * ======================================================================== */

static void emit_indent(Emitter *e)
{
    int i;
    for (i = 0; i < e->indent_lvl; i++) {
        fputs("    ", e->out);
    }
}

/* Emit an indented line with a trailing newline. */
static void emit_line(Emitter *e, const char *fmt, ...)
{
    va_list ap;
    emit_indent(e);
    va_start(ap, fmt);
    vfprintf(e->out, fmt, ap);
    va_end(ap);
    fputc('\n', e->out);
}

/* Open a C block: emit "{\n" and increase indent. */
static void emit_open(Emitter *e)
{
    emit_indent(e);
    fputs("{\n", e->out);
    e->indent_lvl++;
}

/* Close a C block: decrease indent and emit "}\n". */
static void emit_close(Emitter *e)
{
    e->indent_lvl--;
    emit_indent(e);
    fputs("}\n", e->out);
}

/* Generate a new unique id */
static int new_uid(Emitter *e)
{
    return ++e->uid;
}

/* =========================================================================
 * Forward declarations of mutually-recursive emitters
 * ======================================================================== */
static void emit_expr(Emitter *e, const AstNode *n);
static void emit_stmt(Emitter *e, const AstNode *n);
static void emit_block(Emitter *e, const AstNode *block);

/* =========================================================================
 * Expression emission
 * ======================================================================== */

/* Emit a string literal escaping special chars for C */
static void emit_string_literal(Emitter *e, const char *s)
{
    fputc('"', e->out);
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        if (c == '"')       fputs("\\\"", e->out);
        else if (c == '\\') fputs("\\\\", e->out);
        else if (c == '\n') fputs("\\n",  e->out);
        else if (c == '\r') fputs("\\r",  e->out);
        else if (c == '\t') fputs("\\t",  e->out);
        else if (c < 32 || c > 126)
            fprintf(e->out, "\\x%02x", c);
        else
            fputc(c, e->out);
    }
    fputc('"', e->out);
}

/* Map binary operator token to C operator string.
 * Returns NULL if the operator needs special handling. */
static const char *binop_to_c(int op, BasicType resolved)
{
    switch (op) {
        case TOK_PLUS:      return "+";
        case TOK_MINUS:     return "-";
        case TOK_STAR:      return "*";
        case TOK_SLASH:     return "/";
        case TOK_EQ:        return "==";
        case TOK_NEQ:       return "!=";
        case TOK_LT:        return "<";
        case TOK_GT:        return ">";
        case TOK_LE:        return "<=";
        case TOK_GE:        return ">=";
        case TOK_AND:
            return (resolved == BT_INTEGER || resolved == BT_LONG)
                    ? "&" : "&&";
        case TOK_OR:
            return (resolved == BT_INTEGER || resolved == BT_LONG)
                    ? "|" : "||";
        case TOK_XOR:       return "^";
        default:            return NULL;
    }
}

/* Emit a binary-op expression. */
static void emit_binop(Emitter *e, const AstNode *n)
{
    int op = n->u.binop.op;
    const AstNode *left  = n->u.binop.left;
    const AstNode *right = n->u.binop.right;
    BasicType ltype = left  ? left->resolved_type  : BT_INTEGER;
    BasicType rtype = right ? right->resolved_type : BT_INTEGER;

    /* String concatenation: & operator or + with strings */
    if (op == TOK_AMPERSAND || (op == TOK_PLUS && (ltype == BT_STRING || rtype == BT_STRING))) {
        int uid = new_uid(e);
        /* We emit a call expression form: we can't declare a temp here
         * in the middle of an expression in C89.  Use a trick: emit as a
         * compound statement expression using the GNU extension, or instead
         * pre-declare temps at function top.  For simplicity we rely on the
         * caller having pre-declared __bas_tmp_N.  We emit the concat call
         * as a void call that fills a pre-allocated static slot and then
         * use it.  Because this is tricky in pure C89 expressions, we use
         * a global ring of temps (see prologue). For now emit a call to a
         * helper that returns BString*. */
        /* Simpler approach: emit bs_concat into a per-call temp, declared
         * at the top of the enclosing block.  But we can't do that here in
         * an expression context.  Use a pre-declared array of temps and a
         * counter -- but that's not thread-safe. */
        /* Pragmatic C89 approach: use a static temporary buffer pool. */
        fprintf(e->out, "(*__bas_strcat_tmp(%d, &(", uid);
        emit_expr(e, left);
        fprintf(e->out, "), &(");
        emit_expr(e, right);
        fprintf(e->out, ")))");
        return;
    }

    /* Exponentiation */
    if (op == TOK_CARET) {
        fprintf(e->out, "pow((double)(");
        emit_expr(e, left);
        fprintf(e->out, "), (double)(");
        emit_expr(e, right);
        fprintf(e->out, "))");
        return;
    }

    /* Integer division \ */
    if (op == TOK_BACKSLASH) {
        fprintf(e->out, "((int)__bas_idiv((long)(");
        emit_expr(e, left);
        fprintf(e->out, "), (long)(");
        emit_expr(e, right);
        fprintf(e->out, "), %d))", n->line);
        return;
    }

    /* MOD */
    if (op == TOK_MOD) {
        fprintf(e->out, "((int)__bas_imod((long)(");
        emit_expr(e, left);
        fprintf(e->out, "), (long)(");
        emit_expr(e, right);
        fprintf(e->out, "), %d))", n->line);
        return;
    }

    /* Float division */
    if (op == TOK_SLASH) {
        if (n->resolved_type == BT_DOUBLE) {
            fprintf(e->out, "__bas_fdiv((double)(");
            emit_expr(e, left);
            fprintf(e->out, "), (double)(");
            emit_expr(e, right);
            fprintf(e->out, "), %d)", n->line);
        } else {
            fprintf(e->out, "((float)__bas_fdiv((double)(");
            emit_expr(e, left);
            fprintf(e->out, "), (double)(");
            emit_expr(e, right);
            fprintf(e->out, "), %d))", n->line);
        }
        return;
    }

    /* String comparison (returns BASIC boolean: -1 for true, 0 for false) */
    if (ltype == BT_STRING || rtype == BT_STRING) {
        const char *cop = NULL;
        switch (op) {
            case TOK_EQ:  cop = "=="; break;
            case TOK_NEQ: cop = "!="; break;
            case TOK_LT:  cop = "<";  break;
            case TOK_GT:  cop = ">";  break;
            case TOK_LE:  cop = "<="; break;
            case TOK_GE:  cop = ">="; break;
            default: cop = "=="; break;
        }
        fprintf(e->out, "((bs_compare(&(");
        emit_expr(e, left);
        fprintf(e->out, "), &(");
        emit_expr(e, right);
        fprintf(e->out, ")) %s 0) ? -1 : 0)", cop);
        return;
    }

    /* Numeric relational comparison (returns BASIC boolean: -1 for true, 0 for false) */
    if (op == TOK_EQ || op == TOK_NEQ || op == TOK_LT ||
        op == TOK_GT || op == TOK_LE  || op == TOK_GE) {
        const char *cop = binop_to_c(op, n->resolved_type);
        fprintf(e->out, "(((");
        emit_expr(e, left);
        fprintf(e->out, ") %s (", cop ? cop : "==");
        emit_expr(e, right);
        fprintf(e->out, ")) ? -1 : 0)");
        return;
    }

    /* Regular binary operators */
    {
        const char *cop = binop_to_c(op, n->resolved_type);
        if (cop) {
            fprintf(e->out, "((");
            emit_expr(e, left);
            fprintf(e->out, ") %s (", cop);
            emit_expr(e, right);
            fprintf(e->out, "))");
        } else {
            /* Fallback */
            fprintf(e->out, "((");
            emit_expr(e, left);
            fprintf(e->out, ") + (");
            emit_expr(e, right);
            fprintf(e->out, "))");
        }
    }
}

/* Emit a FUNC_CALL expression (built-in or user function). */
static void emit_func_call_expr(Emitter *e, const AstNode *n)
{
    const char *name = n->u.func_call.name;
    const AstNode *arg = n->u.func_call.args;
    char mn[256];
    mangle_name(name, mn, (int)sizeof(mn));

    /*
     * Array-subscript disguised as a function call (BASIC uses the same
     * syntax for both).  If the name resolves to an array variable in the
     * symbol table, emit it as a C array index rather than a function call.
     * We handle static arrays (BString/int/etc name[N]) only here; dynamic
     * BArray variables are always declared with DIM ... and should have been
     * parsed as ND_ARRAY_INDEX already.  In practice the parser always
     * produces ND_FUNC_CALL for name(expr), so we must intercept here.
     */
    if (e->st) {
        char uname[256];
        Symbol *sym;
        int i;
        /* uppercase the name for lookup */
        for (i = 0; name[i] && i < 255; i++)
            uname[i] = (char)toupper((unsigned char)name[i]);
        uname[i] = '\0';
        sym = symtab_lookup((SymTab *)e->st, uname);
        if (sym && (sym->kind == SK_ARRAY || (sym->kind == SK_VAR && sym->ndims > 0))) {
            char aname[128];
            const char *ct;
            int ndims = 0;
            const AstNode *tmp;
            for (tmp = arg; tmp != NULL; tmp = tmp->next) ndims++;
            mangle_name(name, aname, (int)sizeof(aname));
            ct = ctype_of(sym->type, sym->type_idx, n);
            fprintf(e->out, "(*(%s*)__bas_arr_idx(&__arr_%s, %d", ct, aname, ndims);
            for (tmp = arg; tmp != NULL; tmp = tmp->next) {
                fprintf(e->out, ", (int)(");
                emit_expr(e, tmp);
                fprintf(e->out, ")");
            }
            fprintf(e->out, "))");
            return;
        }
    }

    /* Built-in string / size functions */
    if (strcmp(mn, "len") == 0) {
        if (arg && arg->resolved_type != BT_STRING && arg->kind != ND_STRING_LIT) {
            fprintf(e->out, "((int)sizeof("); emit_expr(e, arg); fprintf(e->out, "))");
        } else {
            fprintf(e->out, "bs_len(&("); emit_expr(e, arg); fprintf(e->out, "))");
        }
        return;
    }
    if (strcmp(mn, "asc") == 0) {
        fprintf(e->out, "bs_asc(&("); emit_expr(e, arg); fprintf(e->out, "))");
        return;
    }
    if (strcmp(mn, "val") == 0) {
        fprintf(e->out, "bs_val(&("); emit_expr(e, arg); fprintf(e->out, "))");
        return;
    }
    if (strcmp(mn, "instr") == 0) {
        /* INSTR([start,] hay, needle) -- 2 or 3 args */
        const AstNode *a1 = arg;
        const AstNode *a2 = a1 ? a1->next : NULL;
        const AstNode *a3 = a2 ? a2->next : NULL;
        if (a3) {
            /* 3-arg form */
            fprintf(e->out, "bs_instr((int)("); emit_expr(e, a1);
            fprintf(e->out, "), &("); emit_expr(e, a2);
            fprintf(e->out, "), &("); emit_expr(e, a3);
            fprintf(e->out, "))");
        } else {
            fprintf(e->out, "bs_instr(1, &("); emit_expr(e, a1);
            fprintf(e->out, "), &("); emit_expr(e, a2);
            fprintf(e->out, "))");
        }
        return;
    }
    /* MID$ as expression: MID$(s, start[, len]) */
    if (strcmp(mn, "mid") == 0 || strcmp(mn, "mid$") == 0) {
        const AstNode *a1 = arg;
        const AstNode *a2 = a1 ? a1->next : NULL;
        const AstNode *a3 = a2 ? a2->next : NULL;
        int uid = new_uid(e);
        /* We can't easily return BString by value from an expression in C89
         * without a pre-declared temporary.  Use __bas_tmp pool. */
        fprintf(e->out, "(*__bas_mid_tmp(%d, &(", uid);
        emit_expr(e, a1);
        fprintf(e->out, "), (int)("); emit_expr(e, a2);
        fprintf(e->out, "), (int)(");
        if (a3) emit_expr(e, a3); else fprintf(e->out, "-1");
        fprintf(e->out, ")))");
        return;
    }
    if (strcmp(mn, "left") == 0 || strcmp(mn, "left$") == 0) {
        const AstNode *a1 = arg;
        const AstNode *a2 = a1 ? a1->next : NULL;
        int uid = new_uid(e);
        fprintf(e->out, "(*__bas_left_tmp(%d, &(", uid);
        emit_expr(e, a1);
        fprintf(e->out, "), (int)("); emit_expr(e, a2);
        fprintf(e->out, ")))");
        return;
    }
    if (strcmp(mn, "right") == 0 || strcmp(mn, "right$") == 0) {
        const AstNode *a1 = arg;
        const AstNode *a2 = a1 ? a1->next : NULL;
        int uid = new_uid(e);
        fprintf(e->out, "(*__bas_right_tmp(%d, &(", uid);
        emit_expr(e, a1);
        fprintf(e->out, "), (int)("); emit_expr(e, a2);
        fprintf(e->out, ")))");
        return;
    }
    if (strcmp(mn, "ucase") == 0 || strcmp(mn, "ucase$") == 0) {
        int uid = new_uid(e);
        fprintf(e->out, "(*__bas_ucase_tmp(%d, &(", uid);
        emit_expr(e, arg);
        fprintf(e->out, ")))");
        return;
    }
    if (strcmp(mn, "lcase") == 0 || strcmp(mn, "lcase$") == 0) {
        int uid = new_uid(e);
        fprintf(e->out, "(*__bas_lcase_tmp(%d, &(", uid);
        emit_expr(e, arg);
        fprintf(e->out, ")))");
        return;
    }
    if (strcmp(mn, "ltrim") == 0 || strcmp(mn, "ltrim$") == 0) {
        int uid = new_uid(e);
        fprintf(e->out, "(*__bas_ltrim_tmp(%d, &(", uid);
        emit_expr(e, arg);
        fprintf(e->out, ")))");
        return;
    }
    if (strcmp(mn, "rtrim") == 0 || strcmp(mn, "rtrim$") == 0) {
        int uid = new_uid(e);
        fprintf(e->out, "(*__bas_rtrim_tmp(%d, &(", uid);
        emit_expr(e, arg);
        fprintf(e->out, ")))");
        return;
    }
    if (strcmp(mn, "trim") == 0 || strcmp(mn, "trim$") == 0) {
        int uid = new_uid(e);
        fprintf(e->out, "(*__bas_trim_tmp(%d, &(", uid);
        emit_expr(e, arg);
        fprintf(e->out, ")))");
        return;
    }
    if (strcmp(mn, "str") == 0 || strcmp(mn, "str$") == 0) {
        int uid = new_uid(e);
        fprintf(e->out, "(*__bas_str_tmp(%d, (double)(", uid);
        emit_expr(e, arg);
        fprintf(e->out, ")))");
        return;
    }
    if (strcmp(mn, "chr") == 0 || strcmp(mn, "chr$") == 0) {
        int uid = new_uid(e);
        fprintf(e->out, "(*__bas_chr_tmp(%d, (int)(", uid);
        emit_expr(e, arg);
        fprintf(e->out, ")))");
        return;
    }
    if (strcmp(mn, "space") == 0 || strcmp(mn, "space$") == 0) {
        int uid = new_uid(e);
        fprintf(e->out, "(*__bas_space_tmp(%d, (int)(", uid);
        emit_expr(e, arg);
        fprintf(e->out, ")))");
        return;
    }
    if (strcmp(mn, "hex") == 0 || strcmp(mn, "hex$") == 0) {
        int uid = new_uid(e);
        fprintf(e->out, "(*__bas_hex_tmp(%d, (long)(", uid);
        emit_expr(e, arg);
        fprintf(e->out, ")))");
        return;
    }
    if (strcmp(mn, "oct") == 0 || strcmp(mn, "oct$") == 0) {
        int uid = new_uid(e);
        fprintf(e->out, "(*__bas_oct_tmp(%d, (long)(", uid);
        emit_expr(e, arg);
        fprintf(e->out, ")))");
        return;
    }
    if (strcmp(mn, "string") == 0 || strcmp(mn, "string$") == 0) {
        /* STRING$(n, ch) -- ch may be a char code (int) or a 1+ char STRING */
        const AstNode *a1 = arg;
        const AstNode *a2 = a1 ? a1->next : NULL;
        int uid = new_uid(e);
        fprintf(e->out, "(*__bas_string_fn_tmp(%d, (int)(", uid);
        emit_expr(e, a1);
        if (a2 && (a2->resolved_type == BT_STRING || a2->kind == ND_STRING_LIT)) {
            fprintf(e->out, "), bs_asc(&(");
            emit_expr(e, a2);
            fprintf(e->out, "))))");
        } else {
            fprintf(e->out, "), (int)(");
            emit_expr(e, a2);
            fprintf(e->out, ")))");
        }
        return;
    }

    /* Math built-ins */
    if (strcmp(mn, "sin") == 0) {
        fprintf(e->out, "bs_sin((double)("); emit_expr(e, arg); fprintf(e->out, "))");
        return;
    }
    if (strcmp(mn, "cos") == 0) {
        fprintf(e->out, "bs_cos((double)("); emit_expr(e, arg); fprintf(e->out, "))");
        return;
    }
    if (strcmp(mn, "tan") == 0) {
        fprintf(e->out, "bs_tan((double)("); emit_expr(e, arg); fprintf(e->out, "))");
        return;
    }
    if (strcmp(mn, "atn") == 0) {
        fprintf(e->out, "bs_atn((double)("); emit_expr(e, arg); fprintf(e->out, "))");
        return;
    }
    if (strcmp(mn, "sqr") == 0) {
        fprintf(e->out, "bs_sqr((double)("); emit_expr(e, arg); fprintf(e->out, "))");
        return;
    }
    if (strcmp(mn, "log") == 0) {
        fprintf(e->out, "bs_log((double)("); emit_expr(e, arg); fprintf(e->out, "))");
        return;
    }
    if (strcmp(mn, "exp") == 0) {
        fprintf(e->out, "bs_exp((double)("); emit_expr(e, arg); fprintf(e->out, "))");
        return;
    }
    if (strcmp(mn, "abs") == 0) {
        BasicType at = arg ? arg->resolved_type : BT_DOUBLE;
        if (at == BT_INTEGER) {
            fprintf(e->out, "bs_abs_i((int)("); emit_expr(e, arg); fprintf(e->out, "))");
        } else if (at == BT_LONG) {
            fprintf(e->out, "bs_abs_l((long)("); emit_expr(e, arg); fprintf(e->out, "))");
        } else {
            fprintf(e->out, "bs_abs_d((double)("); emit_expr(e, arg); fprintf(e->out, "))");
        }
        return;
    }
    if (strcmp(mn, "int") == 0 || strcmp(mn, "_bas_int") == 0) {
        fprintf(e->out, "bs_int_fn((double)("); emit_expr(e, arg); fprintf(e->out, "))");
        return;
    }
    if (strcmp(mn, "fix") == 0) {
        fprintf(e->out, "bs_fix((double)("); emit_expr(e, arg); fprintf(e->out, "))");
        return;
    }
    if (strcmp(mn, "rnd") == 0) {
        if (arg) {
            fprintf(e->out, "bs_rnd((int)("); emit_expr(e, arg); fprintf(e->out, "))");
        } else {
            fprintf(e->out, "bs_rnd(1)");
        }
        return;
    }
    if (strcmp(mn, "sgn") == 0) {
        fprintf(e->out, "bs_sgn((double)("); emit_expr(e, arg); fprintf(e->out, "))");
        return;
    }
    if (strcmp(mn, "cint") == 0) {
        fprintf(e->out, "bs_cint((double)("); emit_expr(e, arg); fprintf(e->out, "))");
        return;
    }
    if (strcmp(mn, "clng") == 0) {
        fprintf(e->out, "bs_clng((double)("); emit_expr(e, arg); fprintf(e->out, "))");
        return;
    }
    if (strcmp(mn, "csng") == 0) {
        fprintf(e->out, "bs_csng((double)("); emit_expr(e, arg); fprintf(e->out, "))");
        return;
    }
    if (strcmp(mn, "cdbl") == 0) {
        fprintf(e->out, "bs_cdbl((double)("); emit_expr(e, arg); fprintf(e->out, "))");
        return;
    }
    if (strcmp(mn, "mki") == 0 || strcmp(mn, "mki$") == 0) {
        int uid = new_uid(e);
        fprintf(e->out, "(*__bas_mki_tmp(%d, (int)(", uid);
        emit_expr(e, arg);
        fprintf(e->out, ")))");
        return;
    }
    if (strcmp(mn, "mkl") == 0 || strcmp(mn, "mkl$") == 0) {
        int uid = new_uid(e);
        fprintf(e->out, "(*__bas_mkl_tmp(%d, (long)(", uid);
        emit_expr(e, arg);
        fprintf(e->out, ")))");
        return;
    }
    if (strcmp(mn, "mks") == 0 || strcmp(mn, "mks$") == 0) {
        int uid = new_uid(e);
        fprintf(e->out, "(*__bas_mks_tmp(%d, (float)(", uid);
        emit_expr(e, arg);
        fprintf(e->out, ")))");
        return;
    }
    if (strcmp(mn, "mkd") == 0 || strcmp(mn, "mkd$") == 0) {
        int uid = new_uid(e);
        fprintf(e->out, "(*__bas_mkd_tmp(%d, (double)(", uid);
        emit_expr(e, arg);
        fprintf(e->out, ")))");
        return;
    }
    if (strcmp(mn, "cvi") == 0) {
        fprintf(e->out, "bs_cvi(&("); emit_expr(e, arg); fprintf(e->out, "))");
        return;
    }
    if (strcmp(mn, "cvl") == 0) {
        fprintf(e->out, "bs_cvl(&("); emit_expr(e, arg); fprintf(e->out, "))");
        return;
    }
    if (strcmp(mn, "cvs") == 0) {
        fprintf(e->out, "bs_cvs(&("); emit_expr(e, arg); fprintf(e->out, "))");
        return;
    }
    if (strcmp(mn, "cvd") == 0) {
        fprintf(e->out, "bs_cvd(&("); emit_expr(e, arg); fprintf(e->out, "))");
        return;
    }

    /* EOF() built-in */
    if (strcmp(mn, "eof") == 0) {
        fprintf(e->out, "bsfile_eof((int)("); emit_expr(e, arg); fprintf(e->out, "))");
        return;
    }
    if (strcmp(mn, "loc") == 0) {
        fprintf(e->out, "bsfile_loc((int)("); emit_expr(e, arg); fprintf(e->out, "))");
        return;
    }
    if (strcmp(mn, "lof") == 0) {
        fprintf(e->out, "bsfile_lof((int)("); emit_expr(e, arg); fprintf(e->out, "))");
        return;
    }

    /* ERR / ERL / ERROR$ */
    if (strcmp(mn, "err") == 0) {
        fprintf(e->out, "bs_err_code()");
        return;
    }
    if (strcmp(mn, "erl") == 0) {
        fprintf(e->out, "bs_erl_line()");
        return;
    }
    if (strcmp(mn, "error") == 0 || strcmp(mn, "error$") == 0) {
        int uid = new_uid(e);
        fprintf(e->out, "(*__bas_error_str_tmp(%d, ", uid);
        if (arg) {
            fprintf(e->out, "(int)(");
            emit_expr(e, arg);
            fprintf(e->out, ")");
        } else {
            fprintf(e->out, "bs_err_code()");
        }
        fprintf(e->out, "))");
        return;
    }

    /* ERRLEVEL -- exit code of last SHELL */
    if (strcmp(mn, "errlevel") == 0) {
        fprintf(e->out, "bs_errlevel()");
        return;
    }
    if (strcmp(mn, "termrows") == 0) {
        fprintf(e->out, "bs_term_rows()");
        return;
    }
    if (strcmp(mn, "termcols") == 0) {
        fprintf(e->out, "bs_term_cols()");
        return;
    }

    /* SHELL$ -- capture stdout of shell command */
    if (strcmp(mn, "shell") == 0 || strcmp(mn, "shell$") == 0) {
        int uid = new_uid(e);
        fprintf(e->out, "(*__bas_shell_tmp(%d, &(", uid);
        emit_expr(e, arg);
        fprintf(e->out, ")))");
        return;
    }

    /* INKEY$ -- non-blocking keyboard read */
    if (strcmp(mn, "inkey") == 0 || strcmp(mn, "inkey$") == 0) {
        int uid = new_uid(e);
        fprintf(e->out, "(*__bas_inkey_tmp(%d))", uid);
        return;
    }

    /* COMMAND$ -- command-line arguments */
    if (strcmp(mn, "command") == 0 || strcmp(mn, "command$") == 0) {
        int uid = new_uid(e);
        fprintf(e->out, "(*__bas_command_tmp(%d, ", uid);
        if (arg) {
            fprintf(e->out, "(int)(");
            emit_expr(e, arg);
            fprintf(e->out, ")");
        } else {
            fprintf(e->out, "-1");
        }
        fprintf(e->out, "))");
        return;
    }

    /* User-defined function call */
    {
        const AstNode *a;
        Symbol *fn_sym = NULL;
        const ParamDef *param = NULL;
        int is_str;
        int ret_is_str = (n->resolved_type == BT_STRING);
        if (e->st) {
            char uname[256];
            int i;
            for (i = 0; name[i] && i < 255; i++)
                uname[i] = (char)toupper((unsigned char)name[i]);
            uname[i] = '\0';
            fn_sym = symtab_lookup((SymTab *)e->st, uname);
            if (fn_sym) {
                param = fn_sym->params;
                if (fn_sym->ret_type == BT_STRING) ret_is_str = 1;
            }
        }

        if (ret_is_str) {
            int uid = new_uid(e);
            fprintf(e->out, "(*(%s(&__bas_strpool[%d %% __BAS_STRPOOL]",
                    func_name(name), uid);
            for (a = arg; a != NULL; a = a->next) {
                fprintf(e->out, ", ");
                is_str = (a->resolved_type == BT_STRING) ||
                         (param && param->type == BT_STRING) ||
                         (a->kind == ND_STRING_LIT);
                if (param && param->is_array && a->kind == ND_VAR) {
                    fprintf(e->out, "&__arr_%s", vname(a->u.var.name));
                } else if (is_str || (param && param->is_byref)) {
                    fprintf(e->out, "&(");
                    emit_expr(e, a);
                    fprintf(e->out, ")");
                } else {
                    emit_expr(e, a);
                }
                if (param) param = param->next;
            }
            fprintf(e->out, "), &__bas_strpool[%d %% __BAS_STRPOOL]))", uid);
        } else {
            fprintf(e->out, "%s(", func_name(name));
            for (a = arg; a != NULL; a = a->next) {
                if (a != arg) fprintf(e->out, ", ");
                is_str = (a->resolved_type == BT_STRING) ||
                         (param && param->type == BT_STRING) ||
                         (a->kind == ND_STRING_LIT);
                if (param && param->is_array && a->kind == ND_VAR) {
                    fprintf(e->out, "&__arr_%s", vname(a->u.var.name));
                } else if (is_str || (param && param->is_byref)) {
                    fprintf(e->out, "&(");
                    emit_expr(e, a);
                    fprintf(e->out, ")");
                } else {
                    emit_expr(e, a);
                }
                if (param) param = param->next;
            }
            fprintf(e->out, ")");
        }
    }
}

/* Main expression emitter */
static void emit_expr(Emitter *e, const AstNode *n)
{
    if (!n) { fprintf(e->out, "0"); return; }

    switch (n->kind) {
        case ND_INT_LIT:
            fprintf(e->out, "%d", n->u.int_lit.ival);
            break;

        case ND_LONG_LIT:
            fprintf(e->out, "%ldL", n->u.long_lit.lval);
            break;

        case ND_FLOAT_LIT:
            fprintf(e->out, "((float)%.9g)", (double)n->u.float_lit.fval);
            break;

        case ND_DOUBLE_LIT:
            fprintf(e->out, "((double)%.17g)", n->u.double_lit.dval);
            break;

        case ND_STRING_LIT:
            /* String literals must be passed as BString.
             * We use a helper __bas_strlit(uid, "...") that returns BString* */
            {
                int uid = new_uid(e);
                fprintf(e->out, "(*__bas_strlit(%d, ", uid);
                emit_string_literal(e, n->u.string_lit.sval);
                fprintf(e->out, "))");
            }
            break;

        case ND_VAR:
            {
                char vmn[128];
                mangle_name(n->u.var.name, vmn, (int)sizeof(vmn));
                if (strcmp(vmn, "inkey") == 0) {
                    int uid = new_uid(e);
                    fprintf(e->out, "(*__bas_inkey_tmp(%d))", uid);
                } else if (strcmp(vmn, "command") == 0) {
                    int uid = new_uid(e);
                    fprintf(e->out, "(*__bas_command_tmp(%d, -1))", uid);
                } else if (strcmp(vmn, "error") == 0) {
                    int uid = new_uid(e);
                    fprintf(e->out, "(*__bas_error_str_tmp(%d, bs_err_code()))", uid);
                } else if (strcmp(vmn, "rnd") == 0) {
                    fprintf(e->out, "bs_rnd(1)");
                } else if (strcmp(vmn, "errlevel") == 0) {
                    fprintf(e->out, "bs_errlevel()");
                } else if (strcmp(vmn, "termrows") == 0) {
                    fprintf(e->out, "bs_term_rows()");
                } else if (strcmp(vmn, "termcols") == 0) {
                    fprintf(e->out, "bs_term_cols()");
                } else if (strcmp(vmn, "err") == 0) {
                    fprintf(e->out, "bs_err_code()");
                } else if (strcmp(vmn, "erl") == 0) {
                    fprintf(e->out, "bs_erl_line()");
                } else if (e->in_function && strcmp(vmn, e->cur_func_name) == 0) {
                    fprintf(e->out, "%s_retval", e->cur_func_name);
                } else if (is_string_param(e, n->u.var.name) || is_byref_param(e, n->u.var.name)) {
                    fprintf(e->out, "(*%s)", vname(n->u.var.name));
                } else {
                    fprintf(e->out, "%s", vname(n->u.var.name));
                }
            }
            break;

        case ND_BINOP:
            emit_binop(e, n);
            break;

        case ND_UNOP:
            {
                int op = n->u.binop.op;
                const AstNode *operand = n->u.binop.right
                    ? n->u.binop.right : n->u.binop.left;
                if (op == TOK_MINUS) {
                    fprintf(e->out, "-(");
                    emit_expr(e, operand);
                    fprintf(e->out, ")");
                } else if (op == TOK_NOT) {
                    /* Boolean NOT or bitwise NOT depending on context */
                    if (n->resolved_type == BT_INTEGER
                        || n->resolved_type == BT_LONG) {
                        fprintf(e->out, "~(");
                    } else {
                        fprintf(e->out, "!(");
                    }
                    emit_expr(e, operand);
                    fprintf(e->out, ")");
                } else if (op == TOK_PLUS) {
                    fprintf(e->out, "+(");
                    emit_expr(e, operand);
                    fprintf(e->out, ")");
                } else {
                    emit_expr(e, operand);
                }
            }
            break;

        case ND_CAST:
            fprintf(e->out, "(%s)(", ctype_of(n->u.cast.to_type, 0, n));
            emit_expr(e, n->u.cast.expr);
            fprintf(e->out, ")");
            break;

        case ND_ARRAY_INDEX:
            {
                /* Emit as: *((CType*)bsarray_index(&__arr_name, __idx)) */
                const AstNode *arr = n->u.array_index.array;
                const AstNode *idx = n->u.array_index.indices;
                int ndims = 0;
                const AstNode *tmp;
                char aname[128];
                const char *ct;

                /* count indices */
                for (tmp = idx; tmp; tmp = tmp->next) ndims++;

                if (arr && arr->kind == ND_VAR) {
                    mangle_name(arr->u.var.name, aname, (int)sizeof(aname));
                } else {
                    strcpy(aname, "__arr_unknown");
                }
                ct = ctype_of(n->resolved_type, n->type_idx, n);

                fprintf(e->out, "(*(%s*)__bas_arr_idx(&__arr_%s, %d",
                        ct, aname, ndims);
                for (tmp = idx; tmp; tmp = tmp->next) {
                    fprintf(e->out, ", (int)(");
                    emit_expr(e, tmp);
                    fprintf(e->out, ")");
                }
                fprintf(e->out, "))");
            }
            break;

        case ND_FIELD_ACCESS:
            {
                emit_expr(e, n->u.field_access.record);
                fprintf(e->out, ".%s", vname(n->u.field_access.field));
            }
            break;

        case ND_FUNC_CALL:
            emit_func_call_expr(e, n);
            break;

        default:
            /* Fallback: just emit 0 */
            fprintf(e->out, "0 /*unsupported expr kind %d*/", (int)n->kind);
            break;
    }
}

/* =========================================================================
 * Statement emitters
 * ======================================================================== */

/* Emit a PRINT statement */
static void emit_print(Emitter *e, const AstNode *n)
{
    const AstNode *item;
    int has_trailing_sep = 0;
    int to_file = (n->u.print.to_file && n->u.print.filenum != NULL);

    /* PRINT #filenum */
    if (to_file) {
        for (item = n->u.print.items; item != NULL; item = item->next) {
            if (item->kind == ND_INT_LIT && item->type_idx == -1) {
                has_trailing_sep = (item->next == NULL) ? 1 : 0;
                continue;
            }
            has_trailing_sep = 0;
            emit_indent(e);
            switch (item->resolved_type) {
                case BT_INTEGER:
                    fprintf(e->out, "bsfile_print_int((int)(");
                    emit_expr(e, n->u.print.filenum);
                    fprintf(e->out, "), (int)(");
                    emit_expr(e, item);
                    fprintf(e->out, "));\n");
                    break;
                case BT_LONG:
                    fprintf(e->out, "bsfile_print_long((int)(");
                    emit_expr(e, n->u.print.filenum);
                    fprintf(e->out, "), (long)(");
                    emit_expr(e, item);
                    fprintf(e->out, "));\n");
                    break;
                case BT_SINGLE:
                    fprintf(e->out, "bsfile_print_float((int)(");
                    emit_expr(e, n->u.print.filenum);
                    fprintf(e->out, "), (float)(");
                    emit_expr(e, item);
                    fprintf(e->out, "));\n");
                    break;
                case BT_DOUBLE:
                    fprintf(e->out, "bsfile_print_double((int)(");
                    emit_expr(e, n->u.print.filenum);
                    fprintf(e->out, "), (double)(");
                    emit_expr(e, item);
                    fprintf(e->out, "));\n");
                    break;
                case BT_STRING:
                    fprintf(e->out, "bsfile_print_string((int)(");
                    emit_expr(e, n->u.print.filenum);
                    fprintf(e->out, "), &(");
                    emit_expr(e, item);
                    fprintf(e->out, "));\n");
                    break;
                default:
                    fprintf(e->out, "bsfile_print_int((int)(");
                    emit_expr(e, n->u.print.filenum);
                    fprintf(e->out, "), (int)(");
                    emit_expr(e, item);
                    fprintf(e->out, "));\n");
                    break;
            }
        }
        if (!has_trailing_sep) {
            emit_indent(e);
            fprintf(e->out, "bsfile_print_newline((int)(");
            emit_expr(e, n->u.print.filenum);
            fprintf(e->out, "));\n");
        }
        return;
    }

    /* PRINT USING */
    if (n->u.print.fmt) {
        int argc = 0;
        for (item = n->u.print.items; item != NULL; item = item->next) {
            if (item->kind == ND_INT_LIT && item->type_idx == -1) {
                has_trailing_sep = (item->next == NULL) ? 1 : 0;
                continue;
            }
            has_trailing_sep = 0;
            argc++;
        }
        emit_indent(e);
        fprintf(e->out, "bs_print_using(&(");
        emit_expr(e, n->u.print.fmt);
        fprintf(e->out, "), %d", argc);
        for (item = n->u.print.items; item != NULL; item = item->next) {
            /* skip separator tokens (ND_INT_LIT with type_idx == -1) */
            if (item->kind == ND_INT_LIT && item->type_idx == -1) {
                continue;
            }
            if (item->resolved_type == BT_STRING || item->kind == ND_STRING_LIT) {
                fprintf(e->out, ", BSPU_STR, &(");
                emit_expr(e, item);
                fprintf(e->out, ")");
            } else {
                fprintf(e->out, ", BSPU_NUM, (double)(");
                emit_expr(e, item);
                fprintf(e->out, ")");
            }
        }
        fprintf(e->out, ");\n");
        if (!has_trailing_sep) {
            emit_line(e, "bs_print_newline();");
        }
        return;
    }

    /* Regular PRINT */
    for (item = n->u.print.items; item != NULL; item = item->next) {
        /* Separator tokens: ND_INT_LIT with type_idx=-1 and ival=0 (;) or 1 (,) */
        if (item->kind == ND_INT_LIT && item->type_idx == -1) {
            if (item->u.int_lit.ival == 0) {
                /* semicolon: suppress newline — just mark */
                has_trailing_sep = (item->next == NULL) ? 1 : 0;
                if (item->next == NULL) {
                    /* trailing semicolon — suppress newline */
                }
            } else {
                /* comma: tab stop */
                emit_indent(e);
                fprintf(e->out, "bs_print_tab(0);\n");
                has_trailing_sep = (item->next == NULL) ? 1 : 0;
            }
            continue;
        }
        has_trailing_sep = 0;

        emit_indent(e);
        switch (item->resolved_type) {
            case BT_INTEGER:
                fprintf(e->out, "bs_print_int((int)(");
                emit_expr(e, item);
                fprintf(e->out, "));\n");
                break;
            case BT_LONG:
                fprintf(e->out, "bs_print_long((long)(");
                emit_expr(e, item);
                fprintf(e->out, "));\n");
                break;
            case BT_SINGLE:
                fprintf(e->out, "bs_print_float((float)(");
                emit_expr(e, item);
                fprintf(e->out, "));\n");
                break;
            case BT_DOUBLE:
                fprintf(e->out, "bs_print_double((double)(");
                emit_expr(e, item);
                fprintf(e->out, "));\n");
                break;
            case BT_STRING:
                fprintf(e->out, "bs_print_string(&(");
                emit_expr(e, item);
                fprintf(e->out, "));\n");
                break;
            default:
                fprintf(e->out, "bs_print_int((int)(");
                emit_expr(e, item);
                fprintf(e->out, "));\n");
                break;
        }
    }
    if (!has_trailing_sep) {
        emit_line(e, "bs_print_newline();");
    }
}

/* Emit INPUT statement */
static void emit_input(Emitter *e, const AstNode *n)
{
    const AstNode *var;

    if (n->u.input.from_file && n->u.input.filenum) {
        for (var = n->u.input.vars; var != NULL; var = var->next) {
            const char *vn = vname(var->kind == ND_VAR ? var->u.var.name : "unknown");
            emit_indent(e);
            switch (var->resolved_type) {
                case BT_INTEGER:
                    fprintf(e->out, "bsfile_read_int((int)(");
                    emit_expr(e, n->u.input.filenum);
                    fprintf(e->out, "), &%s);\n", vn);
                    break;
                case BT_LONG:
                    fprintf(e->out, "bsfile_read_long((int)(");
                    emit_expr(e, n->u.input.filenum);
                    fprintf(e->out, "), &%s);\n", vn);
                    break;
                case BT_SINGLE:
                    fprintf(e->out, "bsfile_read_float((int)(");
                    emit_expr(e, n->u.input.filenum);
                    fprintf(e->out, "), &%s);\n", vn);
                    break;
                case BT_DOUBLE:
                    fprintf(e->out, "bsfile_read_double((int)(");
                    emit_expr(e, n->u.input.filenum);
                    fprintf(e->out, "), &%s);\n", vn);
                    break;
                case BT_STRING:
                    fprintf(e->out, "bsfile_read_string((int)(");
                    emit_expr(e, n->u.input.filenum);
                    fprintf(e->out, "), &%s);\n", vn);
                    break;
                default:
                    fprintf(e->out, "bsfile_read_int((int)(");
                    emit_expr(e, n->u.input.filenum);
                    fprintf(e->out, "), &%s);\n", vn);
                    break;
            }
        }
        return;
    }

    /* Always call bs_input_prompt to read a line and show the prompt.
     * With no prompt string pass NULL; show_question is always 1 for bare
     * INPUT (QB/HP-UX convention: bare INPUT prints "? ").
     * With a prompt string, show_question follows the separator:
     *   semicolon => 1 (print "? "), comma => 0 (no "? "). */
    emit_indent(e);
    if (n->u.input.prompt) {
        fprintf(e->out, "bs_input_prompt(&(");
        emit_expr(e, n->u.input.prompt);
        fprintf(e->out, "), %d);\n", n->u.input.prompt_semi);
    } else {
        fprintf(e->out, "bs_input_prompt(NULL, 1);\n");
    }

    for (var = n->u.input.vars; var != NULL; var = var->next) {
        emit_indent(e);
        switch (var->resolved_type) {
            case BT_INTEGER:
                fprintf(e->out, "bs_input_int(&%s);\n", vname(
                    var->kind == ND_VAR ? var->u.var.name : "unknown"));
                break;
            case BT_LONG:
                fprintf(e->out, "bs_input_long(&%s);\n", vname(
                    var->kind == ND_VAR ? var->u.var.name : "unknown"));
                break;
            case BT_DOUBLE:
            case BT_SINGLE:
                fprintf(e->out, "bs_input_double(&%s);\n", vname(
                    var->kind == ND_VAR ? var->u.var.name : "unknown"));
                break;
            case BT_STRING:
                fprintf(e->out, "bs_input_string(&%s);\n", vname(
                    var->kind == ND_VAR ? var->u.var.name : "unknown"));
                break;
            default:
                fprintf(e->out, "bs_input_int(&%s);\n", vname(
                    var->kind == ND_VAR ? var->u.var.name : "unknown"));
                break;
        }
    }
}

/* Emit LINE INPUT statement */
static void emit_line_input(Emitter *e, const AstNode *n)
{
    const AstNode *var = n->u.input.vars;

    if (n->u.input.from_file && n->u.input.filenum) {
        if (var) {
            emit_indent(e);
            fprintf(e->out, "bsfile_line_input((int)(");
            emit_expr(e, n->u.input.filenum);
            fprintf(e->out, "), &%s);\n", vname(
                var->kind == ND_VAR ? var->u.var.name : "unknown"));
        }
        return;
    }

    /* LINE INPUT with no prompt: no "? " (reads silently).
     * LINE INPUT with prompt: honour the separator (same rule as INPUT). */
    emit_indent(e);
    if (n->u.input.prompt) {
        fprintf(e->out, "bs_input_prompt(&(");
        emit_expr(e, n->u.input.prompt);
        fprintf(e->out, "), %d);\n", n->u.input.prompt_semi);
    } else {
        fprintf(e->out, "bs_input_prompt(NULL, 0);\n");
    }
    if (var) {
        emit_indent(e);
        fprintf(e->out, "bs_line_input(&%s);\n", vname(
            var->kind == ND_VAR ? var->u.var.name : "unknown"));
    }
}

/* Emit FOR loop */
static void emit_for(Emitter *e, const AstNode *n)
{
    int uid = new_uid(e);
    char var[128];
    BasicType vt;
    const char *ct;

    mangle_name(n->u.for_stmt.var, var, (int)sizeof(var));
    /* Determine type of loop variable */
    vt = n->u.for_stmt.init
        ? n->u.for_stmt.init->resolved_type : BT_INTEGER;
    ct = ctype_of(vt, 0, NULL);

    emit_indent(e); fprintf(e->out, "{\n");
    e->indent_lvl++;
    emit_indent(e); fprintf(e->out, "%s __for_limit_%d = (", ct, uid);
    emit_expr(e, n->u.for_stmt.limit);
    fprintf(e->out, ");\n");

    emit_indent(e); fprintf(e->out, "%s __for_step_%d = (", ct, uid);
    if (n->u.for_stmt.step) {
        emit_expr(e, n->u.for_stmt.step);
    } else {
        fprintf(e->out, "1");
    }
    fprintf(e->out, ");\n");

    emit_indent(e);
    fprintf(e->out, "for (%s = (", var);
    emit_expr(e, n->u.for_stmt.init);
    fprintf(e->out, "); (__for_step_%d >= 0) ? "
            "(%s <= __for_limit_%d) : (%s >= __for_limit_%d); "
            "%s += __for_step_%d)\n",
            uid, var, uid, var, uid, var, uid);

    emit_indent(e); fprintf(e->out, "{\n");
    e->indent_lvl++;
    emit_block(e, n->u.for_stmt.body);
    e->indent_lvl--;
    emit_indent(e); fprintf(e->out, "}\n");
    e->indent_lvl--;
    emit_indent(e); fprintf(e->out, "}\n");
}

/* Emit DO loop */
static void emit_do(Emitter *e, const AstNode *n)
{
    int is_until    = n->u.do_stmt.is_until;
    int top_checked = n->u.do_stmt.check_at_top;
    const AstNode *cond = n->u.do_stmt.cond;

    if (!cond) {
        /* DO ... LOOP — infinite loop */
        emit_line(e, "while (1)");
        emit_open(e);
        emit_block(e, n->u.do_stmt.body);
        emit_close(e);
        return;
    }

    if (top_checked) {
        emit_indent(e);
        if (is_until) {
            fprintf(e->out, "while (!(");
            emit_expr(e, cond);
            fprintf(e->out, "))\n");
        } else {
            fprintf(e->out, "while ((");
            emit_expr(e, cond);
            fprintf(e->out, "))\n");
        }
        emit_open(e);
        emit_block(e, n->u.do_stmt.body);
        emit_close(e);
    } else {
        emit_line(e, "do");
        emit_open(e);
        emit_block(e, n->u.do_stmt.body);
        emit_close(e);
        emit_indent(e);
        if (is_until) {
            fprintf(e->out, "while (!(");
            emit_expr(e, cond);
            fprintf(e->out, "));\n");
        } else {
            fprintf(e->out, "while ((");
            emit_expr(e, cond);
            fprintf(e->out, "));\n");
        }
    }
}

/* Emit IF statement */
static void emit_if(Emitter *e, const AstNode *n)
{
    const AstNode *elif;

    emit_indent(e);
    fprintf(e->out, "if ((");
    emit_expr(e, n->u.if_stmt.cond);
    fprintf(e->out, "))\n");
    emit_open(e);
    emit_block(e, n->u.if_stmt.then_block);
    emit_close(e);

    for (elif = n->u.if_stmt.elseif_chain; elif != NULL; elif = elif->next) {
        emit_indent(e);
        fprintf(e->out, "else if ((");
        emit_expr(e, elif->u.if_stmt.cond);
        fprintf(e->out, "))\n");
        emit_open(e);
        emit_block(e, elif->u.if_stmt.then_block);
        emit_close(e);
    }

    if (n->u.if_stmt.else_block) {
        emit_line(e, "else");
        emit_open(e);
        emit_block(e, n->u.if_stmt.else_block);
        emit_close(e);
    }
}

/* Emit SELECT CASE */
static void emit_select(Emitter *e, const AstNode *n)
{
    int uid = new_uid(e);
    const AstNode *cases = n->u.select_stmt.cases;
    const AstNode *c;
    BasicType selt = n->u.select_stmt.expr->resolved_type;
    const char *ct = ctype_of(selt, n->u.select_stmt.expr->type_idx, NULL);
    int first = 1;

    emit_indent(e); fprintf(e->out, "{\n");
    e->indent_lvl++;

    /* Declare the selector variable */
    if (selt == BT_STRING) {
        emit_line(e, "BString __sel_%d = {NULL, 0, 0};", uid);
        emit_indent(e);
        fprintf(e->out, "bs_assign(&__sel_%d, &(", uid);
        emit_expr(e, n->u.select_stmt.expr);
        fprintf(e->out, "));\n");
    } else {
        emit_indent(e);
        fprintf(e->out, "%s __sel_%d = (", ct, uid);
        emit_expr(e, n->u.select_stmt.expr);
        fprintf(e->out, ");\n");
    }

    for (c = cases; c != NULL; c = c->next) {
        /* CASE ELSE: u.if_stmt.cond == NULL and else_block != NULL */
        int is_else = (c->u.if_stmt.cond == NULL);

        if (is_else) {
            emit_indent(e);
            if (!first) fprintf(e->out, "else\n");
            emit_open(e);
            emit_block(e, c->u.if_stmt.then_block);
            emit_close(e);
        } else {
            /* Build condition from the linked case_cond list */
            const AstNode *ci;
            emit_indent(e);
            if (!first) fprintf(e->out, "else ");
            fprintf(e->out, "if (");
            for (ci = c->u.if_stmt.cond; ci != NULL; ci = ci->next) {
                if (ci != c->u.if_stmt.cond) fprintf(e->out, " || ");
                if (ci->kind == ND_BINOP && ci->u.binop.op == TOK_TO) {
                    /* Range: lo TO hi */
                    fprintf(e->out, "(__sel_%d >= (", uid);
                    emit_expr(e, ci->u.binop.left);
                    fprintf(e->out, ") && __sel_%d <= (", uid);
                    emit_expr(e, ci->u.binop.right);
                    fprintf(e->out, "))");
                } else if (ci->kind == ND_BINOP && ci->u.binop.left == NULL) {
                    /* IS <op> expr */
                    const char *cop = binop_to_c(ci->u.binop.op, selt);
                    if (!cop) cop = "==";
                    if (selt == BT_STRING) {
                        fprintf(e->out, "(bs_compare(&__sel_%d, &(", uid);
                        emit_expr(e, ci->u.binop.right);
                        fprintf(e->out, ")) %s 0)", cop);
                    } else {
                        fprintf(e->out, "(__sel_%d %s (", uid, cop);
                        emit_expr(e, ci->u.binop.right);
                        fprintf(e->out, "))");
                    }
                } else {
                    /* Single value */
                    if (selt == BT_STRING) {
                        fprintf(e->out, "(bs_compare(&__sel_%d, &(", uid);
                        emit_expr(e, ci);
                        fprintf(e->out, ")) == 0)");
                    } else {
                        fprintf(e->out, "(__sel_%d == (", uid);
                        emit_expr(e, ci);
                        fprintf(e->out, "))");
                    }
                }
            }
            fprintf(e->out, ")\n");
            emit_open(e);
            emit_block(e, c->u.if_stmt.then_block);
            emit_close(e);
        }
        first = 0;
    }

    /* Clean up BString selector if needed */
    if (selt == BT_STRING) {
        emit_line(e, "bs_free(&__sel_%d);", uid);
    }

    e->indent_lvl--;
    emit_indent(e); fprintf(e->out, "}\n");
}

/* Emit ASSIGN statement */
static void emit_assign(Emitter *e, const AstNode *n)
{
    const AstNode *lhs = n->u.assign.lhs;
    const AstNode *rhs = n->u.assign.rhs;

    /* MID$ assignment: MID$(target, start[, len]) = value */
    if (lhs && lhs->kind == ND_MID_ASSIGN) {
        emit_indent(e);
        fprintf(e->out, "bs_mid_assign(&(");
        emit_expr(e, lhs->u.mid_assign.target);
        fprintf(e->out, "), (int)(");
        emit_expr(e, lhs->u.mid_assign.start);
        fprintf(e->out, "), (int)(");
        if (lhs->u.mid_assign.length) {
            emit_expr(e, lhs->u.mid_assign.length);
        } else {
            fprintf(e->out, "-1");
        }
        fprintf(e->out, "), &(");
        emit_expr(e, rhs);
        fprintf(e->out, "));\n");
        return;
    }

    /* Function return value: if LHS is a ND_VAR matching the current func name */
    if (lhs && lhs->kind == ND_VAR && e->in_function) {
        char mn[256];
        mangle_name(lhs->u.var.name, mn, (int)sizeof(mn));
        if (strcmp(mn, e->cur_func_name) == 0) {
            emit_indent(e);
            if (e->cur_ret_type == BT_STRING) {
                fprintf(e->out, "bs_assign(&%s_retval, &(", e->cur_func_name);
                emit_expr(e, rhs);
                fprintf(e->out, "));\n");
            } else {
                fprintf(e->out, "%s_retval = (", e->cur_func_name);
                emit_expr(e, rhs);
                fprintf(e->out, ");\n");
            }
            return;
        }
    }

    /* String assignment: use bs_assign */
    if (lhs && lhs->resolved_type == BT_STRING &&
        rhs && rhs->resolved_type == BT_STRING) {
        emit_indent(e);
        fprintf(e->out, "bs_assign(&(");
        emit_expr(e, lhs);
        fprintf(e->out, "), &(");
        emit_expr(e, rhs);
        fprintf(e->out, "));\n");
        return;
    }

    /* Regular assignment */
    emit_indent(e);
    emit_expr(e, lhs);
    fprintf(e->out, " = ");
    /* Type cast if needed */
    if (lhs && rhs && lhs->resolved_type != rhs->resolved_type
        && lhs->resolved_type != BT_STRING
        && rhs->resolved_type != BT_STRING) {
        fprintf(e->out, "(%s)(", ctype_of(lhs->resolved_type, lhs->type_idx, lhs));
        emit_expr(e, rhs);
        fprintf(e->out, ");\n");
    } else {
        fprintf(e->out, "(");
        emit_expr(e, rhs);
        fprintf(e->out, ");\n");
    }
}

/* Emit DIM statement (local or global variable/array declaration).
 * When called from inside a function body (indent_lvl > 0), all scalar
 * declarations are suppressed because emit_implicit_decls_for_block already
 * hoisted them to the top of the function in C89-compliant order.
 * For local dynamic arrays the BArray declaration is also suppressed (already
 * hoisted) but the bsarray_alloc() call is still emitted in-place. */
static void emit_dim(Emitter *e, const AstNode *n)
{
    char        vn[128];
    BasicType   vt = n->u.dim.var_type;
    const char *ct;
    int i;
    int is_local = (e->indent_lvl > 0); /* inside a SUB/FUNCTION body */

    mangle_name(n->u.dim.name, vn, (int)sizeof(vn));

    /* Resolve type name for BT_USER */
    if (vt == BT_USER) {
        ct = ctype_of(vt, n->u.dim.type_idx, n);
    } else {
        ct = ctype_of(vt, 0, n);
    }

    if (n->u.dim.ndims == 0) {
        /* Scalar: declaration was already hoisted when inside a function. */
        if (!is_local) {
            emit_indent(e);
            if (vt == BT_STRING) {
                fprintf(e->out, "BString %s = {NULL, 0, 0};\n", vn);
            } else {
                fprintf(e->out, "%s %s = %s;\n", ct, vn, czero_of(vt));
            }
        }
        /* else: already declared at top of function -- nothing to emit */
        return;
    }

    if (n->u.dim.is_dynamic) {
        /* Dynamic array: BArray.
         * The 'BArray __arr_X;' declaration was already hoisted when local;
         * only emit the bsarray_alloc() initialiser. */
        int ndims = n->u.dim.ndims;
        if (!is_local) {
            emit_indent(e);
            fprintf(e->out, "BArray __arr_%s;\n", vn);
        }
        emit_indent(e);
        fprintf(e->out, "{\n");
        e->indent_lvl++;
        emit_indent(e);
        fprintf(e->out, "int __lbounds[%d];\n", ndims);
        emit_indent(e);
        fprintf(e->out, "int __ubounds[%d];\n", ndims);
        for (i = 0; i < ndims; i++) {
            if (n->u.dim.dims[i].lbound) {
                emit_indent(e);
                fprintf(e->out, "__lbounds[%d] = (int)(", i);
                emit_expr(e, n->u.dim.dims[i].lbound);
                fprintf(e->out, ");\n");
            } else {
                emit_line(e, "__lbounds[%d] = 0;", i);
            }
            emit_indent(e);
            fprintf(e->out, "__ubounds[%d] = (int)(", i);
            emit_expr(e, n->u.dim.dims[i].ubound);
            fprintf(e->out, ");\n");
        }
        emit_line(e, "bsarray_alloc(&__arr_%s, (int)sizeof(%s), %d, __lbounds, __ubounds);",
                  vn, ct, ndims);
        e->indent_lvl--;
        emit_indent(e); fprintf(e->out, "}\n");
    } else {
        /* Static array: declaration was already hoisted when inside a function. */
        if (!is_local) {
            emit_indent(e);
            fprintf(e->out, "%s %s", ct, vn);
            for (i = 0; i < n->u.dim.ndims; i++) {
                int lo = 0;
                /* Try to get static bound */
                if (n->u.dim.dims[i].ubound &&
                    n->u.dim.dims[i].ubound->kind == ND_INT_LIT) {
                    int hi = n->u.dim.dims[i].ubound->u.int_lit.ival;
                    if (n->u.dim.dims[i].lbound &&
                        n->u.dim.dims[i].lbound->kind == ND_INT_LIT) {
                        lo = n->u.dim.dims[i].lbound->u.int_lit.ival;
                    }
                    fprintf(e->out, "[%d]", hi - lo + 1);
                } else {
                    /* Dynamic bound in static context: use [1] as placeholder */
                    fprintf(e->out, "[1]");
                }
            }
            fprintf(e->out, ";\n");
        }
        /* else: declaration was already hoisted by emit_local_array_decls */
    }
}

/* Emit REDIM statement */
static void emit_redim(Emitter *e, const AstNode *n)
{
    char vn[128];
    int ndims = n->u.dim.ndims;
    int i;

    mangle_name(n->u.dim.name, vn, (int)sizeof(vn));

    emit_indent(e);
    fprintf(e->out, "{\n");
    e->indent_lvl++;
    emit_indent(e);
    fprintf(e->out, "int __lbounds[%d];\n", ndims > 0 ? ndims : 1);
    emit_indent(e);
    fprintf(e->out, "int __ubounds[%d];\n", ndims > 0 ? ndims : 1);
    for (i = 0; i < ndims; i++) {
        if (n->u.dim.dims[i].lbound) {
            emit_indent(e);
            fprintf(e->out, "__lbounds[%d] = (int)(", i);
            emit_expr(e, n->u.dim.dims[i].lbound);
            fprintf(e->out, ");\n");
        } else {
            emit_line(e, "__lbounds[%d] = 0;", i);
        }
        emit_indent(e);
        fprintf(e->out, "__ubounds[%d] = (int)(", i);
        emit_expr(e, n->u.dim.dims[i].ubound);
        fprintf(e->out, ");\n");
    }
    emit_line(e, "bsarray_realloc(&__arr_%s, %d, __lbounds, __ubounds, %d);",
              vn, ndims, n->u.dim.preserve);
    e->indent_lvl--;
    emit_indent(e); fprintf(e->out, "}\n");
}

/* Emit CALL statement */
static void emit_call(Emitter *e, const AstNode *n)
{
    const AstNode *arg = n->u.call.args;
    char uname[64];
    int i;
    int is_str;
    Symbol *sub_sym = NULL;
    const ParamDef *param = NULL;

    /* Upper-case for comparison */
    for (i = 0; n->u.call.name[i] && i < 63; i++)
        uname[i] = (char)toupper((unsigned char)n->u.call.name[i]);
    uname[i] = '\0';

    /*
     * Built-in statement keywords that may arrive as ND_CALL nodes
     * (because the parser produced them via the CALL-keyword path).
     * Route them to the same runtime calls as their dedicated ND_* nodes.
     */
    if (strcmp(uname, "CLS") == 0) {
        emit_line(e, "bs_cls();");
        return;
    }
    if (strcmp(uname, "LOCATE") == 0) {
        const AstNode *row = arg;
        const AstNode *col = row ? row->next : NULL;
        emit_indent(e);
        fprintf(e->out, "bs_locate((int)(");
        if (row) emit_expr(e, row); else fprintf(e->out, "1");
        fprintf(e->out, "), (int)(");
        if (col) emit_expr(e, col); else fprintf(e->out, "1");
        fprintf(e->out, "));\n");
        return;
    }
    if (strcmp(uname, "COLOR") == 0) {
        const AstNode *fg = arg;
        const AstNode *bg = fg ? fg->next : NULL;
        emit_indent(e);
        fprintf(e->out, "bs_color((int)(");
        if (fg) emit_expr(e, fg); else fprintf(e->out, "7");
        fprintf(e->out, "), (int)(");
        if (bg) emit_expr(e, bg); else fprintf(e->out, "-1");
        fprintf(e->out, "));\n");
        return;
    }
    if (strcmp(uname, "SHELL") == 0) {
        emit_indent(e);
        if (arg) {
            fprintf(e->out, "bs_shell(&(");
            emit_expr(e, arg);
            fprintf(e->out, "));\n");
        } else {
            fprintf(e->out, "bs_shell(NULL);\n");
        }
        return;
    }
    if (strcmp(uname, "RANDOMIZE") == 0) {
        emit_indent(e);
        if (arg) {
            fprintf(e->out, "bs_randomize((long)(");
            emit_expr(e, arg);
            fprintf(e->out, "));\n");
        } else {
            fprintf(e->out, "bs_randomize(-1L);\n");
        }
        return;
    }

    if (e->st) {
        sub_sym = symtab_lookup((SymTab *)e->st, uname);
        if (sub_sym) param = sub_sym->params;
    }

    /* Normal user-defined SUB call */
    emit_indent(e);
    fprintf(e->out, "%s(", sub_name(n->u.call.name));
    for (arg = n->u.call.args; arg != NULL; arg = arg->next) {
        if (arg != n->u.call.args) fprintf(e->out, ", ");
        is_str = (arg->resolved_type == BT_STRING) ||
                 (param && param->type == BT_STRING) ||
                 (arg->kind == ND_STRING_LIT);
        if (param && param->is_array && arg->kind == ND_VAR) {
            fprintf(e->out, "&__arr_%s", vname(arg->u.var.name));
        } else if (is_str || (param && param->is_byref)) {
            fprintf(e->out, "&(");
            emit_expr(e, arg);
            fprintf(e->out, ")");
        } else {
            emit_expr(e, arg);
        }
        if (param) param = param->next;
    }
    fprintf(e->out, ");\n");
}

/* Emit TRY/CATCH block */
static void emit_try(Emitter *e, const AstNode *n)
{
    int uid = new_uid(e);

    emit_indent(e); fprintf(e->out, "{\n");
    e->indent_lvl++;
    emit_line(e, "BsErrorFrame __try_frame_%d;", uid);
    emit_line(e, "bs_error_push(&__try_frame_%d);", uid);
    emit_line(e, "if (setjmp(__try_frame_%d.jmp) == 0)", uid);
    emit_open(e);
    emit_block(e, n->u.try_stmt.body);
    emit_line(e, "bs_error_pop();");
    emit_close(e);
    emit_line(e, "else");
    emit_open(e);
    if (n->u.try_stmt.catch_var) {
        const char *cv = vname(
            n->u.try_stmt.catch_var->kind == ND_VAR
            ? n->u.try_stmt.catch_var->u.var.name : "err");
        emit_line(e, "BString %s = {NULL, 0, 0};", cv);
        emit_line(e, "bs_error_pop();");
        emit_line(e, "bs_err_msg(&%s);", cv);
        emit_block(e, n->u.try_stmt.catch_body);
        emit_line(e, "bs_free(&%s);", cv);
    } else {
        emit_line(e, "bs_error_pop();");
        emit_block(e, n->u.try_stmt.catch_body);
    }
    emit_close(e);
    /* FINALLY (no official FINALLY in HP-UX BASIC, but support if present) */
    if (n->u.try_stmt.finally_body) {
        emit_block(e, n->u.try_stmt.finally_body);
    }
    e->indent_lvl--;
    emit_indent(e); fprintf(e->out, "}\n");
}

/* Emit ON ERROR GOTO */
static void emit_on_error(Emitter *e, const AstNode *n)
{
    if (n->u.on_error.goto_zero) {
        emit_line(e, "bs_on_error_off();");
    } else {
        emit_line(e, "if (setjmp(*bs_on_error_jmpbuf()) != 0) goto %s;",
                  lbl_name(n->u.on_error.label));
        emit_line(e, "bs_on_error_arm();");
    }
}

/* Emit OPEN statement */
static void emit_open_stmt(Emitter *e, const AstNode *n)
{
    const char *mode_str;

    switch (n->u.open.mode) {
        case TOK_INPUT:  mode_str = "BSFILE_INPUT";  break;
        case TOK_OUTPUT: mode_str = "BSFILE_OUTPUT"; break;
        case TOK_APPEND: mode_str = "BSFILE_APPEND"; break;
        case TOK_RANDOM: mode_str = "BSFILE_RANDOM"; break;
        case TOK_BINARY: mode_str = "BSFILE_BINARY"; break;
        default:         mode_str = "BSFILE_INPUT";  break;
    }

    emit_indent(e);
    fprintf(e->out, "bsfile_open((int)(");
    emit_expr(e, n->u.open.filenum);
    fprintf(e->out, "), &(");
    emit_expr(e, n->u.open.path);
    fprintf(e->out, "), %s, ", mode_str);
    if (n->u.open.reclen) {
        fprintf(e->out, "(long)(");
        emit_expr(e, n->u.open.reclen);
        fprintf(e->out, ")");
    } else {
        fprintf(e->out, "0L");
    }
    fprintf(e->out, ");\n");
}

/* Emit WRITE statement */
static void emit_write(Emitter *e, const AstNode *n)
{
    const AstNode *v;
    int filenum_expr = (n->u.file_io.filenum != NULL);

    for (v = n->u.file_io.vars; v != NULL; v = v->next) {
        emit_indent(e);
        if (filenum_expr) {
            /* File write */
            switch (v->resolved_type) {
                case BT_INTEGER:
                    fprintf(e->out, "bsfile_write_int((int)(");
                    emit_expr(e, n->u.file_io.filenum);
                    fprintf(e->out, "), (int)(");
                    emit_expr(e, v);
                    fprintf(e->out, "));\n");
                    break;
                case BT_LONG:
                    fprintf(e->out, "bsfile_write_long((int)(");
                    emit_expr(e, n->u.file_io.filenum);
                    fprintf(e->out, "), (long)(");
                    emit_expr(e, v);
                    fprintf(e->out, "));\n");
                    break;
                case BT_SINGLE:
                    fprintf(e->out, "bsfile_write_float((int)(");
                    emit_expr(e, n->u.file_io.filenum);
                    fprintf(e->out, "), (float)(");
                    emit_expr(e, v);
                    fprintf(e->out, "));\n");
                    break;
                case BT_DOUBLE:
                    fprintf(e->out, "bsfile_write_double((int)(");
                    emit_expr(e, n->u.file_io.filenum);
                    fprintf(e->out, "), (double)(");
                    emit_expr(e, v);
                    fprintf(e->out, "));\n");
                    break;
                case BT_STRING:
                    fprintf(e->out, "bsfile_write_string((int)(");
                    emit_expr(e, n->u.file_io.filenum);
                    fprintf(e->out, "), &(");
                    emit_expr(e, v);
                    fprintf(e->out, "));\n");
                    break;
                default:
                    fprintf(e->out, "bsfile_write_int((int)(");
                    emit_expr(e, n->u.file_io.filenum);
                    fprintf(e->out, "), (int)(");
                    emit_expr(e, v);
                    fprintf(e->out, "));\n");
                    break;
            }
        } else {
            /* PRINT to stdout (plain WRITE) */
            fprintf(e->out, "bs_print_int(0); /* WRITE */\n");
        }
    }
}

/* Emit READ statement (from file) */
static void emit_read(Emitter *e, const AstNode *n)
{
    const AstNode *v;
    for (v = n->u.file_io.vars; v != NULL; v = v->next) {
        const char *varname = (v->kind == ND_VAR) ? vname(v->u.var.name) : "__unknown";
        emit_indent(e);
        switch (v->resolved_type) {
            case BT_INTEGER:
                fprintf(e->out, "bsfile_read_int((int)(");
                emit_expr(e, n->u.file_io.filenum);
                fprintf(e->out, "), &%s);\n", varname);
                break;
            case BT_LONG:
                fprintf(e->out, "bsfile_read_long((int)(");
                emit_expr(e, n->u.file_io.filenum);
                fprintf(e->out, "), &%s);\n", varname);
                break;
            case BT_DOUBLE:
            case BT_SINGLE:
                fprintf(e->out, "bsfile_read_double((int)(");
                emit_expr(e, n->u.file_io.filenum);
                fprintf(e->out, "), &%s);\n", varname);
                break;
            case BT_STRING:
                fprintf(e->out, "bsfile_read_string((int)(");
                emit_expr(e, n->u.file_io.filenum);
                fprintf(e->out, "), &%s);\n", varname);
                break;
            default:
                fprintf(e->out, "bsfile_read_int((int)(");
                emit_expr(e, n->u.file_io.filenum);
                fprintf(e->out, "), &%s);\n", varname);
                break;
        }
    }
}

/* Emit GET statement */
static void emit_get(Emitter *e, const AstNode *n)
{
    const AstNode *v = n->u.file_io.vars;
    emit_indent(e);
    fprintf(e->out, "bsfile_get((int)(");
    emit_expr(e, n->u.file_io.filenum);
    fprintf(e->out, "), (long)(");
    if (n->u.file_io.recno) {
        emit_expr(e, n->u.file_io.recno);
    } else {
        fprintf(e->out, "0");
    }
    if (v && v->kind == ND_VAR) {
        const char *vn2 = vname(v->u.var.name);
        fprintf(e->out, "), &%s, (int)sizeof(%s));\n", vn2, vn2);
    } else {
        fprintf(e->out, "), NULL, 0);\n");
    }
}

/* Emit PUT statement */
static void emit_put(Emitter *e, const AstNode *n)
{
    const AstNode *v = n->u.file_io.vars;
    emit_indent(e);
    fprintf(e->out, "bsfile_put((int)(");
    emit_expr(e, n->u.file_io.filenum);
    fprintf(e->out, "), (long)(");
    if (n->u.file_io.recno) {
        emit_expr(e, n->u.file_io.recno);
    } else {
        fprintf(e->out, "0");
    }
    if (v) {
        fprintf(e->out, "), &(");
        emit_expr(e, v);
        fprintf(e->out, "), (int)sizeof(");
        emit_expr(e, v);
        fprintf(e->out, "));\n");
    } else {
        fprintf(e->out, "), NULL, 0);\n");
    }
}

/* Emit FIELD statement */
static void emit_field(Emitter *e, const AstNode *n)
{
    const AstNode *w = n->u.file_io.recno;
    const AstNode *v = n->u.file_io.vars;

    emit_indent(e);
    fprintf(e->out, "bsfile_field_reset((int)(");
    emit_expr(e, n->u.file_io.filenum);
    fprintf(e->out, "));\n");

    while (w && v) {
        emit_indent(e);
        fprintf(e->out, "bsfile_field_add((int)(");
        emit_expr(e, n->u.file_io.filenum);
        fprintf(e->out, "), (int)(");
        emit_expr(e, w);
        fprintf(e->out, "), &(");
        emit_expr(e, v);
        fprintf(e->out, "));\n");
        w = w->next;
        v = v->next;
    }
}

/* Emit LSET / RSET statement */
static void emit_lset_rset(Emitter *e, const AstNode *n, int is_rset)
{
    emit_indent(e);
    fprintf(e->out, "%s(&(", is_rset ? "bs_rset" : "bs_lset");
    emit_expr(e, n->u.assign.lhs);
    fprintf(e->out, "), &(");
    emit_expr(e, n->u.assign.rhs);
    fprintf(e->out, "));\n");
}

/* =========================================================================
 * Single-statement emitter
 * ======================================================================== */

static void emit_stmt(Emitter *e, const AstNode *n)
{
    if (!n) return;

    switch (n->kind) {
        case ND_ASSIGN:
            emit_assign(e, n);
            break;

        case ND_MID_ASSIGN:
            emit_indent(e);
            fprintf(e->out, "bs_mid_assign(&(");
            emit_expr(e, n->u.mid_assign.target);
            fprintf(e->out, "), (int)(");
            emit_expr(e, n->u.mid_assign.start);
            fprintf(e->out, "), (int)(");
            if (n->u.mid_assign.length) {
                emit_expr(e, n->u.mid_assign.length);
            } else {
                fprintf(e->out, "-1");
            }
            fprintf(e->out, "), &(");
            emit_expr(e, n->u.mid_assign.value);
            fprintf(e->out, "));\n");
            break;

        case ND_IF:
            emit_if(e, n);
            break;

        case ND_FOR:
            emit_for(e, n);
            break;

        case ND_WHILE:
            emit_indent(e);
            fprintf(e->out, "while ((");
            emit_expr(e, n->u.while_stmt.cond);
            fprintf(e->out, "))\n");
            emit_open(e);
            emit_block(e, n->u.while_stmt.body);
            emit_close(e);
            break;

        case ND_DO:
            emit_do(e, n);
            break;

        case ND_SELECT:
            emit_select(e, n);
            break;

        case ND_GOSUB:
            {
                /* Emit: goto label; __bas_ret_N: ;
                 * Push the return uid onto the GOSUB stack so that the
                 * matching RETURN can emit: goto __bas_ret_N; */
                int uid = new_uid(e);
                if (e->gosub_top < EMITTER_GOSUB_DEPTH) {
                    e->gosub_stack[e->gosub_top++] = uid;
                }
                emit_line(e, "goto %s; /* GOSUB %s */",
                          lbl_name(n->u.jump.label), n->u.jump.label);
                emit_line(e, "__bas_ret_%d: ;", uid);
            }
            break;

        case ND_GOTO:
            emit_line(e, "goto %s;", lbl_name(n->u.jump.label));
            break;

        case ND_RETURN:
            /* RETURN from GOSUB: pop the top uid and jump back. */
            if (e->gosub_top > 0) {
                int ret_uid = e->gosub_stack[--e->gosub_top];
                emit_line(e, "goto __bas_ret_%d; /* RETURN */", ret_uid);
            } else {
                emit_line(e, "/* RETURN -- no matching GOSUB on stack */");
            }
            break;

        case ND_LABEL:
            /* Labels must be at statement level in C89 before a statement.
             * Emit label followed by a null statement. */
            emit_indent(e);
            fprintf(e->out, "%s: ;\n", lbl_name(n->u.label.name));
            break;

        case ND_PRINT:
            emit_print(e, n);
            break;

        case ND_INPUT:
            emit_input(e, n);
            break;

        case ND_LINE_INPUT:
            emit_line_input(e, n);
            break;

        case ND_OPEN:
            emit_open_stmt(e, n);
            break;

        case ND_CLOSE:
            emit_indent(e);
            if (n->u.close_stmt.filenum) {
                fprintf(e->out, "bsfile_close((int)(");
                emit_expr(e, n->u.close_stmt.filenum);
                fprintf(e->out, "));\n");
            } else {
                fprintf(e->out, "bsfile_close(0); /* CLOSE ALL */\n");
            }
            break;

        case ND_WRITE:
            emit_write(e, n);
            break;

        case ND_READ:
            emit_read(e, n);
            break;

        case ND_GET:
            emit_get(e, n);
            break;

        case ND_PUT:
            emit_put(e, n);
            break;

        case ND_FIELD:
            emit_field(e, n);
            break;

        case ND_LSET:
            emit_lset_rset(e, n, 0);
            break;

        case ND_RSET:
            emit_lset_rset(e, n, 1);
            break;

        case ND_TRY:
            emit_try(e, n);
            break;

        case ND_ON_ERROR:
            emit_on_error(e, n);
            break;

        case ND_RESUME_NEXT:
            emit_line(e, "bs_resume_next();");
            break;

        case ND_EXIT_SUB:
            emit_line(e, "return;");
            break;

        case ND_EXIT_FUNC:
            if (e->in_function) {
                if (e->cur_ret_type == BT_STRING) {
                    emit_line(e, "bs_assign(__retval, &%s_retval);", e->cur_func_name);
                    emit_line(e, "bs_free(&%s_retval);", e->cur_func_name);
                    emit_line(e, "return;");
                } else {
                    emit_line(e, "return %s_retval;", e->cur_func_name);
                }
            } else {
                emit_line(e, "return;");
            }
            break;

        case ND_EXIT_FOR:
        case ND_EXIT_DO:
            emit_line(e, "break;");
            break;

        case ND_LPRINT:
            emit_indent(e);
            fprintf(e->out, "bs_lprint_begin();\n");
            {
                /* Reuse the same item-walk as PRINT but call lp_ variants */
                const AstNode *item;
                int has_trailing_sep = 0;
                if (n->u.print.fmt) {
                    int argc = 0;
                    for (item = n->u.print.items; item; item = item->next) {
                        if (item->kind == ND_INT_LIT && item->type_idx == -1) {
                            has_trailing_sep = (item->next == NULL) ? 1 : 0;
                            continue;
                        }
                        has_trailing_sep = 0;
                        argc++;
                    }
                    emit_indent(e);
                    fprintf(e->out, "bs_lprint_using(&(");
                    emit_expr(e, n->u.print.fmt);
                    fprintf(e->out, "), %d", argc);
                    for (item = n->u.print.items; item; item = item->next) {
                        if (item->kind == ND_INT_LIT && item->type_idx == -1) continue;
                        if (item->resolved_type == BT_STRING || item->kind == ND_STRING_LIT) {
                            fprintf(e->out, ", BSPU_STR, &(");
                            emit_expr(e, item);
                            fprintf(e->out, ")");
                        } else {
                            fprintf(e->out, ", BSPU_NUM, (double)(");
                            emit_expr(e, item);
                            fprintf(e->out, ")");
                        }
                    }
                    fprintf(e->out, ");\n");
                    if (!has_trailing_sep) {
                        emit_line(e, "bs_lprint_newline();");
                    }
                } else {
                    for (item = n->u.print.items; item; item = item->next) {
                        has_trailing_sep = 0;
                        if (item->kind == ND_INT_LIT && item->type_idx == -1) {
                            has_trailing_sep = (item->next == NULL) ? 1 : 0;
                            continue;
                        }
                        emit_indent(e);
                        switch (item->resolved_type) {
                            case BT_INTEGER:
                                fprintf(e->out, "bs_lprint_int((int)(");
                                emit_expr(e, item);
                                fprintf(e->out, "));\n");
                                break;
                            case BT_LONG:
                                fprintf(e->out, "bs_lprint_long((long)(");
                                emit_expr(e, item);
                                fprintf(e->out, "));\n");
                                break;
                            case BT_SINGLE:
                                fprintf(e->out, "bs_lprint_float((float)(");
                                emit_expr(e, item);
                                fprintf(e->out, "));\n");
                                break;
                            case BT_DOUBLE:
                                fprintf(e->out, "bs_lprint_double((double)(");
                                emit_expr(e, item);
                                fprintf(e->out, "));\n");
                                break;
                            case BT_STRING:
                                fprintf(e->out, "bs_lprint_string(&(");
                                emit_expr(e, item);
                                fprintf(e->out, "));\n");
                                break;
                            default:
                                fprintf(e->out, "bs_lprint_int((int)(");
                                emit_expr(e, item);
                                fprintf(e->out, "));\n");
                                break;
                        }
                    }
                    if (!has_trailing_sep) {
                        emit_line(e, "bs_lprint_newline();");
                    }
                }
            }
            break;

        case ND_LPRINT_OPEN:
            if (n->type_idx == 1) {
                emit_line(e, "bs_lprint_close();");
            } else if (n->u.lprint_open.printer) {
                emit_indent(e);
                fprintf(e->out, "bs_lprint_open(&(");
                emit_expr(e, n->u.lprint_open.printer);
                fprintf(e->out, "));\n");
            } else {
                emit_line(e, "bs_lprint_open(NULL);");
            }
            break;

        case ND_CLS:
            emit_line(e, "bs_cls();");
            break;

        case ND_LOCATE:
            emit_indent(e);
            fprintf(e->out, "bs_locate((int)(");
            emit_expr(e, n->u.locate.row);
            fprintf(e->out, "), (int)(");
            if (n->u.locate.col) {
                emit_expr(e, n->u.locate.col);
            } else {
                fprintf(e->out, "1");
            }
            fprintf(e->out, "));\n");
            break;

        case ND_COLOR:
            emit_indent(e);
            fprintf(e->out, "bs_color((int)(");
            emit_expr(e, n->u.color.fg);
            fprintf(e->out, "), (int)(");
            if (n->u.color.bg) {
                emit_expr(e, n->u.color.bg);
            } else {
                fprintf(e->out, "-1");
            }
            fprintf(e->out, "));\n");
            break;

        case ND_BGCOLOR:
            emit_indent(e);
            fprintf(e->out, "bs_bgcolor((int)(");
            emit_expr(e, n->u.color.bg);
            fprintf(e->out, "));\n");
            break;

        case ND_SHELL:
            emit_indent(e);
            if (n->u.shell.cmd) {
                fprintf(e->out, "bs_shell(&(");
                emit_expr(e, n->u.shell.cmd);
                fprintf(e->out, "));\n");
            } else {
                fprintf(e->out, "bs_shell(NULL);\n");
            }
            break;

        case ND_END:
            emit_line(e, "bs_runtime_cleanup();");
            emit_line(e, "exit(0);");
            break;

        case ND_DIM:
            emit_dim(e, n);
            break;

        case ND_REDIM:
            emit_redim(e, n);
            break;

        case ND_CALL:
            emit_call(e, n);
            break;

        case ND_SUB_DEF:
        case ND_FUNC_DEF:
        case ND_TYPE_DEF:
            /* These are top-level constructs, not inline statements.
             * They are handled in emit_program(). */
            break;

        case ND_BLOCK:
            emit_block(e, n);
            break;

        default:
            emit_line(e, "/* unsupported stmt kind %d */", (int)n->kind);
            break;
    }
}

/* Emit all statements in a block (the stmts chain). */
static void emit_block(Emitter *e, const AstNode *block)
{
    const AstNode *s;
    if (!block) return;

    if (block->kind == ND_BLOCK) {
        for (s = block->u.block.stmts; s != NULL; s = s->next) {
            emit_stmt(e, s);
        }
    } else {
        /* A single statement passed directly */
        emit_stmt(e, block);
    }
}

/* =========================================================================
 * Parameter list emission (shared by SUB and FUNCTION)
 * ======================================================================== */

static void emit_param_list(Emitter *e, const ParamDef *p)
{
    int first = 1;
    fprintf(e->out, "(");
    for (; p != NULL; p = p->next) {
        const char *ct;
        if (!first) fprintf(e->out, ", ");
        first = 0;

        ct = ctype_of(p->type, p->type_idx, NULL);

        if (p->is_array) {
            /* Array parameter — pass as BArray* */
            fprintf(e->out, "BArray *%s", vname(p->name));
        } else if (p->is_byref || p->type == BT_STRING) {
            fprintf(e->out, "%s *%s", ct, vname(p->name));
        } else {
            fprintf(e->out, "%s %s", ct, vname(p->name));
        }
    }
    if (first) {
        fprintf(e->out, "void");
    }
    fprintf(e->out, ")");
}

/* =========================================================================
 * Collect top-level program nodes
 * ======================================================================== */

/* Walk the top-level block's stmts chain and call cb for each node of the
 * given kind. */
typedef void (*NodeVisitor)(Emitter *e, const AstNode *n);

static void visit_toplevel(Emitter *e, const AstNode *root,
                            NodeKind kind, NodeVisitor cb)
{
    const AstNode *s;
    if (!root) return;

    if (root->kind == ND_BLOCK) {
        for (s = root->u.block.stmts; s != NULL; s = s->next) {
            if (s->kind == kind) cb(e, s);
        }
    }
}

/* =========================================================================
 * TYPE definitions
 * ======================================================================== */

static void emit_one_typedef(Emitter *e, const AstNode *n)
{
    const ParamDef *f;
    int idx = e->typedef_idx++;
    fprintf(e->out, "typedef struct {\n");
    for (f = n->u.type_def.fields; f != NULL; f = f->next) {
        const char *ct = ctype_of(f->type, f->type_idx, NULL);
        if (f->type == BT_STRING) {
            fprintf(e->out, "    BString %s;\n", vname(f->name));
        } else {
            fprintf(e->out, "    %s %s;\n", ct, vname(f->name));
        }
    }
    fprintf(e->out, "} %s, TYPE_%d;\n\n", type_name(n->u.type_def.name), idx);
}

/* =========================================================================
 * Forward declarations
 * ======================================================================== */

static void emit_fwd_decl_sub(Emitter *e, const AstNode *n)
{
    fprintf(e->out, "static void %s", sub_name(n->u.sub_def.name));
    emit_param_list(e, n->u.sub_def.params);
    fprintf(e->out, ";\n");
}

static void emit_fwd_decl_func(Emitter *e, const AstNode *n)
{
    const char *ct;
    ct = ctype_of(n->u.sub_def.ret_type, n->u.sub_def.ret_type_idx, NULL);
    if (n->u.sub_def.ret_type == BT_STRING) {
        /* Functions returning BString return it by pointer via out-param */
        fprintf(e->out, "static void %s", func_name(n->u.sub_def.name));
        /* prepend BString* retval param */
        fprintf(e->out, "(BString *__retval");
        {
            const ParamDef *p;
            for (p = n->u.sub_def.params; p != NULL; p = p->next) {
                const char *pct = ctype_of(p->type, p->type_idx, NULL);
                fprintf(e->out, ", ");
                if (p->is_byref || p->type == BT_STRING) {
                    fprintf(e->out, "%s *%s", pct, vname(p->name));
                } else {
                    fprintf(e->out, "%s %s", pct, vname(p->name));
                }
            }
        }
        fprintf(e->out, ");\n");
    } else {
        fprintf(e->out, "static %s %s", ct, func_name(n->u.sub_def.name));
        emit_param_list(e, n->u.sub_def.params);
        fprintf(e->out, ";\n");
    }
}

/* =========================================================================
 * SUB / FUNCTION definitions
 * ======================================================================== */

/* =========================================================================
 * Implicit variable declaration emitter
 * Walks an AST block and emits C declarations for any variables used via
 * ND_VAR that were not declared with ND_DIM (implicit BASIC declarations).
 * Uses a simple fixed-size seen-names table to avoid duplicate declarations.
 * ======================================================================== */

#define IMPL_MAX 256

typedef struct {
    char names[IMPL_MAX][128];
    int  count;
} ImplSeen;

static int impl_seen(ImplSeen *s, const char *nm)
{
    int i;
    for (i = 0; i < s->count; i++) {
        if (strcmp(s->names[i], nm) == 0) return 1;
    }
    if (s->count < IMPL_MAX) {
        strncpy(s->names[s->count], nm, 127);
        s->names[s->count][127] = '\0';
        s->count++;
    }
    return 0;
}

/* Collect all DIM'd names in a block (recursively) into the seen table so we skip them. */
static void collect_dim_names(const AstNode *block, ImplSeen *seen)
{
    const AstNode *s;
    if (!block) return;
    s = (block->kind == ND_BLOCK) ? block->u.block.stmts : block;
    for (; s; s = s->next) {
        if (s->kind == ND_DIM || s->kind == ND_REDIM) {
            char mn[128];
            mangle_name(s->u.dim.name, mn, (int)sizeof(mn));
            impl_seen(seen, mn);
        } else if (s->kind == ND_IF) {
            const AstNode *ei;
            collect_dim_names(s->u.if_stmt.then_block, seen);
            for (ei = s->u.if_stmt.elseif_chain; ei; ei = ei->next) {
                collect_dim_names(ei->u.if_stmt.then_block, seen);
            }
            collect_dim_names(s->u.if_stmt.else_block, seen);
        } else if (s->kind == ND_FOR) {
            collect_dim_names(s->u.for_stmt.body, seen);
        } else if (s->kind == ND_WHILE) {
            collect_dim_names(s->u.while_stmt.body, seen);
        } else if (s->kind == ND_DO) {
            collect_dim_names(s->u.do_stmt.body, seen);
        } else if (s->kind == ND_SELECT) {
            const AstNode *c;
            for (c = s->u.select_stmt.cases; c; c = c->next) {
                collect_dim_names(c->u.if_stmt.then_block, seen);
            }
        } else if (s->kind == ND_TRY) {
            if (s->u.try_stmt.catch_var && s->u.try_stmt.catch_var->kind == ND_VAR) {
                char mn[128];
                mangle_name(s->u.try_stmt.catch_var->u.var.name, mn, (int)sizeof(mn));
                impl_seen(seen, mn);
            }
            collect_dim_names(s->u.try_stmt.body, seen);
            collect_dim_names(s->u.try_stmt.catch_body, seen);
            collect_dim_names(s->u.try_stmt.finally_body, seen);
        }
    }
}

/* Collect top-level DIM names from the root AST block */
static void collect_global_dim_names(const AstNode *root, ImplSeen *seen)
{
    collect_dim_names(root, seen);
}

/*
 * Emit hoisted declarations for local scalars and arrays (both static and
 * dynamic) found anywhere in a SUB/FUNCTION body.  This is needed because
 * C89 does not allow mid-block declarations, but BASIC allows DIM inside
 * loops, branches, and mid-function.
 *
 * We track which variable/array names we have already declared (via the
 * dim_seen table) so we never emit duplicates.
 *
 * Called from emit_implicit_decls_for_block, which runs at the very
 * top of the function body before any executable statements are emitted.
 */
static void emit_local_dim_decls(Emitter *e, const AstNode *block, ImplSeen *dim_seen)
{
    const AstNode *s;
    if (!block) return;
    s = (block->kind == ND_BLOCK) ? block->u.block.stmts : block;
    for (; s; s = s->next) {
        if (s->kind == ND_DIM) {
            char vn[128];
            char key[144];
            BasicType vt = s->u.dim.var_type;
            const char *ct;
            int i;
            mangle_name(s->u.dim.name, vn, (int)sizeof(vn));
            if (vt == BT_USER) {
                ct = ctype_of(vt, s->u.dim.type_idx, s);
            } else {
                ct = ctype_of(vt, 0, s);
            }

            if (s->u.dim.ndims == 0) {
                if (impl_seen(dim_seen, vn)) continue;
                if (vt == BT_STRING) {
                    emit_line(e, "BString %s = {NULL, 0, 0};", vn);
                } else {
                    emit_line(e, "%s %s = %s;", ct, vn, czero_of(vt));
                }
            } else {
                if (s->u.dim.is_dynamic) {
                    key[0] = '\0';
                    strncat(key, "__arr_", 6);
                    strncat(key, vn, 127);
                } else {
                    strncpy(key, vn, (int)sizeof(key) - 1);
                    key[(int)sizeof(key) - 1] = '\0';
                }
                if (impl_seen(dim_seen, key)) continue;

                if (s->u.dim.is_dynamic) {
                    emit_line(e, "BArray __arr_%s = {NULL, 0, NULL, NULL, 0};", vn);
                } else {
                    emit_indent(e);
                    fprintf(e->out, "%s %s", ct, vn);
                    for (i = 0; i < s->u.dim.ndims; i++) {
                        if (s->u.dim.dims[i].ubound &&
                            s->u.dim.dims[i].ubound->kind == ND_INT_LIT) {
                            int lo = 0;
                            int hi = s->u.dim.dims[i].ubound->u.int_lit.ival;
                            if (s->u.dim.dims[i].lbound &&
                                s->u.dim.dims[i].lbound->kind == ND_INT_LIT) {
                                lo = s->u.dim.dims[i].lbound->u.int_lit.ival;
                            }
                            fprintf(e->out, "[%d]", hi - lo + 1);
                        } else {
                            fprintf(e->out, "[1]");
                        }
                    }
                    fprintf(e->out, ";\n");
                }
            }
        } else if (s->kind == ND_IF) {
            const AstNode *ei;
            emit_local_dim_decls(e, s->u.if_stmt.then_block, dim_seen);
            for (ei = s->u.if_stmt.elseif_chain; ei; ei = ei->next)
                emit_local_dim_decls(e, ei->u.if_stmt.then_block, dim_seen);
            emit_local_dim_decls(e, s->u.if_stmt.else_block, dim_seen);
        } else if (s->kind == ND_FOR) {
            emit_local_dim_decls(e, s->u.for_stmt.body, dim_seen);
        } else if (s->kind == ND_WHILE) {
            emit_local_dim_decls(e, s->u.while_stmt.body, dim_seen);
        } else if (s->kind == ND_DO) {
            emit_local_dim_decls(e, s->u.do_stmt.body, dim_seen);
        } else if (s->kind == ND_SELECT) {
            const AstNode *c;
            for (c = s->u.select_stmt.cases; c; c = c->next)
                emit_local_dim_decls(e, c->u.if_stmt.then_block, dim_seen);
        } else if (s->kind == ND_TRY) {
            emit_local_dim_decls(e, s->u.try_stmt.body, dim_seen);
            emit_local_dim_decls(e, s->u.try_stmt.catch_body, dim_seen);
            emit_local_dim_decls(e, s->u.try_stmt.finally_body, dim_seen);
        }
    }
}

/* Recursively walk an expression node looking for ND_VAR references. */
static void scan_expr_vars(Emitter *e, const AstNode *node, ImplSeen *seen)
{
    if (!node) return;
    if (node->kind == ND_VAR) {
        char mn[128];
        const char *ct;
        mangle_name(node->u.var.name, mn, (int)sizeof(mn));
        /* Built-in zero-arg functions must never be declared as implicit variables */
        if (strcmp(mn, "inkey") == 0 || strcmp(mn, "command") == 0 ||
            strcmp(mn, "error") == 0 || strcmp(mn, "rnd") == 0 ||
            strcmp(mn, "errlevel") == 0 || strcmp(mn, "termrows") == 0 ||
            strcmp(mn, "termcols") == 0 || strcmp(mn, "err") == 0 ||
            strcmp(mn, "erl") == 0) {
            return;
        }
        if (!impl_seen(seen, mn)) {
            /* Not yet seen and not DIM'd: emit a declaration */
            ct = ctype_of(node->resolved_type, node->type_idx, node);
            if (node->resolved_type == BT_STRING) {
                emit_line(e, "BString %s = {NULL, 0, 0}; /* implicit */", mn);
            } else {
                emit_line(e, "%s %s = %s; /* implicit */",
                          ct, mn, czero_of(node->resolved_type));
            }
        }
        return;
    }
    /* Recurse into children based on kind */
    switch (node->kind) {
        case ND_BINOP:
            scan_expr_vars(e, node->u.binop.left,  seen);
            scan_expr_vars(e, node->u.binop.right, seen);
            break;
        case ND_UNOP:
            scan_expr_vars(e, node->u.binop.left,  seen);
            break;
        case ND_ARRAY_INDEX:
            /* Do not scan array variable itself as a scalar variable */
            scan_expr_vars(e, node->u.array_index.indices, seen);
            break;
        case ND_FIELD_ACCESS:
            scan_expr_vars(e, node->u.field_access.record, seen);
            break;
        case ND_FUNC_CALL:
            scan_expr_vars(e, node->u.func_call.args, seen);
            break;
        case ND_CAST:
            scan_expr_vars(e, node->u.cast.expr, seen);
            break;
        case ND_MID_ASSIGN:
            scan_expr_vars(e, node->u.mid_assign.target, seen);
            scan_expr_vars(e, node->u.mid_assign.start,  seen);
            scan_expr_vars(e, node->u.mid_assign.length, seen);
            scan_expr_vars(e, node->u.mid_assign.value,  seen);
            break;
        default: break;
    }
    /* Walk sibling chain for expression lists */
    scan_expr_vars(e, node->next, seen);
}

/* Walk a statement block and emit implicit declarations. */
static void emit_implicit_decls(Emitter *e, const AstNode *block, ImplSeen *seen)
{
    const AstNode *s;
    if (!block) return;
    s = (block->kind == ND_BLOCK) ? block->u.block.stmts : block;
    for (; s; s = s->next) {
        switch (s->kind) {
            case ND_ASSIGN:
            case ND_LSET:
            case ND_RSET:
                scan_expr_vars(e, s->u.assign.lhs, seen);
                scan_expr_vars(e, s->u.assign.rhs, seen);
                break;
            case ND_MID_ASSIGN:
                scan_expr_vars(e, s->u.mid_assign.target, seen);
                scan_expr_vars(e, s->u.mid_assign.start,  seen);
                scan_expr_vars(e, s->u.mid_assign.length, seen);
                scan_expr_vars(e, s->u.mid_assign.value,  seen);
                break;
            case ND_FIELD:
            case ND_READ:
            case ND_WRITE:
            case ND_GET:
            case ND_PUT:
                scan_expr_vars(e, s->u.file_io.filenum, seen);
                scan_expr_vars(e, s->u.file_io.recno,   seen);
                scan_expr_vars(e, s->u.file_io.vars,    seen);
                break;
            case ND_PRINT:
                scan_expr_vars(e, s->u.print.items, seen);
                break;
            case ND_INPUT:
            case ND_LINE_INPUT:
                scan_expr_vars(e, s->u.input.vars, seen);
                break;
            case ND_IF: {
                const AstNode *ei;
                scan_expr_vars(e, s->u.if_stmt.cond, seen);
                /* Recurse into sub-blocks */
                emit_implicit_decls(e, s->u.if_stmt.then_block, seen);
                for (ei = s->u.if_stmt.elseif_chain; ei != NULL; ei = ei->next) {
                    scan_expr_vars(e, ei->u.if_stmt.cond, seen);
                    emit_implicit_decls(e, ei->u.if_stmt.then_block, seen);
                }
                emit_implicit_decls(e, s->u.if_stmt.else_block, seen);
                break;
            }
            case ND_FOR:
                scan_expr_vars(e, s->u.for_stmt.init,  seen);
                scan_expr_vars(e, s->u.for_stmt.limit, seen);
                scan_expr_vars(e, s->u.for_stmt.step,  seen);
                /* Loop variable itself */
                if (s->u.for_stmt.var) {
                    char mn[128];
                    mangle_name(s->u.for_stmt.var, mn, (int)sizeof(mn));
                    if (!impl_seen(seen, mn)) {
                        BasicType vt = s->u.for_stmt.init
                            ? s->u.for_stmt.init->resolved_type : BT_INTEGER;
                        emit_line(e, "%s %s = %s; /* implicit FOR var */",
                                  ctype_of(vt, 0, NULL), mn, czero_of(vt));
                    }
                }
                emit_implicit_decls(e, s->u.for_stmt.body, seen);
                break;
            case ND_WHILE:
                scan_expr_vars(e, s->u.while_stmt.cond, seen);
                emit_implicit_decls(e, s->u.while_stmt.body, seen);
                break;
            case ND_DO:
                scan_expr_vars(e, s->u.do_stmt.cond, seen);
                emit_implicit_decls(e, s->u.do_stmt.body, seen);
                break;
            case ND_SELECT: {
                const AstNode *c;
                const AstNode *ci;
                scan_expr_vars(e, s->u.select_stmt.expr, seen);
                for (c = s->u.select_stmt.cases; c != NULL; c = c->next) {
                    for (ci = c->u.if_stmt.cond; ci != NULL; ci = ci->next) {
                        scan_expr_vars(e, ci, seen);
                    }
                    emit_implicit_decls(e, c->u.if_stmt.then_block, seen);
                }
                break;
            }
            case ND_CALL:
                scan_expr_vars(e, s->u.call.args, seen);
                break;
            case ND_LOCATE:
                scan_expr_vars(e, s->u.locate.row, seen);
                scan_expr_vars(e, s->u.locate.col, seen);
                break;
            case ND_COLOR:
                scan_expr_vars(e, s->u.color.fg, seen);
                scan_expr_vars(e, s->u.color.bg, seen);
                break;
            case ND_BGCOLOR:
                scan_expr_vars(e, s->u.color.bg, seen);
                break;
            case ND_TRY:
                if (s->u.try_stmt.catch_var && s->u.try_stmt.catch_var->kind == ND_VAR) {
                    char mn[128];
                    mangle_name(s->u.try_stmt.catch_var->u.var.name, mn, (int)sizeof(mn));
                    impl_seen(seen, mn);
                }
                emit_implicit_decls(e, s->u.try_stmt.body, seen);
                emit_implicit_decls(e, s->u.try_stmt.catch_body, seen);
                emit_implicit_decls(e, s->u.try_stmt.finally_body, seen);
                break;
            default: break;
        }
    }
}

/*
 * Top-level helper: pre-scan a function/sub/module body, collect DIM names,
 * then emit declarations for any implicitly-used variables.
 * Must be called right after the opening '{' and before emit_block().
 */
/*
 * params: optional SUB/FUNC parameter list whose names should be pre-seeded
 * into the seen table so implicit-decl scanning doesn't re-declare them.
 */
static void emit_implicit_decls_for_block(Emitter *e, const AstNode *block,
                                           const ParamDef *params)
{
    ImplSeen seen;
    ImplSeen arr_seen;
    const ParamDef *p;
    char mn[128];
    seen.count = 0;
    arr_seen.count = 0;
    /* Pre-seed parameter names so they are never re-declared as locals. */
    for (p = params; p != NULL; p = p->next) {
        mangle_name(p->name, mn, (int)sizeof(mn));
        impl_seen(&seen, mn);
    }
    /* Pre-seed zero-arg built-in identifiers so they are never declared as locals */
    impl_seen(&seen, "inkey");
    impl_seen(&seen, "command");
    impl_seen(&seen, "error");
    impl_seen(&seen, "rnd");
    impl_seen(&seen, "errlevel");
    impl_seen(&seen, "termrows");
    impl_seen(&seen, "termcols");
    impl_seen(&seen, "err");
    impl_seen(&seen, "erl");
    /* Pre-seed the enclosing FUNCTION name so it uses <func>_retval instead of a local */
    if (e && e->in_function && e->cur_func_name[0]) {
        impl_seen(&seen, e->cur_func_name);
    }
    /* Pre-seed all global variables from root block so they are never shadowed */
    if (e && e->root) {
        collect_global_dim_names(e->root, &seen);
    }
    collect_dim_names(block, &seen);
    emit_implicit_decls(e, block, &seen);
    /*
     * Hoist local array declarations (BArray descriptors and static C arrays)
     * to the top of the function body.  Must run after emit_implicit_decls so
     * that scalar declarations come first (matching the old ordering) and
     * array declarations follow.  We use a fresh ImplSeen table seeded with
     * all globally-declared array names so we do not re-declare them.
     */
    if (e && e->indent_lvl > 0) {
        for (p = params; p != NULL; p = p->next) {
            mangle_name(p->name, mn, (int)sizeof(mn));
            impl_seen(&arr_seen, mn);
        }
        /* Seed global scalars and arrays so we don't redeclare them locally */
        if (e->root) {
            const AstNode *gs;
            gs = (e->root->kind == ND_BLOCK) ? e->root->u.block.stmts : e->root;
            for (; gs; gs = gs->next) {
                if (gs->kind == ND_DIM || gs->kind == ND_REDIM) {
                    char gmn[128];
                    char gkey[144];
                    mangle_name(gs->u.dim.name, gmn, (int)sizeof(gmn));
                    impl_seen(&arr_seen, gmn);
                    if (gs->u.dim.ndims > 0 && gs->u.dim.is_dynamic) {
                        gkey[0] = '\0';
                        strncat(gkey, "__arr_", 6);
                        strncat(gkey, gmn, 127);
                        impl_seen(&arr_seen, gkey);
                    }
                }
            }
        }
        emit_local_dim_decls(e, block, &arr_seen);
    }
}

static void emit_sub_def(Emitter *e, const AstNode *n)
{
    fprintf(e->out, "\nstatic void %s", sub_name(n->u.sub_def.name));
    emit_param_list(e, n->u.sub_def.params);
    fprintf(e->out, "\n{\n");
    e->indent_lvl = 1;
    e->in_function = 0;
    e->cur_func_name[0] = '\0';
    e->cur_params = n->u.sub_def.params;
    emit_implicit_decls_for_block(e, n->u.sub_def.body, n->u.sub_def.params);
    emit_block(e, n->u.sub_def.body);
    e->cur_params = NULL;
    fprintf(e->out, "}\n");
    e->indent_lvl = 0;
}

static void emit_func_def(Emitter *e, const AstNode *n)
{
    const char *ct;
    char mn[256];
    mangle_name(n->u.sub_def.name, mn, (int)sizeof(mn));

    e->in_function = 1;
    strncpy(e->cur_func_name, mn, sizeof(e->cur_func_name) - 1);
    e->cur_func_name[sizeof(e->cur_func_name) - 1] = '\0';
    e->cur_ret_type = n->u.sub_def.ret_type;

    if (n->u.sub_def.ret_type == BT_STRING) {
        fprintf(e->out, "\nstatic void %s(BString *__retval",
                func_name(n->u.sub_def.name));
        {
            const ParamDef *p;
            for (p = n->u.sub_def.params; p != NULL; p = p->next) {
                const char *pct = ctype_of(p->type, p->type_idx, NULL);
                fprintf(e->out, ", ");
                if (p->is_byref || p->type == BT_STRING) {
                    fprintf(e->out, "%s *%s", pct, vname(p->name));
                } else {
                    fprintf(e->out, "%s %s", pct, vname(p->name));
                }
            }
        }
        fprintf(e->out, ")\n{\n");
        fprintf(e->out, "    BString %s_retval = {NULL,0,0};\n", mn);
    } else {
        ct = ctype_of(n->u.sub_def.ret_type, n->u.sub_def.ret_type_idx, NULL);
        fprintf(e->out, "\nstatic %s %s", ct, func_name(n->u.sub_def.name));
        emit_param_list(e, n->u.sub_def.params);
        fprintf(e->out, "\n{\n");
        fprintf(e->out, "    %s %s_retval = %s;\n",
                ct, mn, czero_of(n->u.sub_def.ret_type));
    }

    e->indent_lvl = 1;
    e->cur_params = n->u.sub_def.params;
    emit_implicit_decls_for_block(e, n->u.sub_def.body, n->u.sub_def.params);
    emit_block(e, n->u.sub_def.body);
    e->cur_params = NULL;

    if (n->u.sub_def.ret_type == BT_STRING) {
        fprintf(e->out, "    bs_assign(__retval, &%s_retval);\n", mn);
        fprintf(e->out, "    bs_free(&%s_retval);\n", mn);
    } else {
        fprintf(e->out, "    return %s_retval;\n", mn);
    }
    fprintf(e->out, "}\n");
    e->indent_lvl = 0;
    e->in_function = 0;
    e->cur_func_name[0] = '\0';
}

/* =========================================================================
 * Top-level module body (everything that is not a SUB/FUNC/TYPE definition)
 * ======================================================================== */

static void emit_module_body(Emitter *e, const AstNode *root)
{
    const AstNode *s;

    fprintf(e->out, "\nstatic void __bas_main__(void)\n{\n");
    e->indent_lvl = 1;
    e->in_function = 0;
    e->cur_func_name[0] = '\0';
    emit_implicit_decls_for_block(e, root, NULL);

    if (root && root->kind == ND_BLOCK) {
        for (s = root->u.block.stmts; s != NULL; s = s->next) {
            /* Skip top-level definitions and DIM statements.
             * SUB/FUNC/TYPE are emitted elsewhere.
             * Global DIM (scalars and arrays) are emitted by
             * emit_global_vars() as C file-scope statics; we must not
             * re-declare them inside __bas_main__.  Dynamic array
             * allocation (bsarray_alloc) is handled by emit_dim when
             * is_dynamic is set -- but since we've moved the declaration
             * to global scope we only need the initialisation call here,
             * which emit_dim already emits inside a block. */
            if (s->kind == ND_SUB_DEF  ||
                s->kind == ND_FUNC_DEF ||
                s->kind == ND_TYPE_DEF) {
                continue;
            }
            if (s->kind == ND_DIM) {
                if (s->u.dim.is_dynamic) {
                    /* Emit dynamic array allocation */
                    int ndims = s->u.dim.ndims;
                    int i;
                    char vn[128];
                    const char *ct = (s->u.dim.var_type == BT_USER)
                        ? ctype_of(s->u.dim.var_type, s->u.dim.type_idx, s)
                        : ctype_of(s->u.dim.var_type, 0, s);
                    mangle_name(s->u.dim.name, vn, (int)sizeof(vn));
                    emit_indent(e);
                    fprintf(e->out, "{\n");
                    e->indent_lvl++;
                    emit_indent(e);
                    fprintf(e->out, "int __lbounds[%d];\n", ndims > 0 ? ndims : 1);
                    emit_indent(e);
                    fprintf(e->out, "int __ubounds[%d];\n", ndims > 0 ? ndims : 1);
                    for (i = 0; i < ndims; i++) {
                        if (s->u.dim.dims[i].lbound) {
                            emit_indent(e);
                            fprintf(e->out, "__lbounds[%d] = (int)(", i);
                            emit_expr(e, s->u.dim.dims[i].lbound);
                            fprintf(e->out, ");\n");
                        } else {
                            emit_line(e, "__lbounds[%d] = 0;", i);
                        }
                        emit_indent(e);
                        fprintf(e->out, "__ubounds[%d] = (int)(", i);
                        emit_expr(e, s->u.dim.dims[i].ubound);
                        fprintf(e->out, ");\n");
                    }
                    emit_line(e, "bsarray_alloc(&__arr_%s, (int)sizeof(%s), %d, __lbounds, __ubounds);",
                              vn, ct, ndims);
                    e->indent_lvl--;
                    emit_indent(e); fprintf(e->out, "}\n");
                }
                continue;
            }
            emit_stmt(e, s);
        }
    }

    e->indent_lvl = 0;
    fprintf(e->out, "}\n");
}

/* =========================================================================
 * Global variable declarations
 * Emit a DIM at global scope for any DIM SHARED in the top-level block.
 * Non-shared top-level DIM statements are emitted inside __bas_main__.
 * ======================================================================== */

static void emit_global_vars(Emitter *e, const AstNode *root)
{
    const AstNode *s;
    if (!root || root->kind != ND_BLOCK) return;

    for (s = root->u.block.stmts; s != NULL; s = s->next) {
        if (s->kind == ND_DIM) {
            const char *vn = vname(s->u.dim.name);
            BasicType   vt = s->u.dim.var_type;
            const char *ct = ctype_of(vt, s->u.dim.type_idx, NULL);

            if (s->u.dim.ndims == 0) {
                /* Scalar global */
                if (vt == BT_STRING) {
                    fprintf(e->out, "static BString %s = {NULL, 0, 0};\n", vn);
                } else {
                    fprintf(e->out, "static %s %s = %s;\n",
                            ct, vn, czero_of(vt));
                }
            } else if (!s->u.dim.is_dynamic) {
                /*
                 * Static array global: emit as a C file-scope array so all
                 * SUBs and FUNCTIONs can see it.  DIM a(N) => a[N+1] (0..N).
                 */
                int i;
                fprintf(e->out, "static %s %s", ct, vn);
                for (i = 0; i < s->u.dim.ndims; i++) {
                    int hi = 0;
                    if (s->u.dim.dims[i].ubound &&
                        s->u.dim.dims[i].ubound->kind == ND_INT_LIT) {
                        hi = s->u.dim.dims[i].ubound->u.int_lit.ival;
                    }
                    fprintf(e->out, "[%d]", hi + 1);
                }
                fprintf(e->out, ";\n");
            } else {
                /*
                 * Dynamic array (BArray): declare the handle globally;
                 * the bsarray_alloc() call is emitted inside __bas_main__.
                 */
                fprintf(e->out, "static BArray __arr_%s;\n", vn);
            }
        }
    }
}

/* =========================================================================
 * File prologue
 * ======================================================================== */

static void emit_prologue(Emitter *e, const AstNode *root)
{
    fprintf(e->out,
        "/* Generated by bcc -- do not edit */\n"
        "#include \"basic_runtime.h\"\n"
        "#include <math.h>\n"
        "#include <setjmp.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "\n"
    );

    /* Helper function prototypes for string temporaries */
    fprintf(e->out, "/* ---- string temporary helpers ---- */\n");
    fprintf(e->out, "static BString *__bas_strlit(int uid, const char *s);\n");
    fprintf(e->out, "static BString *__bas_strcat_tmp(int uid, const BString *a, const BString *b);\n");
    fprintf(e->out, "static BString *__bas_mid_tmp(int uid, const BString *s, int start, int len);\n");
    fprintf(e->out, "static BString *__bas_left_tmp(int uid, const BString *s, int n);\n");
    fprintf(e->out, "static BString *__bas_right_tmp(int uid, const BString *s, int n);\n");
    fprintf(e->out, "static BString *__bas_ucase_tmp(int uid, const BString *s);\n");
    fprintf(e->out, "static BString *__bas_lcase_tmp(int uid, const BString *s);\n");
    fprintf(e->out, "static BString *__bas_ltrim_tmp(int uid, const BString *s);\n");
    fprintf(e->out, "static BString *__bas_rtrim_tmp(int uid, const BString *s);\n");
    fprintf(e->out, "static BString *__bas_trim_tmp(int uid, const BString *s);\n");
    fprintf(e->out, "static BString *__bas_str_tmp(int uid, double v);\n");
    fprintf(e->out, "static BString *__bas_chr_tmp(int uid, int code);\n");
    fprintf(e->out, "static BString *__bas_space_tmp(int uid, int n);\n");
    fprintf(e->out, "static BString *__bas_hex_tmp(int uid, long v);\n");
    fprintf(e->out, "static BString *__bas_oct_tmp(int uid, long v);\n");
    fprintf(e->out, "static BString *__bas_string_fn_tmp(int uid, int n, int ch);\n");
    fprintf(e->out, "static BString *__bas_shell_tmp(int uid, const BString *cmd);\n");
    fprintf(e->out, "static BString *__bas_inkey_tmp(int uid);\n");
    fprintf(e->out, "static BString *__bas_command_tmp(int uid, int idx);\n");
    fprintf(e->out, "static BString *__bas_mki_tmp(int uid, int v);\n");
    fprintf(e->out, "static BString *__bas_mkl_tmp(int uid, long v);\n");
    fprintf(e->out, "static BString *__bas_mks_tmp(int uid, float v);\n");
    fprintf(e->out, "static BString *__bas_mkd_tmp(int uid, double v);\n");
    fprintf(e->out, "static BString *__bas_error_str_tmp(int uid, int code);\n");
    fprintf(e->out, "static long     __bas_idiv(long a, long b, int line);\n");
    fprintf(e->out, "static long     __bas_imod(long a, long b, int line);\n");
    fprintf(e->out, "static double   __bas_fdiv(double a, double b, int line);\n");
    fprintf(e->out, "static void    *__bas_arr_idx(BArray *a, int ndims, ...);\n");
    fprintf(e->out, "\n");

    /* TYPE definitions */
    fprintf(e->out, "/* ---- user TYPE definitions ---- */\n");
    visit_toplevel(e, root, ND_TYPE_DEF, emit_one_typedef);
    fprintf(e->out, "\n");

    /* Global variable declarations */
    fprintf(e->out, "/* ---- global variables ---- */\n");
    emit_global_vars(e, root);
    fprintf(e->out, "\n");

    /* Forward declarations for SUBs and FUNCTIONs */
    fprintf(e->out, "/* ---- forward declarations ---- */\n");
    visit_toplevel(e, root, ND_SUB_DEF,  emit_fwd_decl_sub);
    visit_toplevel(e, root, ND_FUNC_DEF, emit_fwd_decl_func);
    fprintf(e->out, "\n");
}

/* =========================================================================
 * String temporary helper implementations
 * ======================================================================== */

static void emit_string_helpers(Emitter *e)
{
    /* Use a static ring-buffer of BString slots.
     * The uid argument is masked to the pool size to select a slot.
     * This is not thread-safe but matches C89 and HP-UX single-threaded BASIC. */
    fprintf(e->out, "\n");
    fprintf(e->out, "/* ---- string temporary pool ---- */\n");
    fprintf(e->out, "#define __BAS_STRPOOL 256\n");
    fprintf(e->out, "static BString __bas_strpool[__BAS_STRPOOL];\n");
    fprintf(e->out, "\n");
    fprintf(e->out, "static BString *__bas_strlit(int uid, const char *s)\n");
    fprintf(e->out, "{\n");
    fprintf(e->out, "    BString *b = &__bas_strpool[uid %% __BAS_STRPOOL];\n");
    fprintf(e->out, "    bs_assign_cstr(b, s);\n");
    fprintf(e->out, "    return b;\n");
    fprintf(e->out, "}\n");
    fprintf(e->out, "\n");
    fprintf(e->out, "static BString *__bas_strcat_tmp(int uid, const BString *a, const BString *b)\n");
    fprintf(e->out, "{\n");
    fprintf(e->out, "    BString *r = &__bas_strpool[uid %% __BAS_STRPOOL];\n");
    fprintf(e->out, "    bs_concat(r, a, b);\n");
    fprintf(e->out, "    return r;\n");
    fprintf(e->out, "}\n");
    fprintf(e->out, "\n");
    fprintf(e->out, "static BString *__bas_mid_tmp(int uid, const BString *s, int start, int len)\n");
    fprintf(e->out, "{\n");
    fprintf(e->out, "    BString *r = &__bas_strpool[uid %% __BAS_STRPOOL];\n");
    fprintf(e->out, "    bs_mid(r, s, start, len);\n");
    fprintf(e->out, "    return r;\n");
    fprintf(e->out, "}\n");
    fprintf(e->out, "\n");
    fprintf(e->out, "static BString *__bas_left_tmp(int uid, const BString *s, int n)\n");
    fprintf(e->out, "{\n");
    fprintf(e->out, "    BString *r = &__bas_strpool[uid %% __BAS_STRPOOL];\n");
    fprintf(e->out, "    bs_left(r, s, n);\n");
    fprintf(e->out, "    return r;\n");
    fprintf(e->out, "}\n");
    fprintf(e->out, "\n");
    fprintf(e->out, "static BString *__bas_right_tmp(int uid, const BString *s, int n)\n");
    fprintf(e->out, "{\n");
    fprintf(e->out, "    BString *r = &__bas_strpool[uid %% __BAS_STRPOOL];\n");
    fprintf(e->out, "    bs_right(r, s, n);\n");
    fprintf(e->out, "    return r;\n");
    fprintf(e->out, "}\n");
    fprintf(e->out, "\n");
    fprintf(e->out, "static BString *__bas_ucase_tmp(int uid, const BString *s)\n");
    fprintf(e->out, "{\n");
    fprintf(e->out, "    BString *r = &__bas_strpool[uid %% __BAS_STRPOOL];\n");
    fprintf(e->out, "    bs_ucase(r, s);\n");
    fprintf(e->out, "    return r;\n");
    fprintf(e->out, "}\n");
    fprintf(e->out, "\n");
    fprintf(e->out, "static BString *__bas_lcase_tmp(int uid, const BString *s)\n");
    fprintf(e->out, "{\n");
    fprintf(e->out, "    BString *r = &__bas_strpool[uid %% __BAS_STRPOOL];\n");
    fprintf(e->out, "    bs_lcase(r, s);\n");
    fprintf(e->out, "    return r;\n");
    fprintf(e->out, "}\n");
    fprintf(e->out, "\n");
    fprintf(e->out, "static BString *__bas_ltrim_tmp(int uid, const BString *s)\n");
    fprintf(e->out, "{\n");
    fprintf(e->out, "    BString *r = &__bas_strpool[uid %% __BAS_STRPOOL];\n");
    fprintf(e->out, "    bs_ltrim(r, s);\n");
    fprintf(e->out, "    return r;\n");
    fprintf(e->out, "}\n");
    fprintf(e->out, "\n");
    fprintf(e->out, "static BString *__bas_rtrim_tmp(int uid, const BString *s)\n");
    fprintf(e->out, "{\n");
    fprintf(e->out, "    BString *r = &__bas_strpool[uid %% __BAS_STRPOOL];\n");
    fprintf(e->out, "    bs_rtrim(r, s);\n");
    fprintf(e->out, "    return r;\n");
    fprintf(e->out, "}\n");
    fprintf(e->out, "\n");
    fprintf(e->out, "static BString *__bas_trim_tmp(int uid, const BString *s)\n");
    fprintf(e->out, "{\n");
    fprintf(e->out, "    BString *r = &__bas_strpool[uid %% __BAS_STRPOOL];\n");
    fprintf(e->out, "    bs_trim(r, s);\n");
    fprintf(e->out, "    return r;\n");
    fprintf(e->out, "}\n");
    fprintf(e->out, "\n");
    fprintf(e->out, "static BString *__bas_str_tmp(int uid, double v)\n");
    fprintf(e->out, "{\n");
    fprintf(e->out, "    BString *r = &__bas_strpool[uid %% __BAS_STRPOOL];\n");
    fprintf(e->out, "    bs_str(r, v);\n");
    fprintf(e->out, "    return r;\n");
    fprintf(e->out, "}\n");
    fprintf(e->out, "\n");
    fprintf(e->out, "static BString *__bas_chr_tmp(int uid, int code)\n");
    fprintf(e->out, "{\n");
    fprintf(e->out, "    BString *r = &__bas_strpool[uid %% __BAS_STRPOOL];\n");
    fprintf(e->out, "    bs_chr(r, code);\n");
    fprintf(e->out, "    return r;\n");
    fprintf(e->out, "}\n");
    fprintf(e->out, "\n");
    fprintf(e->out, "static BString *__bas_space_tmp(int uid, int n)\n");
    fprintf(e->out, "{\n");
    fprintf(e->out, "    BString *r = &__bas_strpool[uid %% __BAS_STRPOOL];\n");
    fprintf(e->out, "    bs_space(r, n);\n");
    fprintf(e->out, "    return r;\n");
    fprintf(e->out, "}\n");
    fprintf(e->out, "\n");
    fprintf(e->out, "static BString *__bas_hex_tmp(int uid, long v)\n");
    fprintf(e->out, "{\n");
    fprintf(e->out, "    BString *r = &__bas_strpool[uid %% __BAS_STRPOOL];\n");
    fprintf(e->out, "    bs_hex(r, v);\n");
    fprintf(e->out, "    return r;\n");
    fprintf(e->out, "}\n");
    fprintf(e->out, "\n");
    fprintf(e->out, "static BString *__bas_oct_tmp(int uid, long v)\n");
    fprintf(e->out, "{\n");
    fprintf(e->out, "    BString *r = &__bas_strpool[uid %% __BAS_STRPOOL];\n");
    fprintf(e->out, "    bs_oct(r, v);\n");
    fprintf(e->out, "    return r;\n");
    fprintf(e->out, "}\n");
    fprintf(e->out, "\n");
    fprintf(e->out, "static BString *__bas_string_fn_tmp(int uid, int n, int ch)\n");
    fprintf(e->out, "{\n");
    fprintf(e->out, "    BString *r = &__bas_strpool[uid %% __BAS_STRPOOL];\n");
    fprintf(e->out, "    bs_string_fn(r, n, ch);\n");
    fprintf(e->out, "    return r;\n");
    fprintf(e->out, "}\n");
    fprintf(e->out, "\n");
    fprintf(e->out, "static BString *__bas_shell_tmp(int uid, const BString *cmd)\n");
    fprintf(e->out, "{\n");
    fprintf(e->out, "    BString *r = &__bas_strpool[uid %% __BAS_STRPOOL];\n");
    fprintf(e->out, "    bs_shell_str(r, cmd);\n");
    fprintf(e->out, "    return r;\n");
    fprintf(e->out, "}\n");
    fprintf(e->out, "\n");
    fprintf(e->out, "static BString *__bas_inkey_tmp(int uid)\n");
    fprintf(e->out, "{\n");
    fprintf(e->out, "    BString *r = &__bas_strpool[uid %% __BAS_STRPOOL];\n");
    fprintf(e->out, "    bs_inkey(r);\n");
    fprintf(e->out, "    return r;\n");
    fprintf(e->out, "}\n");
    fprintf(e->out, "\n");
    fprintf(e->out, "static BString *__bas_command_tmp(int uid, int idx)\n");
    fprintf(e->out, "{\n");
    fprintf(e->out, "    BString *r = &__bas_strpool[uid %% __BAS_STRPOOL];\n");
    fprintf(e->out, "    bs_command(r, idx);\n");
    fprintf(e->out, "    return r;\n");
    fprintf(e->out, "}\n");
    fprintf(e->out, "\n");
    fprintf(e->out, "static BString *__bas_mki_tmp(int uid, int v)\n");
    fprintf(e->out, "{\n");
    fprintf(e->out, "    BString *r = &__bas_strpool[uid %% __BAS_STRPOOL];\n");
    fprintf(e->out, "    bs_mki(r, v);\n");
    fprintf(e->out, "    return r;\n");
    fprintf(e->out, "}\n");
    fprintf(e->out, "\n");
    fprintf(e->out, "static BString *__bas_mkl_tmp(int uid, long v)\n");
    fprintf(e->out, "{\n");
    fprintf(e->out, "    BString *r = &__bas_strpool[uid %% __BAS_STRPOOL];\n");
    fprintf(e->out, "    bs_mkl(r, v);\n");
    fprintf(e->out, "    return r;\n");
    fprintf(e->out, "}\n");
    fprintf(e->out, "\n");
    fprintf(e->out, "static BString *__bas_mks_tmp(int uid, float v)\n");
    fprintf(e->out, "{\n");
    fprintf(e->out, "    BString *r = &__bas_strpool[uid %% __BAS_STRPOOL];\n");
    fprintf(e->out, "    bs_mks(r, v);\n");
    fprintf(e->out, "    return r;\n");
    fprintf(e->out, "}\n");
    fprintf(e->out, "\n");
    fprintf(e->out, "static BString *__bas_mkd_tmp(int uid, double v)\n");
    fprintf(e->out, "{\n");
    fprintf(e->out, "    BString *r = &__bas_strpool[uid %% __BAS_STRPOOL];\n");
    fprintf(e->out, "    bs_mkd(r, v);\n");
    fprintf(e->out, "    return r;\n");
    fprintf(e->out, "}\n");
    fprintf(e->out, "\n");
    fprintf(e->out, "static BString *__bas_error_str_tmp(int uid, int code)\n");
    fprintf(e->out, "{\n");
    fprintf(e->out, "    BString *r = &__bas_strpool[uid %% __BAS_STRPOOL];\n");
    fprintf(e->out, "    bs_error_str(r, code);\n");
    fprintf(e->out, "    return r;\n");
    fprintf(e->out, "}\n");
    fprintf(e->out, "\n");
    fprintf(e->out, "static long __bas_idiv(long a, long b, int line)\n");
    fprintf(e->out, "{\n");
    fprintf(e->out, "    if (b == 0L) { bs_raise_error(11, line); return 0L; }\n");
    fprintf(e->out, "    return a / b;\n");
    fprintf(e->out, "}\n");
    fprintf(e->out, "\n");
    fprintf(e->out, "static long __bas_imod(long a, long b, int line)\n");
    fprintf(e->out, "{\n");
    fprintf(e->out, "    if (b == 0L) { bs_raise_error(11, line); return 0L; }\n");
    fprintf(e->out, "    return a %% b;\n");
    fprintf(e->out, "}\n");
    fprintf(e->out, "\n");
    fprintf(e->out, "static double __bas_fdiv(double a, double b, int line)\n");
    fprintf(e->out, "{\n");
    fprintf(e->out, "    if (b == 0.0) { bs_raise_error(11, line); return 0.0; }\n");
    fprintf(e->out, "    return a / b;\n");
    fprintf(e->out, "}\n");

    /* Array index helper using variadic args */
    fprintf(e->out,
        "\n"
        "/* variadic array index helper */\n"
        "#include <stdarg.h>\n"
        "static void *__bas_arr_idx(BArray *a, int ndims, ...)\n"
        "{\n"
        "    int __idx[16];\n"
        "    int __i;\n"
        "    va_list __ap;\n"
        "    va_start(__ap, ndims);\n"
        "    for (__i = 0; __i < ndims && __i < 16; __i++)\n"
        "        __idx[__i] = va_arg(__ap, int);\n"
        "    va_end(__ap);\n"
        "    return bsarray_index(a, __idx);\n"
        "}\n"
    );
}

/* =========================================================================
 * File epilogue
 * ======================================================================== */

static void emit_epilogue(Emitter *e)
{
    /* bs_runtime_init / cleanup must be declared somewhere — declare extern */
    fprintf(e->out,
        "\n"
        "/* ---- runtime lifecycle (declared in basic_runtime.h) ---- */\n"
        "void bs_runtime_set_args(int argc, char **argv);\n"
        "void bs_runtime_init(void);\n"
        "void bs_runtime_cleanup(void);\n"
        "\n"
        "int main(int argc, char **argv)\n"
        "{\n"
        "    bs_runtime_set_args(argc, argv);\n"
        "    bs_runtime_init();\n"
        "    __bas_main__();\n"
        "    bs_runtime_cleanup();\n"
        "    return 0;\n"
        "}\n"
    );
}

/* =========================================================================
 * Public entry point
 * ======================================================================== */

int emitter_run(const AstNode *root, const SymTab *st, FILE *out)
{
    Emitter e;
    const AstNode *s;

    if (!out) return -1;

    memset(&e, 0, sizeof(e));
    e.out        = out;
    e.st         = st;
    e.root       = root;
    e.cur_params = NULL;
    e.indent_lvl = 0;
    e.uid        = 0;

    /* 1. File prologue: includes, TYPE defs, global vars, forward decls */
    emit_prologue(&e, root);

    /* 2. String temporary helpers */
    emit_string_helpers(&e);

    /* 3. SUB and FUNCTION definitions */
    if (root && root->kind == ND_BLOCK) {
        for (s = root->u.block.stmts; s != NULL; s = s->next) {
            if (s->kind == ND_SUB_DEF) {
                emit_sub_def(&e, s);
            } else if (s->kind == ND_FUNC_DEF) {
                emit_func_def(&e, s);
            }
        }
    }

    /* 4. Top-level module body */
    emit_module_body(&e, root);

    /* 5. File epilogue: main() */
    emit_epilogue(&e);

    return 0;
}
