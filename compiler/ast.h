/*
 * ast.h -- Abstract Syntax Tree node types for the BASIC compiler.
 * C89 compatible.  Named union 'u' inside AstNode; fully pedantic-clean.
 */

#ifndef AST_H
#define AST_H

/* Forward declaration */
struct AstNode;

/* ------------------------------------------------------------------ */
/* Node kind enumeration                                               */
/* ------------------------------------------------------------------ */
typedef enum {
    /* -- Statements ------------------------------------------------- */
    ND_ASSIGN,
    ND_IF,
    ND_FOR,
    ND_WHILE,
    ND_DO,
    ND_SELECT,
    ND_GOSUB,
    ND_GOTO,
    ND_RETURN,
    ND_SUB_DEF,
    ND_FUNC_DEF,
    ND_CALL,
    ND_DIM,
    ND_REDIM,
    ND_TYPE_DEF,
    ND_PRINT,
    ND_LPRINT,
    ND_LPRINT_OPEN,
    ND_INPUT,
    ND_LINE_INPUT,
    ND_OPEN,
    ND_CLOSE,
    ND_READ,
    ND_WRITE,
    ND_GET,
    ND_PUT,
    ND_FIELD,
    ND_LSET,
    ND_RSET,
    ND_TRY,
    ND_CATCH,
    ND_ON_ERROR,
    ND_RESUME_NEXT,
    ND_EXIT_SUB,
    ND_EXIT_FOR,
    ND_EXIT_DO,
    ND_EXIT_FUNC,
    ND_SHELL,
    ND_CLS,
    ND_LOCATE,
    ND_COLOR,
    ND_BGCOLOR,
    ND_END,
    ND_LABEL,
    ND_BLOCK,

    /* -- Expressions ------------------------------------------------ */
    ND_BINOP,
    ND_UNOP,
    ND_INT_LIT,
    ND_LONG_LIT,
    ND_FLOAT_LIT,
    ND_DOUBLE_LIT,
    ND_STRING_LIT,
    ND_VAR,
    ND_ARRAY_INDEX,
    ND_FIELD_ACCESS,
    ND_FUNC_CALL,
    ND_CAST,
    ND_MID_ASSIGN,

    ND_KIND_COUNT /* sentinel */
} NodeKind;

/* ------------------------------------------------------------------ */
/* Basic type tags                                                     */
/* ------------------------------------------------------------------ */
typedef enum {
    BT_INTEGER,  /* 16-bit INTEGER */
    BT_LONG,     /* 32-bit LONG    */
    BT_SINGLE,   /* 32-bit SINGLE  */
    BT_DOUBLE,   /* 64-bit DOUBLE  */
    BT_STRING,   /* variable-length STRING */
    BT_USER,     /* user-defined TYPE; resolved via type_idx */
    BT_VOID      /* SUB / no return value */
} BasicType;

/* ------------------------------------------------------------------ */
/* Parameter definition (for SUB / FUNCTION / TYPE fields)            */
/* ------------------------------------------------------------------ */
typedef struct ParamDef {
    char            *name;
    BasicType        type;
    int              type_idx;   /* index into type table when type==BT_USER */
    int              is_byref;   /* 1 = BYREF, 0 = BYVAL */
    int              is_array;   /* 1 = array parameter */
    struct ParamDef *next;
} ParamDef;

/* ------------------------------------------------------------------ */
/* Array dimension specification (for DIM / REDIM)                    */
/* One DimSpec per dimension; stored as a flat array of ndims entries. */
/* ------------------------------------------------------------------ */
typedef struct DimSpec {
    struct AstNode *lbound;  /* NULL => use OPTION BASE default */
    struct AstNode *ubound;
} DimSpec;

/* ------------------------------------------------------------------ */
/* AST node -- flat tagged union                                       */
/* ------------------------------------------------------------------ */
typedef struct AstNode {
    NodeKind        kind;
    int             line;
    int             col;
    BasicType       resolved_type;  /* filled in by semantic pass */
    int             type_idx;       /* for BT_USER resolved types */

    /* Sibling link: used to chain statements in a block, arguments in
       a call list, cases in SELECT, etc.                               */
    struct AstNode *next;

    /* Named union 'u' -- fully C89 / pedantic compliant.
       Access members as node->u.block.stmts, node->u.binop.op, etc.   */
    union {

        /* ND_BLOCK
           The list of statements is chained via the 'next' pointer on
           each statement node; stmts points at the head.               */
        struct {
            struct AstNode *stmts;
        } block;

        /* ND_ASSIGN */
        struct {
            struct AstNode *lhs;
            struct AstNode *rhs;
        } assign;

        /* ND_IF */
        struct {
            struct AstNode *cond;
            struct AstNode *then_block;
            struct AstNode *elseif_chain; /* linked via next */
            struct AstNode *else_block;   /* NULL if absent  */
        } if_stmt;

        /* ND_FOR */
        struct {
            char           *var;    /* loop variable name */
            struct AstNode *init;   /* initial value      */
            struct AstNode *limit;  /* TO value           */
            struct AstNode *step;   /* NULL => implicit 1 */
            struct AstNode *body;
        } for_stmt;

        /* ND_WHILE */
        struct {
            struct AstNode *cond;
            struct AstNode *body;
        } while_stmt;

        /* ND_DO */
        struct {
            struct AstNode *cond;      /* NULL => DO ... LOOP (no condition) */
            struct AstNode *body;
            int             is_until;     /* 1=UNTIL, 0=WHILE */
            int             check_at_top; /* 1=condition at top, 0=at bottom */
        } do_stmt;

        /* ND_SELECT */
        struct {
            struct AstNode *expr;  /* the tested expression */
            struct AstNode *cases; /* linked list of case blocks via next */
        } select_stmt;

        /* ND_GOSUB, ND_GOTO */
        struct {
            char *label;
        } jump;

        /* ND_LABEL */
        struct {
            char *name;
        } label;

        /* ND_SUB_DEF, ND_FUNC_DEF */
        struct {
            char           *name;
            ParamDef       *params;
            BasicType       ret_type;     /* BT_VOID for SUB */
            int             ret_type_idx; /* for BT_USER return types */
            struct AstNode *body;
        } sub_def;

        /* ND_CALL */
        struct {
            char           *name;
            struct AstNode *args; /* linked list via next */
        } call;

        /* ND_DIM, ND_REDIM */
        struct {
            char      *name;
            BasicType  var_type;
            int        type_idx;    /* for BT_USER element types */
            int        is_dynamic;  /* 1 = heap allocation */
            int        preserve;    /* REDIM PRESERVE flag */
            int        ndims;       /* number of entries in dims[] */
            DimSpec   *dims;        /* flat array of ndims DimSpec, or NULL */
        } dim;

        /* ND_TYPE_DEF */
        struct {
            char     *name;
            ParamDef *fields; /* ParamDef reused as field descriptors */
        } type_def;

        /* ND_PRINT, ND_LPRINT */
        struct {
            struct AstNode *items;    /* linked list of print items */
            struct AstNode *fmt;      /* PRINT USING format string, or NULL */
            int             to_file;  /* 1 = PRINT #n */
            struct AstNode *filenum;  /* file-number expression, or NULL */
        } print;

        /* ND_LPRINT_OPEN */
        struct {
            struct AstNode *printer;  /* printer name string expr, or NULL = default */
        } lprint_open;

        /* ND_INPUT, ND_LINE_INPUT */
        struct {
            struct AstNode *vars;           /* linked list of target variables */
            struct AstNode *prompt;         /* prompt string literal, or NULL */
            int             from_file;      /* 1 = INPUT #n */
            struct AstNode *filenum;        /* file-number expression, or NULL */
            int             prompt_semi;    /* 1 = semicolon (show ?), 0 = comma (no ?) */
        } input;

        /* ND_OPEN */
        struct {
            struct AstNode *path;    /* file-path expression */
            int             mode;    /* TokenType: TOK_INPUT/OUTPUT/APPEND/... */
            struct AstNode *filenum; /* #n expression */
            struct AstNode *reclen;  /* record length, or NULL */
        } open;

        /* ND_CLOSE */
        struct {
            struct AstNode *filenum; /* NULL = CLOSE ALL */
        } close_stmt;

        /* ND_READ, ND_WRITE, ND_GET, ND_PUT */
        struct {
            struct AstNode *filenum; /* file-number expression */
            struct AstNode *recno;   /* record number, or NULL */
            struct AstNode *vars;    /* linked list */
        } file_io;

        /* ND_TRY */
        struct {
            struct AstNode *body;
            struct AstNode *catch_var;   /* CATCH variable node, or NULL */
            struct AstNode *catch_body;
            struct AstNode *finally_body; /* NULL if no FINALLY */
        } try_stmt;

        /* ND_ON_ERROR */
        struct {
            char *label;      /* target label; NULL when goto_zero is set */
            int   goto_zero;  /* 1 = ON ERROR GOTO 0 (disable handler) */
        } on_error;

        /* ND_BINOP, ND_UNOP */
        struct {
            int             op;      /* TokenType of operator */
            struct AstNode *left;
            struct AstNode *right;   /* NULL for unary */
        } binop;

        /* ND_INT_LIT */
        struct { int    ival; } int_lit;

        /* ND_LONG_LIT */
        struct { long   lval; } long_lit;

        /* ND_FLOAT_LIT */
        struct { float  fval; } float_lit;

        /* ND_DOUBLE_LIT */
        struct { double dval; } double_lit;

        /* ND_STRING_LIT */
        struct { char  *sval; } string_lit;

        /* ND_VAR */
        struct { char  *name; } var;

        /* ND_ARRAY_INDEX */
        struct {
            struct AstNode *array;   /* the array expression */
            struct AstNode *indices; /* linked list of index expressions */
        } array_index;

        /* ND_FIELD_ACCESS */
        struct {
            struct AstNode *record; /* record/object expression */
            char           *field;  /* member name */
        } field_access;

        /* ND_FUNC_CALL */
        struct {
            char           *name;
            struct AstNode *args; /* linked list via next */
        } func_call;

        /* ND_CAST */
        struct {
            BasicType       to_type; /* target type */
            struct AstNode *expr;
        } cast;

        /* ND_MID_ASSIGN  (MID$(target, start [, length]) = value) */
        struct {
            struct AstNode *target;
            struct AstNode *start;
            struct AstNode *length; /* NULL => replace to end-of-string */
            struct AstNode *value;
        } mid_assign;

        /* ND_SHELL  SHELL "command" */
        struct {
            struct AstNode *cmd; /* command string expression */
        } shell;

        /* ND_LOCATE  LOCATE row, col */
        struct {
            struct AstNode *row;  /* 1-based row expression */
            struct AstNode *col;  /* 1-based column expression; NULL = col unchanged */
        } locate;

        /* ND_COLOR  COLOR foreground[, background] */
        struct {
            struct AstNode *fg;   /* foreground colour expression */
            struct AstNode *bg;   /* background colour expression; NULL = unchanged */
        } color;

    } u; /* end union u */

} AstNode;

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

/*
 * Allocate a zeroed AstNode of the given kind at (line, col).
 * Returns NULL on allocation failure.
 */
AstNode  *make_node(NodeKind kind, int line, int col);

/*
 * Recursively free an entire AST sub-tree, including the sibling chain
 * reachable via node->next.
 */
void      free_ast(AstNode *node);

/*
 * Allocate a ParamDef with a copy of name.
 */
ParamDef *make_param(const char *name, BasicType type, int is_byref);

/*
 * Free a linked list of ParamDef nodes.
 */
void      free_params(ParamDef *p);

/*
 * Debug pretty-printer (used by the --parse flag).
 * Prints the sub-tree rooted at n to stdout, indented by indent spaces.
 */
void      print_ast(AstNode *n, int indent);

/*
 * Portable C89 string duplication helper (since strdup is not in ANSI C89
 * <string.h> under -std=c89 on 64-bit glibc and would otherwise default
 * to returning a 32-bit int, truncating 64-bit pointers).
 */
char     *c89_strdup(const char *s);
#undef strdup
#define strdup c89_strdup

#endif /* AST_H */
