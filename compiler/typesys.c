/*
 * typesys.c -- Type-checking / semantic-analysis pass.
 * C89 compatible.
 *
 * DEVELOPMENT ENVIRONMENT NOTE:
 *   Authored on Windows. Target: HP-UX 11i, gcc 4.2, gmake.
 *   Do not attempt to build on the authoring machine.
 *
 * Walk order:
 *   1. pre_scan()       -- register all top-level TYPE, SUB, FUNCTION names
 *   2. walk_node()      -- recursive main walk; annotates every node
 *   3. resolve_expr()   -- sub-walk for expressions; sets node->resolved_type
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "typesys.h"
#include "error.h"
#include "token.h"

/* ------------------------------------------------------------------ */
/* Constants and limits                                                */
/* ------------------------------------------------------------------ */

#define MAX_USER_TYPES  256
#define MSG_BUF_SIZE    256

/* ------------------------------------------------------------------ */
/* Built-in function table                                             */
/* ------------------------------------------------------------------ */

typedef struct BuiltinEntry {
    const char *name;       /* uppercased */
    BasicType   ret_type;
    int         min_args;
    int         max_args;   /* -1 = variadic */
} BuiltinEntry;

static const BuiltinEntry s_builtins[] = {
    /* String functions */
    { "LEN",     BT_INTEGER, 1, 1 },
    { "MID$",    BT_STRING,  2, 3 },
    { "LEFT$",   BT_STRING,  2, 2 },
    { "RIGHT$",  BT_STRING,  2, 2 },
    { "UCASE$",  BT_STRING,  1, 1 },
    { "LCASE$",  BT_STRING,  1, 1 },
    { "LTRIM$",  BT_STRING,  1, 1 },
    { "RTRIM$",  BT_STRING,  1, 1 },
    { "TRIM$",   BT_STRING,  1, 1 },
    { "INSTR",   BT_INTEGER, 2, 3 },
    { "STR$",    BT_STRING,  1, 1 },
    { "VAL",     BT_DOUBLE,  1, 1 },
    { "CHR$",    BT_STRING,  1, 1 },
    { "ASC",     BT_INTEGER, 1, 1 },
    { "SPACE$",  BT_STRING,  1, 1 },
    { "STRING$", BT_STRING,  2, 2 },
    { "HEX$",    BT_STRING,  1, 1 },
    { "OCT$",    BT_STRING,  1, 1 },
    /* Math functions */
    { "SIN",     BT_DOUBLE,  1, 1 },
    { "COS",     BT_DOUBLE,  1, 1 },
    { "TAN",     BT_DOUBLE,  1, 1 },
    { "ATN",     BT_DOUBLE,  1, 1 },
    { "SQR",     BT_DOUBLE,  1, 1 },
    { "LOG",     BT_DOUBLE,  1, 1 },
    { "EXP",     BT_DOUBLE,  1, 1 },
    { "ABS",     BT_DOUBLE,  1, 1 },
    { "INT",     BT_LONG,    1, 1 },
    { "FIX",     BT_LONG,    1, 1 },
    { "RND",     BT_SINGLE,  0, 1 },
    { "SGN",     BT_INTEGER, 1, 1 },
    /* Conversion functions */
    { "CINT",    BT_INTEGER, 1, 1 },
    { "CLNG",    BT_LONG,    1, 1 },
    { "CSNG",    BT_SINGLE,  1, 1 },
    { "CDBL",    BT_DOUBLE,  1, 1 },
    { "MKI$",    BT_STRING,  1, 1 },
    { "MKL$",    BT_STRING,  1, 1 },
    { "MKS$",    BT_STRING,  1, 1 },
    { "MKD$",    BT_STRING,  1, 1 },
    { "CVI",     BT_INTEGER, 1, 1 },
    { "CVL",     BT_LONG,    1, 1 },
    { "CVS",     BT_SINGLE,  1, 1 },
    { "CVD",     BT_DOUBLE,  1, 1 },
    /* I/O state functions */
    { "EOF",     BT_INTEGER, 1, 1 },
    { "LOC",     BT_LONG,    1, 1 },
    { "LOF",     BT_LONG,    1, 1 },
    /* Error functions */
    { "ERR",      BT_INTEGER, 0, 0 },
    { "ERL",      BT_INTEGER, 0, 0 },
    { "ERROR",    BT_STRING,  0, 1 },
    { "ERROR$",   BT_STRING,  0, 1 },
    /* Shell / system functions */
    { "SHELL$",   BT_STRING,  1, 1 },  /* SHELL$(cmd$) -> stdout as STRING */
    { "INKEY$",   BT_STRING,  0, 0 },  /* INKEY$      -> single keystroke  */
    { "COMMAND",  BT_STRING,  0, 1 },  /* COMMAND$[([n])] -> argv string   */
    { "COMMAND$", BT_STRING,  0, 1 },  /* COMMAND$[([n])] -> argv string   */
    { "ERRLEVEL", BT_INTEGER, 0, 0 },  /* exit code from last SHELL        */
    { "TERMROWS", BT_INTEGER, 0, 0 },  /* terminal height in lines/rows    */
    { "TERMCOLS", BT_INTEGER, 0, 0 },  /* terminal width in chars/columns  */
    /* Sentinel */
    { NULL,       BT_VOID,    0, 0 }
};

/* ------------------------------------------------------------------ */
/* TypeSys structure                                                   */
/* ------------------------------------------------------------------ */

struct TypeSys {
    SymTab    *st;                         /* borrowed symbol table          */
    UserType   user_types[MAX_USER_TYPES]; /* registered TYPE records        */
    int        n_user_types;
    const char *filename;                  /* source filename for errors     */
    int        error_count;               /* semantic errors (not from error.h) */
};

/* ------------------------------------------------------------------ */
/* Forward declarations for internal functions                         */
/* ------------------------------------------------------------------ */

static void      pre_scan(TypeSys *t, AstNode *block);
static void      walk_node(TypeSys *t, AstNode *node);
static BasicType resolve_expr(TypeSys *t, AstNode *node);
static int       find_user_type(TypeSys *t, const char *name);
static void      sem_error(TypeSys *t, int line, int col, const char *msg);
static void      sem_warn(TypeSys *t, int line, int col, const char *msg);
static char     *upper_copy(const char *s);
static Symbol   *implicit_declare(TypeSys *t, const char *name, int line, int col);

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

TypeSys *typesys_new(SymTab *st)
{
    TypeSys *t = (TypeSys *)calloc(1, sizeof(TypeSys));
    if (!t) return NULL;
    t->st = st;
    t->filename = "<unknown>";
    return t;
}

void typesys_free(TypeSys *t)
{
    int i;
    if (!t) return;
    for (i = 0; i < t->n_user_types; i++) {
        free(t->user_types[i].name);
        /* fields are owned by the AST -- do not free here */
    }
    free(t);
}

int typesys_check(TypeSys *t, AstNode *program)
{
    int before;
    if (!t || !program) return 0;

    before = error_count();

    /* 1. Pre-scan: register all top-level TYPE, SUB, FUNCTION names */
    pre_scan(t, program);

    /* 2. Main walk */
    walk_node(t, program);

    return error_count() - before;
}

BasicType typesys_expr_type(const AstNode *expr)
{
    if (!expr) return BT_VOID;
    return expr->resolved_type;
}

BasicType typesys_sigil_to_type(char sigil)
{
    switch (sigil) {
    case '%': return BT_INTEGER;
    case '&': return BT_LONG;
    case '!': return BT_SINGLE;
    case '#': return BT_DOUBLE;
    case '$': return BT_STRING;
    default:  return BT_SINGLE;
    }
}

/* ------------------------------------------------------------------ */
/* Utility                                                             */
/* ------------------------------------------------------------------ */

/* Return an uppercased heap copy of s. Caller must free. */
static char *upper_copy(const char *s)
{
    size_t len = strlen(s);
    char  *out = (char *)malloc(len + 1);
    size_t i;
    if (!out) return NULL;
    for (i = 0; i < len; i++) out[i] = (char)toupper((unsigned char)s[i]);
    out[len] = '\0';
    return out;
}

/* Emit a semantic error. */
static void sem_error(TypeSys *t, int line, int col, const char *msg)
{
    error_report(t->filename, line, col, msg);
    t->error_count++;
}

/* Emit a semantic warning. */
static void sem_warn(TypeSys *t, int line, int col, const char *msg)
{
    warn_report(t->filename, line, col, msg);
}

/* Return the index of the user type named 'name' (uppercased), or -1. */
static int find_user_type(TypeSys *t, const char *name)
{
    int i;
    for (i = 0; i < t->n_user_types; i++) {
        if (strcmp(t->user_types[i].name, name) == 0) return i;
    }
    return -1;
}

/* Determine the default type from the last character (sigil) of a name. */
static BasicType type_from_name(const char *name)
{
    size_t len = strlen(name);
    if (len == 0) return BT_SINGLE;
    return typesys_sigil_to_type(name[len - 1]);
}

/* Declare a variable implicitly (BASIC allows undeclared variables).
 * Type is inferred from sigil; BT_SINGLE if no sigil.
 * Issues a warning. */
static Symbol *implicit_declare(TypeSys *t, const char *name, int line, int col)
{
    char    msg[MSG_BUF_SIZE];
    Symbol *sym;
    char   *uname = upper_copy(name);
    Symbol *existing;
    if (!uname) return NULL;

    sprintf(msg, "implicit declaration of '%s'", uname);
    sem_warn(t, line, col, msg);

    sym = make_symbol(uname, SK_VAR, type_from_name(uname));
    free(uname);
    if (!sym) return NULL;

    if (symtab_insert(t->st, sym) != 0) {
        /* Duplicate: look up the already-existing symbol before freeing. */
        existing = symtab_lookup(t->st, sym->name);
        symbol_free(sym);
        return existing;
    }
    return sym;
}

/* Count nodes in a next-linked list. */
static int count_list(AstNode *n)
{
    int c = 0;
    while (n) { c++; n = n->next; }
    return c;
}

/* Look up a built-in by uppercased name. Returns NULL if not found. */
static const BuiltinEntry *find_builtin(const char *name)
{
    const BuiltinEntry *e;
    for (e = s_builtins; e->name; e++) {
        if (strcmp(e->name, name) == 0) return e;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Pre-scan: register TYPE, SUB, FUNCTION at global scope             */
/* ------------------------------------------------------------------ */

static void pre_scan(TypeSys *t, AstNode *block)
{
    AstNode *node;
    AstNode *stmts;

    if (!block) return;

    /* Handle both a bare ND_BLOCK and a direct statement list. */
    stmts = (block->kind == ND_BLOCK) ? block->u.block.stmts : block;

    for (node = stmts; node; node = node->next) {
        if (node->kind == ND_TYPE_DEF) {
            /* Register user-defined TYPE */
            char *uname;
            int   idx;
            Symbol *sym;

            if (t->n_user_types >= MAX_USER_TYPES) {
                sem_error(t, node->line, node->col,
                          "too many user-defined TYPEs");
                continue;
            }
            uname = upper_copy(node->u.type_def.name);
            if (!uname) continue;

            idx = find_user_type(t, uname);
            if (idx >= 0) {
                char msg[MSG_BUF_SIZE];
                sprintf(msg,
                         "TYPE '%s' already defined", uname);
                sem_error(t, node->line, node->col, msg);
                free(uname);
                continue;
            }

            idx = t->n_user_types++;
            t->user_types[idx].name   = uname;  /* owns uname */
            t->user_types[idx].fields = node->u.type_def.fields;

            /* Register a SK_TYPE symbol so expressions can reference it */
            sym = make_symbol(uname, SK_TYPE, BT_USER);
            if (sym) {
                sym->type_idx = idx;
                if (symtab_insert(t->st, sym) != 0) symbol_free(sym);
            }

        } else if (node->kind == ND_SUB_DEF || node->kind == ND_FUNC_DEF) {
            /* Register SUB/FUNCTION signature */
            char   *uname = upper_copy(node->u.sub_def.name);
            Symbol *sym;
            char    msg[MSG_BUF_SIZE];

            if (!uname) continue;

            if (symtab_lookup_local(t->st, uname)) {
                sprintf(msg,
                         "SUB/FUNCTION '%s' already defined", uname);
                sem_error(t, node->line, node->col, msg);
                free(uname);
                continue;
            }

            sym = make_symbol(uname, 
                              node->kind == ND_SUB_DEF ? SK_SUB : SK_FUNCTION,
                              node->kind == ND_SUB_DEF ? BT_VOID
                                                       : node->u.sub_def.ret_type);
            free(uname);
            if (!sym) continue;

            sym->params       = node->u.sub_def.params;
            sym->ret_type     = node->u.sub_def.ret_type;
            sym->ret_type_idx = node->u.sub_def.ret_type_idx;
            sym->type_idx     = node->u.sub_def.ret_type_idx;

            if (symtab_insert(t->st, sym) != 0) symbol_free(sym);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Expression type resolution                                          */
/* ------------------------------------------------------------------ */

/* Promote two numeric types according to BASIC widening rules. */
static BasicType promote_numeric(BasicType a, BasicType b)
{
    if (a == BT_DOUBLE || b == BT_DOUBLE) return BT_DOUBLE;
    if (a == BT_SINGLE  || b == BT_SINGLE)  return BT_SINGLE;
    if (a == BT_LONG    || b == BT_LONG)    return BT_LONG;
    return BT_INTEGER;
}

/* Return 1 if the type is numeric (non-string, non-user, non-void). */
static int is_numeric(BasicType t)
{
    return (t == BT_INTEGER || t == BT_LONG ||
            t == BT_SINGLE  || t == BT_DOUBLE);
}

static BasicType resolve_expr(TypeSys *t, AstNode *node)
{
    BasicType result = BT_SINGLE; /* safe default */

    if (!node) return BT_VOID;

    switch (node->kind) {

    /* -- Literals --------------------------------------------------- */
    case ND_INT_LIT:    result = BT_INTEGER; break;
    case ND_LONG_LIT:   result = BT_LONG;    break;
    case ND_FLOAT_LIT:  result = BT_SINGLE;  break;
    case ND_DOUBLE_LIT: result = BT_DOUBLE;  break;
    case ND_STRING_LIT: result = BT_STRING;  break;

    /* -- Variable reference ----------------------------------------- */
    case ND_VAR: {
        char   *uname = upper_copy(node->u.var.name);
        Symbol *sym   = NULL;
        if (uname) {
            sym = symtab_lookup(t->st, uname);
            if (!sym) {
                /*
                 * Check if this is a zero-arg built-in used without parens
                 * (e.g. ERRLEVEL, INKEY$).  If so, return its declared type
                 * without creating an implicit variable.
                 */
                const BuiltinEntry *bi = find_builtin(uname);
                if (bi && bi->min_args == 0) {
                    result = bi->ret_type;
                    free(uname);
                    break;
                }
                sym = implicit_declare(t, uname, node->line, node->col);
            }
            free(uname);
        }
        if (sym) {
            result        = sym->type;
            node->type_idx = sym->type_idx;
        }
        break;
    }

    /* -- Array index ------------------------------------------------ */
    case ND_ARRAY_INDEX: {
        AstNode *arr     = node->u.array_index.array;
        AstNode *idx     = node->u.array_index.indices;
        char    *uname;
        Symbol  *sym     = NULL;

        /* Resolve index expressions */
        while (idx) { resolve_expr(t, idx); idx = idx->next; }

        /* Get element type from array symbol */
        if (arr && arr->kind == ND_VAR) {
            uname = upper_copy(arr->u.var.name);
            if (uname) {
                sym = symtab_lookup(t->st, uname);
                free(uname);
            }
        }
        if (sym && (sym->kind == SK_ARRAY)) {
            result         = sym->type;
            node->type_idx = sym->type_idx;
        } else if (sym) {
            /* Variable used as array -- resolve its type */
            result = sym->type;
        } else {
            result = BT_SINGLE; /* unknown */
        }
        if (arr) resolve_expr(t, arr);
        break;
    }

    /* -- Field access (record.field) -------------------------------- */
    case ND_FIELD_ACCESS: {
        /* All declarations at top of block (C89). */
        AstNode    *rec      = node->u.field_access.record;
        const char *fn       = node->u.field_access.field;
        BasicType   rec_type;
        int         type_idx;
        char       *ufn      = NULL;
        int         found    = 0;
        ParamDef   *f;
        char        msg[MSG_BUF_SIZE];

        rec_type = resolve_expr(t, rec);
        type_idx = rec->type_idx;

        if (rec_type == BT_USER && type_idx >= 0 &&
            type_idx < t->n_user_types) {
            ufn = upper_copy(fn);
            for (f = t->user_types[type_idx].fields; f; f = f->next) {
                char *uft = upper_copy(f->name);
                if (uft && ufn && strcmp(uft, ufn) == 0) {
                    result         = f->type;
                    node->type_idx = f->type_idx;
                    found          = 1;
                    free(uft);
                    break;
                }
                free(uft);
            }
            if (!found) {
                sprintf(msg,
                         "TYPE '%s' has no field '%s'",
                         t->user_types[type_idx].name, fn);
                sem_error(t, node->line, node->col, msg);
            }
            free(ufn);
        } else if (rec_type != BT_USER) {
            sprintf(msg,
                     "field access on non-TYPE expression");
            sem_error(t, node->line, node->col, msg);
        }
        break;
    }

    /* -- Function call ---------------------------------------------- */
    case ND_FUNC_CALL: {
        /* All declarations at top of block (C89). */
        AstNode            *arg   = node->u.func_call.args;
        char               *uname = upper_copy(node->u.func_call.name);
        Symbol             *sym   = NULL;
        int                 nargs = 0;
        char                msg[MSG_BUF_SIZE];
        const BuiltinEntry *bi    = NULL;
        AstNode            *var_node = NULL;

        /* Resolve argument expressions first */
        while (arg) { resolve_expr(t, arg); arg = arg->next; }
        nargs = count_list(node->u.func_call.args);

        if (uname) {
            /* Check built-in table first */
            bi = find_builtin(uname);
            if (bi) {
                result = bi->ret_type;
                if (bi->max_args != -1 &&
                    (nargs < bi->min_args || nargs > bi->max_args)) {
                    sprintf(msg,
                             "built-in '%s' expects %d-%d arguments, got %d",
                             uname, bi->min_args, bi->max_args, nargs);
                    sem_error(t, node->line, node->col, msg);
                }
            } else {
                sym = symtab_lookup(t->st, uname);
                if (!sym) {
                    sprintf(msg,
                             "undefined function '%s'", uname);
                    sem_error(t, node->line, node->col, msg);
                } else if (sym->kind == SK_ARRAY || (sym->kind == SK_VAR && sym->ndims > 0)) {
                    /*
                     * name(args) where 'name' is an array variable.
                     * Rewrite this AST node from ND_FUNC_CALL to ND_ARRAY_INDEX
                     * so that the emitter treats it identically to LHS array accesses.
                     */
                    char *arr_name = node->u.func_call.name;
                    AstNode *args  = node->u.func_call.args;
                    var_node = make_node(ND_VAR, node->line, node->col);
                    if (var_node) {
                        var_node->u.var.name = arr_name;
                        var_node->resolved_type = sym->type;
                        var_node->type_idx = sym->type_idx;
                        node->kind = ND_ARRAY_INDEX;
                        node->u.array_index.array = var_node;
                        node->u.array_index.indices = args;
                    }
                    result         = sym->type;
                    node->type_idx = sym->type_idx;
                } else if (sym->kind != SK_FUNCTION) {
                    sprintf(msg,
                             "'%s' is not a function", uname);
                    sem_error(t, node->line, node->col, msg);
                } else {
                    int expected;
                    ParamDef *p;
                    result         = sym->ret_type;
                    node->type_idx = sym->ret_type_idx;
                    /* Check argument count */
                    expected = 0;
                    for (p = sym->params; p; p = p->next) expected++;
                    if (nargs != expected) {
                        sprintf(msg,
                                 "function '%s' expects %d argument(s), got %d",
                                 uname, expected, nargs);
                        sem_error(t, node->line, node->col, msg);
                    }
                }
            }
            free(uname);
        }
        break;
    }

    /* -- Binary operator -------------------------------------------- */
    case ND_BINOP: {
        BasicType ltype, rtype;
        int op = node->u.binop.op;

        ltype = resolve_expr(t, node->u.binop.left);
        rtype = resolve_expr(t, node->u.binop.right);

        /* Comparison operators always yield INTEGER (0 or -1 in BASIC) */
        if (op == TOK_EQ  || op == TOK_NEQ ||
            op == TOK_LT  || op == TOK_GT  ||
            op == TOK_LE  || op == TOK_GE) {
            result = BT_INTEGER;
            break;
        }

        /* Logical operators */
        if (op == TOK_AND || op == TOK_OR || op == TOK_XOR) {
            result = BT_INTEGER;
            break;
        }

        /* String concatenation */
        if (op == TOK_AMPERSAND || op == TOK_PLUS) {
            if (ltype == BT_STRING && rtype == BT_STRING) {
                result = BT_STRING;
                break;
            }
        }

        /* Integer division always yields LONG */
        if (op == TOK_BACKSLASH) {
            result = BT_LONG;
            break;
        }

        /* Regular division of any numeric types yields SINGLE */
        if (op == TOK_SLASH) {
            if (ltype == BT_DOUBLE || rtype == BT_DOUBLE)
                result = BT_DOUBLE;
            else
                result = BT_SINGLE;
            break;
        }

        /* MOD: yields the wider integer type */
        if (op == TOK_MOD) {
            if (ltype == BT_LONG || rtype == BT_LONG)
                result = BT_LONG;
            else
                result = BT_INTEGER;
            break;
        }

        /* Exponentiation always yields DOUBLE */
        if (op == TOK_CARET) {
            result = BT_DOUBLE;
            break;
        }

        /* Arithmetic: promote to widest numeric type */
        if (is_numeric(ltype) && is_numeric(rtype)) {
            result = promote_numeric(ltype, rtype);
        } else {
            result = BT_SINGLE; /* fallback */
        }
        break;
    }

    /* -- Unary operator --------------------------------------------- */
    case ND_UNOP: {
        BasicType otype = resolve_expr(t, node->u.binop.left);
        if (node->u.binop.op == TOK_NOT) {
            result = BT_INTEGER;
        } else {
            result = otype;
        }
        break;
    }

    /* -- Cast ------------------------------------------------------- */
    case ND_CAST:
        resolve_expr(t, node->u.cast.expr);
        result = node->u.cast.to_type;
        break;

    default:
        result = BT_SINGLE;
        break;
    }

    node->resolved_type = result;
    return result;
}

/* ------------------------------------------------------------------ */
/* Statement walker                                                    */
/* ------------------------------------------------------------------ */

static void walk_node(TypeSys *t, AstNode *node)
{
    AstNode *cur;

    for (cur = node; cur; cur = cur->next) {
        switch (cur->kind) {

        /* ---- Block ----------------------------------------------- */
        case ND_BLOCK:
            walk_node(t, cur->u.block.stmts);
            break;

        /* ---- SUB / FUNCTION definition --------------------------- */
        case ND_SUB_DEF:
        case ND_FUNC_DEF: {
            ParamDef *p;
            char     *uname = upper_copy(cur->u.sub_def.name);

            symtab_push_scope(t->st);

            /* Insert each parameter as SK_VAR in the new scope */
            for (p = cur->u.sub_def.params; p; p = p->next) {
                char   *pname = upper_copy(p->name);
                Symbol *psym;
                if (!pname) continue;
                psym = make_symbol(pname, SK_VAR, p->type);
                free(pname);
                if (!psym) continue;
                psym->type_idx = p->type_idx;
                psym->is_byref = p->is_byref;
                if (symtab_insert(t->st, psym) != 0) symbol_free(psym);
            }

            /* Note: Do NOT insert a local SK_VAR with the function's name,
               because that would shadow the global SK_FUNCTION symbol and
               break recursive calls (e.g. Factorial(n - 1)).  The global
               SK_FUNCTION symbol already carries sym->type == ret_type so
               LHS return-value assignments (Factorial = 1) resolve cleanly. */

            walk_node(t, cur->u.sub_def.body);
            symtab_pop_scope(t->st);
            free(uname);
            break;
        }

        /* ---- DIM statement --------------------------------------- */
        case ND_DIM: {
            char    *uname = upper_copy(cur->u.dim.name);
            Symbol  *sym;
            int      is_arr = (cur->u.dim.ndims > 0);
            char     msg[MSG_BUF_SIZE];

            if (!uname) break;

            if (symtab_lookup_local(t->st, uname)) {
                sprintf(msg,
                         "'%s' already declared in this scope", uname);
                sem_error(t, cur->line, cur->col, msg);
                free(uname);
                break;
            }

            sym = make_symbol(uname, is_arr ? SK_ARRAY : SK_VAR,
                              cur->u.dim.var_type);
            free(uname);
            if (!sym) break;
            sym->type_idx   = cur->u.dim.type_idx;
            sym->ndims      = cur->u.dim.ndims;
            sym->is_dynamic = cur->u.dim.is_dynamic;

            if (symtab_insert(t->st, sym) != 0) symbol_free(sym);

            /* Walk dimension bound expressions */
            if (cur->u.dim.dims) {
                int i;
                for (i = 0; i < cur->u.dim.ndims; i++) {
                    if (cur->u.dim.dims[i].lbound)
                        resolve_expr(t, cur->u.dim.dims[i].lbound);
                    if (cur->u.dim.dims[i].ubound)
                        resolve_expr(t, cur->u.dim.dims[i].ubound);
                }
            }
            break;
        }

        /* ---- REDIM statement ------------------------------------- */
        case ND_REDIM: {
            char   *uname = upper_copy(cur->u.dim.name);
            Symbol *sym;
            char    msg[MSG_BUF_SIZE];

            if (!uname) break;

            sym = symtab_lookup(t->st, uname);
            free(uname);

            if (!sym) {
                /* REDIM on undeclared: treat as a fresh dynamic array DIM */
                char *uname2 = upper_copy(cur->u.dim.name);
                if (uname2) {
                    Symbol *nsym = make_symbol(uname2, SK_ARRAY,
                                              cur->u.dim.var_type);
                    free(uname2);
                    if (nsym) {
                        nsym->type_idx   = cur->u.dim.type_idx;
                        nsym->ndims      = cur->u.dim.ndims;
                        nsym->is_dynamic = 1;
                        if (symtab_insert(t->st, nsym) != 0) symbol_free(nsym);
                    }
                }
            } else if (sym->kind != SK_ARRAY) {
                sprintf(msg,
                         "REDIM: '%s' is not an array", sym->name);
                sem_error(t, cur->line, cur->col, msg);
            } else if (cur->u.dim.ndims > 0 &&
                       sym->ndims > 0 &&
                       cur->u.dim.ndims != sym->ndims) {
                sprintf(msg,
                         "REDIM: '%s' dimension count changed from %d to %d",
                         sym->name, sym->ndims, cur->u.dim.ndims);
                sem_error(t, cur->line, cur->col, msg);
            } else {
                sym->is_dynamic = 1;
                if (cur->u.dim.ndims > 0) sym->ndims = cur->u.dim.ndims;
            }

            /* Walk bound expressions */
            if (cur->u.dim.dims) {
                int i;
                for (i = 0; i < cur->u.dim.ndims; i++) {
                    if (cur->u.dim.dims[i].lbound)
                        resolve_expr(t, cur->u.dim.dims[i].lbound);
                    if (cur->u.dim.dims[i].ubound)
                        resolve_expr(t, cur->u.dim.dims[i].ubound);
                }
            }
            break;
        }

        /* ---- TYPE definition (body, already pre-scanned) --------- */
        case ND_TYPE_DEF:
            /* Nothing more to do; fields are stored in user_types[]. */
            break;

        /* ---- Assignment / LSET / RSET ---------------------------- */
        case ND_ASSIGN:
        case ND_LSET:
        case ND_RSET: {
            BasicType lhs_type = resolve_expr(t, cur->u.assign.lhs);
            BasicType rhs_type = resolve_expr(t, cur->u.assign.rhs);

            /* Type compatibility check: mismatched string/numeric */
            if ((lhs_type == BT_STRING) != (rhs_type == BT_STRING) &&
                lhs_type != BT_USER && rhs_type != BT_USER &&
                lhs_type != BT_VOID && rhs_type != BT_VOID) {
                char msg[MSG_BUF_SIZE];
                sprintf(msg,
                         "type mismatch in assignment");
                sem_error(t, cur->line, cur->col, msg);
            }
            cur->resolved_type = lhs_type;
            break;
        }

        /* ---- MID$() assignment ----------------------------------- */
        case ND_MID_ASSIGN:
            resolve_expr(t, cur->u.mid_assign.target);
            resolve_expr(t, cur->u.mid_assign.start);
            if (cur->u.mid_assign.length)
                resolve_expr(t, cur->u.mid_assign.length);
            resolve_expr(t, cur->u.mid_assign.value);
            break;

        /* ---- IF -------------------------------------------------- */
        case ND_IF:
            resolve_expr(t, cur->u.if_stmt.cond);
            symtab_push_scope(t->st);
            walk_node(t, cur->u.if_stmt.then_block);
            symtab_pop_scope(t->st);
            if (cur->u.if_stmt.elseif_chain) {
                AstNode *ei = cur->u.if_stmt.elseif_chain;
                while (ei) {
                    resolve_expr(t, ei->u.if_stmt.cond);
                    symtab_push_scope(t->st);
                    walk_node(t, ei->u.if_stmt.then_block);
                    symtab_pop_scope(t->st);
                    ei = ei->next;
                }
            }
            if (cur->u.if_stmt.else_block) {
                symtab_push_scope(t->st);
                walk_node(t, cur->u.if_stmt.else_block);
                symtab_pop_scope(t->st);
            }
            break;

        /* ---- FOR ------------------------------------------------- */
        case ND_FOR: {
            char   *uname = upper_copy(cur->u.for_stmt.var);
            Symbol *vsym  = NULL;
            if (uname) {
                vsym = symtab_lookup(t->st, uname);
                if (!vsym) vsym = implicit_declare(t, uname, cur->line, cur->col);
                free(uname);
            }
            resolve_expr(t, cur->u.for_stmt.init);
            resolve_expr(t, cur->u.for_stmt.limit);
            if (cur->u.for_stmt.step)
                resolve_expr(t, cur->u.for_stmt.step);
            symtab_push_scope(t->st);
            walk_node(t, cur->u.for_stmt.body);
            symtab_pop_scope(t->st);
            (void)vsym;
            break;
        }

        /* ---- WHILE ----------------------------------------------- */
        case ND_WHILE:
            resolve_expr(t, cur->u.while_stmt.cond);
            symtab_push_scope(t->st);
            walk_node(t, cur->u.while_stmt.body);
            symtab_pop_scope(t->st);
            break;

        /* ---- DO -------------------------------------------------- */
        case ND_DO:
            if (cur->u.do_stmt.cond)
                resolve_expr(t, cur->u.do_stmt.cond);
            symtab_push_scope(t->st);
            walk_node(t, cur->u.do_stmt.body);
            symtab_pop_scope(t->st);
            break;

        /* ---- SELECT CASE ----------------------------------------- */
        case ND_SELECT: {
            AstNode *cases;
            AstNode *ci;
            resolve_expr(t, cur->u.select_stmt.expr);
            cases = cur->u.select_stmt.cases;
            while (cases) {
                for (ci = cases->u.if_stmt.cond; ci; ci = ci->next) {
                    resolve_expr(t, ci);
                }
                symtab_push_scope(t->st);
                walk_node(t, cases->u.if_stmt.then_block);
                symtab_pop_scope(t->st);
                cases = cases->next;
            }
            break;
        }

        /* ---- CALL (SUB call) ------------------------------------- */
        case ND_CALL: {
            char    *uname = upper_copy(cur->u.call.name);
            Symbol  *sym;
            AstNode *arg;
            int      nargs = 0;
            char     msg[MSG_BUF_SIZE];

            /* Resolve argument expressions */
            for (arg = cur->u.call.args; arg; arg = arg->next) {
                resolve_expr(t, arg);
                nargs++;
            }

            if (uname) {
                /*
                 * Some built-in statement keywords (LOCATE, COLOR, CLS, SHELL,
                 * PRINT, INPUT, OPEN, CLOSE) may arrive here as ND_CALL nodes
                 * if the parser produced them that way.  Silently accept them --
                 * the emitter handles these node kinds separately.  We just
                 * need to resolve their argument expressions.
                 */
                if (strcmp(uname, "LOCATE")    == 0 ||
                    strcmp(uname, "COLOR")     == 0 ||
                    strcmp(uname, "CLS")       == 0 ||
                    strcmp(uname, "SHELL")     == 0 ||
                    strcmp(uname, "PRINT")     == 0 ||
                    strcmp(uname, "INPUT")     == 0 ||
                    strcmp(uname, "OPEN")      == 0 ||
                    strcmp(uname, "CLOSE")     == 0 ||
                    strcmp(uname, "RANDOMIZE") == 0) {
                    free(uname);
                    break; /* args already resolved above */
                }
                sym = symtab_lookup(t->st, uname);
                if (!sym) {
                    sprintf(msg,
                             "'%s' is not defined -- declare it with SUB %s(...) before calling it",
                             uname, uname);
                    sem_error(t, cur->line, cur->col, msg);
                } else if (sym->kind == SK_FUNCTION) {
                    /* Calling a FUNCTION as a statement -- allowed, return value discarded */
                    int expected = 0;
                    ParamDef *p;
                    for (p = sym->params; p; p = p->next) expected++;
                    if (nargs != expected) {
                        sprintf(msg,
                                 "FUNCTION '%s' expects %d argument(s), got %d",
                                 uname, expected, nargs);
                        sem_error(t, cur->line, cur->col, msg);
                    }
                } else if (sym->kind != SK_SUB) {
                    sprintf(msg,
                             "CALL '%s': '%s' is a %s, not a SUB",
                             uname, uname,
                             sym->kind == SK_VAR   ? "variable" :
                             sym->kind == SK_ARRAY  ? "array"    :
                             sym->kind == SK_TYPE   ? "TYPE"     : "symbol");
                    sem_error(t, cur->line, cur->col, msg);
                } else {
                    /* SK_SUB: check argument count */
                    int expected = 0;
                    ParamDef *p;
                    for (p = sym->params; p; p = p->next) expected++;
                    if (nargs != expected) {
                        sprintf(msg,
                                 "SUB '%s' expects %d argument(s), got %d",
                                 uname, expected, nargs);
                        sem_error(t, cur->line, cur->col, msg);
                    }
                    /*
                     * BYREF check: skip for STRING parameters.
                     * String arguments are always heap-allocated BString
                     * values; passing an expression is fine because the
                     * emitter wraps temporaries correctly.  Only enforce
                     * the lvalue rule for non-string BYREF parameters
                     * (integers, floats, user types).
                     */
                    {
                        int ai = 0;
                        ParamDef *pp;
                        AstNode  *aa = cur->u.call.args;
                        for (pp = sym->params; pp && aa;
                             pp = pp->next, aa = aa->next, ai++) {
                            if (pp->is_byref &&
                                pp->type != BT_STRING &&
                                aa->kind != ND_VAR &&
                                aa->kind != ND_ARRAY_INDEX &&
                                aa->kind != ND_FIELD_ACCESS &&
                                aa->kind != ND_FUNC_CALL) {
                                sprintf(msg,
                                         "argument %d to SUB '%s' is BYREF -- "
                                         "pass a variable, not a literal or expression",
                                         ai + 1, uname);
                                sem_warn(t, aa->line, aa->col, msg);
                            }
                        }
                    }
                }
                free(uname);
            }
            break;
        }

        /* ---- PRINT ----------------------------------------------- */
        case ND_PRINT: {
            AstNode *item = cur->u.print.items;
            while (item) { resolve_expr(t, item); item = item->next; }
            if (cur->u.print.fmt)    resolve_expr(t, cur->u.print.fmt);
            if (cur->u.print.filenum) resolve_expr(t, cur->u.print.filenum);
            break;
        }

        /* ---- INPUT / LINE INPUT ---------------------------------- */
        case ND_INPUT:
        case ND_LINE_INPUT: {
            AstNode *v = cur->u.input.vars;
            while (v) { resolve_expr(t, v); v = v->next; }
            if (cur->u.input.prompt)  resolve_expr(t, cur->u.input.prompt);
            if (cur->u.input.filenum) resolve_expr(t, cur->u.input.filenum);
            break;
        }

        /* ---- OPEN ------------------------------------------------ */
        case ND_OPEN:
            if (cur->u.open.path)    resolve_expr(t, cur->u.open.path);
            if (cur->u.open.filenum) resolve_expr(t, cur->u.open.filenum);
            if (cur->u.open.reclen)  resolve_expr(t, cur->u.open.reclen);
            break;

        /* ---- CLOSE ----------------------------------------------- */
        case ND_CLOSE:
            if (cur->u.close_stmt.filenum)
                resolve_expr(t, cur->u.close_stmt.filenum);
            break;

        /* ---- READ / WRITE / GET / PUT ---------------------------- */
        case ND_READ:
        case ND_WRITE:
        case ND_GET:
        case ND_PUT: {
            AstNode *v = cur->u.file_io.vars;
            if (cur->u.file_io.filenum) resolve_expr(t, cur->u.file_io.filenum);
            if (cur->u.file_io.recno)   resolve_expr(t, cur->u.file_io.recno);
            while (v) { resolve_expr(t, v); v = v->next; }
            break;
        }

        /* ---- FIELD ----------------------------------------------- */
        case ND_FIELD: {
            AstNode *w = cur->u.file_io.recno;
            AstNode *v = cur->u.file_io.vars;
            if (cur->u.file_io.filenum) resolve_expr(t, cur->u.file_io.filenum);
            while (w) { resolve_expr(t, w); w = w->next; }
            while (v) {
                if (v->kind == ND_VAR && v->u.var.name) {
                    char   *uname = upper_copy(v->u.var.name);
                    Symbol *sym   = uname ? symtab_lookup(t->st, uname) : NULL;
                    if (!sym && uname) {
                        sym = make_symbol(uname, SK_VAR, BT_STRING);
                        if (sym && symtab_insert(t->st, sym) != 0)
                            symbol_free(sym);
                    }
                    free(uname);
                }
                resolve_expr(t, v);
                v = v->next;
            }
            break;
        }

        /* ---- TRY / CATCH ----------------------------------------- */
        case ND_TRY: {
            symtab_push_scope(t->st);
            walk_node(t, cur->u.try_stmt.body);
            symtab_pop_scope(t->st);

            symtab_push_scope(t->st);
            /* If there is a named catch variable, declare it as BT_STRING */
            if (cur->u.try_stmt.catch_var) {
                AstNode *cv = cur->u.try_stmt.catch_var;
                if (cv->kind == ND_VAR) {
                    char   *uname = upper_copy(cv->u.var.name);
                    Symbol *csym;
                    if (uname) {
                        csym = make_symbol(uname, SK_VAR, BT_STRING);
                        free(uname);
                        if (csym) {
                            if (symtab_insert(t->st, csym) != 0)
                                symbol_free(csym);
                        }
                    }
                }
            }
            walk_node(t, cur->u.try_stmt.catch_body);
            symtab_pop_scope(t->st);

            if (cur->u.try_stmt.finally_body) {
                symtab_push_scope(t->st);
                walk_node(t, cur->u.try_stmt.finally_body);
                symtab_pop_scope(t->st);
            }
            break;
        }

        /* ---- LOCATE row [, col] ---------------------------------- */
        case ND_LOCATE:
            if (cur->u.locate.row) resolve_expr(t, cur->u.locate.row);
            if (cur->u.locate.col) resolve_expr(t, cur->u.locate.col);
            break;

        /* ---- COLOR fg [, bg] ------------------------------------- */
        case ND_COLOR:
            if (cur->u.color.fg) resolve_expr(t, cur->u.color.fg);
            if (cur->u.color.bg) resolve_expr(t, cur->u.color.bg);
            break;

        /* ---- BGCOLOR bg ------------------------------------------ */
        case ND_BGCOLOR:
            if (cur->u.color.bg) resolve_expr(t, cur->u.color.bg);
            break;

        /* ---- CLS ------------------------------------------------- */
        case ND_CLS:
            break;

        /* ---- SHELL [cmd] ----------------------------------------- */
        case ND_SHELL:
            if (cur->u.shell.cmd) resolve_expr(t, cur->u.shell.cmd);
            break;

        /* ---- LPRINT ---------------------------------------------- */
        case ND_LPRINT: {
            const AstNode *item = cur->u.print.items;
            while (item) { resolve_expr(t, (AstNode *)item); item = item->next; }
            if (cur->u.print.fmt) resolve_expr(t, (AstNode *)cur->u.print.fmt);
            break;
        }

        /* ---- LPRINT OPEN ----------------------------------------- */
        case ND_LPRINT_OPEN:
            if (cur->u.lprint_open.printer)
                resolve_expr(t, (AstNode *)cur->u.lprint_open.printer);
            break;

        /* ---- GOSUB / GOTO / LABEL -------------------------------- */
        case ND_GOSUB:
        case ND_GOTO:
        case ND_LABEL:
        case ND_RETURN:
        case ND_ON_ERROR:
        case ND_RESUME_NEXT:
        case ND_EXIT_SUB:
        case ND_EXIT_FUNC:
        case ND_EXIT_FOR:
        case ND_EXIT_DO:
        case ND_END:
            /* No expressions to resolve. */
            break;

        /* ---- Expression statements (bare expression used as stmt) - */
        default:
            /* If this is an expression node appearing as a statement,
               resolve it so the resolved_type is filled in. */
            if (cur->kind >= ND_BINOP) {
                resolve_expr(t, cur);
            }
            break;
        }
    }
}
