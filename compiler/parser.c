/*
 * parser.c -- BASIC recursive-descent parser.
 * C89 compatible -- no // comments, all variables declared at top of block.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "error.h"

/* =========================================================================
 * Internal parser state
 * ====================================================================== */

/*
 * The lexer provides one-token peek (lexer_peek / lexer_next).
 * We need two-token lookahead for some decisions (e.g. "END IF" vs plain
 * "END", label detection, single-line IF).
 * We maintain a two-slot ring buffer of already-fetched tokens.
 *   buf[0] = current token  (what peek() returns)
 *   buf[1] = next token     (what peek2() returns)
 * When advance() is called we free buf[0], shift buf[1]->buf[0], and
 * fill buf[1] fresh from the lexer.
 */
struct Parser {
    Lexer  *lex;
    Token  *buf[2];   /* two-slot lookahead; buf[0]=current, buf[1]=next */
    int     had_error;
    const char *filename; /* cached from lexer for error messages */
    char   *type_names[64];
    int     n_types;
};

static int register_or_find_type(Parser *p, const char *name)
{
    int i;
    if (!p || !name) return 0;
    for (i = 0; i < p->n_types; i++) {
        if (strcmp(p->type_names[i], name) == 0) return i;
    }
    if (p->n_types < 64) {
        p->type_names[p->n_types] = strdup(name);
        return p->n_types++;
    }
    return 0;
}

/* =========================================================================
 * Infrastructure helpers
 * ====================================================================== */

/* Initialise the two-token buffer. */
static void buf_init(Parser *p)
{
    p->buf[0] = lexer_next(p->lex);
    p->buf[1] = lexer_next(p->lex);
}

/* peek() -- current token (buf[0]), never NULL (may be EOF). */
static Token *peek(Parser *p)
{
    return p->buf[0];
}

/* peek2() -- one-ahead token (buf[1]), never NULL. */
static Token *peek2(Parser *p)
{
    return p->buf[1];
}

/* advance() -- consume current token and slide the window forward.
 * Returns the consumed token (caller must free it). */
static Token *advance(Parser *p)
{
    Token *tok = p->buf[0];
    p->buf[0]  = p->buf[1];
    p->buf[1]  = lexer_next(p->lex);
    return tok;
}

/* syn_error() -- report a syntax error and set had_error. */
static void syn_error(Parser *p, const char *msg)
{
    Token *t = peek(p);
    error_report(p->filename, t->line, t->col, msg);
    p->had_error = 1;
}

/* skip_to_newline() -- error recovery: consume tokens until newline/EOF. */
static void skip_to_newline(Parser *p)
{
    Token *t;
    for (;;) {
        t = peek(p);
        if (t->type == TOK_NEWLINE || t->type == TOK_EOF) break;
        token_free(advance(p));
    }
}

/* expect() -- consume current token if it matches type, else error.
 * Returns the consumed token (caller must free), or NULL on mismatch. */
static Token *expect(Parser *p, TokenType type)
{
    Token *t = peek(p);
    char msg[128];
    if (t->type == type) {
        return advance(p);
    }
    sprintf(msg, "expected %s but got %s",
            token_type_name(type), token_type_name(t->type));
    syn_error(p, msg);
    skip_to_newline(p);
    return NULL;
}

/* match() -- consume and return 1 if current token matches, else return 0. */
static int match(Parser *p, TokenType type)
{
    if (peek(p)->type == type) {
        token_free(advance(p));
        return 1;
    }
    return 0;
}

/* skip_newlines() -- consume all TOK_NEWLINE tokens. */
static void skip_newlines(Parser *p)
{
    while (peek(p)->type == TOK_NEWLINE)
        token_free(advance(p));
}

/* expect_newline() -- expect TOK_NEWLINE or TOK_COLON or TOK_EOF.
 * Consumes the terminator; returns 1 on success, 0 on error. */
static int expect_newline(Parser *p)
{
    TokenType t = peek(p)->type;
    if (t == TOK_NEWLINE || t == TOK_COLON || t == TOK_EOF) {
        if (t != TOK_EOF) token_free(advance(p));
        return 1;
    }
    syn_error(p, "expected end of statement");
    skip_to_newline(p);
    return 0;
}

/* parse_type_tag() -- parse an optional AS <type> clause.
 * Sets *btype and *tidx. Returns 1 if AS was present, 0 otherwise. */
static int parse_type_tag(Parser *p, BasicType *btype, int *tidx)
{
    Token *t;
    if (!match(p, TOK_AS)) return 0;
    t = peek(p);
    *tidx = 0;
    switch (t->type) {
    case TOK_INTEGER:      *btype = BT_INTEGER; token_free(advance(p)); break;
    case TOK_LONG:         *btype = BT_LONG;    token_free(advance(p)); break;
    case TOK_SINGLE:       *btype = BT_SINGLE;  token_free(advance(p)); break;
    case TOK_DOUBLE:       *btype = BT_DOUBLE;  token_free(advance(p)); break;
    case TOK_STRING_TYPE:  *btype = BT_STRING;  token_free(advance(p)); break;
    case TOK_IDENT:
        *btype = BT_USER;
        *tidx  = register_or_find_type(p, t->text);
        token_free(advance(p));
        break;
    default:
        syn_error(p, "expected type name after AS");
        *btype = BT_INTEGER;
        break;
    }
    return 1;
}

/* type_from_sigil() -- infer BasicType from sigil suffix in name.
 * Returns BT_SINGLE (default) if no recognised sigil. */
static BasicType type_from_sigil(const char *name)
{
    int len;
    char last;
    if (!name) return BT_SINGLE;
    len  = (int)strlen(name);
    if (len == 0) return BT_SINGLE;
    last = name[len - 1];
    switch (last) {
    case '%': return BT_INTEGER;
    case '&': return BT_LONG;
    case '!': return BT_SINGLE;
    case '#': return BT_DOUBLE;
    case '$': return BT_STRING;
    default:  return BT_SINGLE; /* no sigil = default SINGLE in BASIC */
    }
}

/* =========================================================================
 * Forward declarations for mutual recursion
 * ====================================================================== */

static AstNode *parse_expr(Parser *p);
static AstNode *parse_statement(Parser *p);
static AstNode *parse_block(Parser *p, int stop1, int stop2, int stop3);

/* =========================================================================
 * Expression parser — recursive descent with explicit precedence levels
 *
 *  parse_expr      -> parse_or
 *  parse_or        -> parse_and (OR parse_and)*
 *                  also handles XOR
 *  parse_and       -> parse_not (AND parse_not)*
 *  parse_not       -> NOT parse_not | parse_comparison
 *  parse_comparison-> parse_concat (op parse_concat)?
 *  parse_concat    -> parse_add (& parse_add)*
 *  parse_add       -> parse_mul ((+|-) parse_mul)*
 *  parse_mul       -> parse_unary ((*|/|\|MOD) parse_unary)*
 *  parse_unary     -> - parse_unary | + parse_unary | parse_power
 *  parse_power     -> parse_postfix (^ parse_power)?  (right-assoc)
 *  parse_postfix   -> parse_primary ( (args) | .field )*
 *  parse_primary   -> literal | ident | (expr) | built-in-func
 * ====================================================================== */

/* Forward declarations for expression levels */
static AstNode *parse_or(Parser *p);
static AstNode *parse_and(Parser *p);
static AstNode *parse_not(Parser *p);
static AstNode *parse_comparison(Parser *p);
static AstNode *parse_concat(Parser *p);
static AstNode *parse_add(Parser *p);
static AstNode *parse_mul(Parser *p);
static AstNode *parse_unary(Parser *p);
static AstNode *parse_power(Parser *p);
static AstNode *parse_postfix(Parser *p);
static AstNode *parse_primary(Parser *p);

/* Helper: build a binary node */
static AstNode *make_binop(int op, AstNode *left, AstNode *right,
                           int line, int col)
{
    AstNode *n = make_node(ND_BINOP, line, col);
    if (!n) return NULL;
    n->u.binop.op    = op;
    n->u.binop.left  = left;
    n->u.binop.right = right;
    return n;
}

/* Helper: parse a parenthesised argument list.
 * Expects '(' already consumed.  Returns linked list via ->next.
 * Stops at ')'. */
static AstNode *parse_arg_list(Parser *p)
{
    AstNode *head;
    AstNode *tail;
    AstNode *arg;
    head = NULL;
    tail = NULL;
    if (peek(p)->type == TOK_RPAREN) {
        token_free(advance(p));
        return NULL;
    }
    for (;;) {
        arg = parse_expr(p);
        if (!arg) {
            free_ast(head);
            return NULL;
        }
        if (!head) { head = arg; tail = arg; }
        else       { tail->next = arg; tail = arg; }
        if (peek(p)->type != TOK_COMMA) break;
        token_free(advance(p)); /* consume comma */
    }
    token_free(expect(p, TOK_RPAREN));
    return head;
}

/* parse_primary */
static AstNode *parse_primary(Parser *p)
{
    Token   *t   = peek(p);
    AstNode *n   = NULL;
    int      line = t->line;
    int      col  = t->col;
    TokenType tt = t->type;

    /* ---- Literals ---- */
    if (tt == TOK_INT_LIT) {
        n = make_node(ND_INT_LIT, line, col);
        if (n) n->u.int_lit.ival = (int)atoi(t->text);
        token_free(advance(p));
        return n;
    }
    if (tt == TOK_LONG_LIT) {
        n = make_node(ND_LONG_LIT, line, col);
        if (n) n->u.long_lit.lval = atol(t->text);
        token_free(advance(p));
        return n;
    }
    if (tt == TOK_FLOAT_LIT) {
        n = make_node(ND_FLOAT_LIT, line, col);
        if (n) n->u.float_lit.fval = (float)atof(t->text);
        token_free(advance(p));
        return n;
    }
    if (tt == TOK_DOUBLE_LIT) {
        n = make_node(ND_DOUBLE_LIT, line, col);
        if (n) n->u.double_lit.dval = atof(t->text);
        token_free(advance(p));
        return n;
    }
    if (tt == TOK_STRING_LIT) {
        n = make_node(ND_STRING_LIT, line, col);
        if (n) n->u.string_lit.sval = strdup(t->text);
        token_free(advance(p));
        return n;
    }

    /* ---- Parenthesised expression ---- */
    if (tt == TOK_LPAREN) {
        AstNode *inner;
        token_free(advance(p)); /* consume '(' */
        inner = parse_expr(p);
        token_free(expect(p, TOK_RPAREN));
        return inner;
    }

    /* ---- Built-in type-cast functions ---- */
    if (tt == TOK_CINT || tt == TOK_CLNG || tt == TOK_CSNG ||
        tt == TOK_CDBL) {
        BasicType bt = BT_INTEGER;
        AstNode *inner;
        token_free(advance(p));
        switch (tt) {
        case TOK_CINT: bt = BT_INTEGER; break;
        case TOK_CLNG: bt = BT_LONG;    break;
        case TOK_CSNG: bt = BT_SINGLE;  break;
        case TOK_CDBL: bt = BT_DOUBLE;  break;
        default: break;
        }
        token_free(expect(p, TOK_LPAREN));
        inner = parse_expr(p);
        token_free(expect(p, TOK_RPAREN));
        n = make_node(ND_CAST, line, col);
        if (n) { n->u.cast.to_type = bt; n->u.cast.expr = inner; }
        return n;
    }

    /* ---- MID$ in expression context ---- */
    if (tt == TOK_MID) {
        /* MID$(str, start [, length]) as expression */
        AstNode *str_expr, *start_expr, *len_expr, *call_n;
        char *name_copy;
        token_free(advance(p)); /* consume MID */
        token_free(expect(p, TOK_LPAREN));
        str_expr   = parse_expr(p);
        token_free(expect(p, TOK_COMMA));
        start_expr = parse_expr(p);
        len_expr   = NULL;
        if (match(p, TOK_COMMA)) len_expr = parse_expr(p);
        token_free(expect(p, TOK_RPAREN));
        /* Build as a FUNC_CALL node named "MID$" */
        call_n = make_node(ND_FUNC_CALL, line, col);
        name_copy = strdup("MID$");
        if (call_n && name_copy) {
            call_n->u.func_call.name = name_copy;
            call_n->u.func_call.args = str_expr;
            if (str_expr) str_expr->next = start_expr;
            if (start_expr) start_expr->next = len_expr;
        }
        return call_n;
    }

    /* ---- Built-in single-arg functions ---- */
    if (tt == TOK_ASC    || tt == TOK_ATN   ||
        tt == TOK_CHR    || tt == TOK_EXP   || tt == TOK_FIX   ||
        tt == TOK_HEX    || tt == TOK_INT_KW|| tt == TOK_LCASE  ||
        tt == TOK_LEFT   || tt == TOK_LEN   || tt == TOK_LOG   ||
        tt == TOK_LTRIM  || tt == TOK_OCT   || tt == TOK_RIGHT  ||
        tt == TOK_RTRIM  || tt == TOK_SGN   || tt == TOK_SIN   ||
        tt == TOK_SPACE  || tt == TOK_SQR   || tt == TOK_STR   ||
        tt == TOK_STRING_KW || tt == TOK_TAN || tt == TOK_TRIM  ||
        tt == TOK_UCASE  || tt == TOK_VAL   || tt == TOK_RND   ||
        tt == TOK_ERL    || tt == TOK_ERR   || tt == TOK_ERROR ||
        tt == TOK_LOC    || tt == TOK_LOF   ||
        tt == TOK_EOF_KW) {
        char *fname = strdup(t->text);
        AstNode *args = NULL;
        token_free(advance(p));
        if (peek(p)->type == TOK_LPAREN) {
            token_free(advance(p));
            args = parse_arg_list(p);
        }
        n = make_node(ND_FUNC_CALL, line, col);
        if (n) { n->u.func_call.name = fname; n->u.func_call.args = args; }
        else free(fname);
        return n;
    }

    /* ---- Identifier: variable, array, or function call ---- */
    if (tt == TOK_IDENT) {
        char *name = strdup(t->text);
        token_free(advance(p));
        /* If followed by '(' it might be a function call or array index --
         * we leave disambiguation to parse_postfix via ND_FUNC_CALL. */
        if (peek(p)->type == TOK_LPAREN) {
            AstNode *args;
            token_free(advance(p)); /* consume '(' */
            args = parse_arg_list(p);
            n = make_node(ND_FUNC_CALL, line, col);
            if (n) { n->u.func_call.name = name; n->u.func_call.args = args; }
            else free(name);
        } else {
            n = make_node(ND_VAR, line, col);
            if (n) n->u.var.name = name;
            else free(name);
        }
        return n;
    }

    /* ---- Unary minus or plus handled in parse_unary; reaching here is
     *      an error. ---- */
    {
        char msg[128];
        sprintf(msg, "unexpected token '%s' in expression",
                t->text ? t->text : token_type_name(t->type));
        syn_error(p, msg);
    }
    return NULL;
}

/* parse_postfix -- handle .field access chaining */
static AstNode *parse_postfix(Parser *p)
{
    AstNode *n = parse_primary(p);
    if (!n) return NULL;
    for (;;) {
        if (peek(p)->type == TOK_DOT) {
            Token *dot = advance(p);
            Token *field_tok;
            AstNode *acc;
            token_free(dot);
            field_tok = peek(p);
            if (field_tok->type != TOK_IDENT) {
                syn_error(p, "expected field name after '.'");
                break;
            }
            acc = make_node(ND_FIELD_ACCESS, n->line, n->col);
            if (!acc) break;
            acc->u.field_access.record = n;
            acc->u.field_access.field  = strdup(field_tok->text);
            token_free(advance(p));
            n = acc;
        } else {
            break;
        }
    }
    return n;
}

/* parse_power (right-associative) */
static AstNode *parse_power(Parser *p)
{
    AstNode *base = parse_postfix(p);
    if (!base) return NULL;
    if (peek(p)->type == TOK_CARET) {
        Token *op = advance(p);
        int line = op->line, col = op->col;
        AstNode *exp_node;
        token_free(op);
        exp_node = parse_power(p); /* right-associative */
        return make_binop(TOK_CARET, base, exp_node, line, col);
    }
    return base;
}

/* parse_unary */
static AstNode *parse_unary(Parser *p)
{
    Token   *t = peek(p);
    AstNode *operand;
    AstNode *n;
    int      line = t->line, col = t->col;
    if (t->type == TOK_MINUS) {
        token_free(advance(p));
        operand = parse_unary(p);
        n = make_node(ND_UNOP, line, col);
        if (n) { n->u.binop.op = TOK_MINUS; n->u.binop.left = operand; }
        return n;
    }
    if (t->type == TOK_PLUS) {
        token_free(advance(p));
        return parse_unary(p); /* unary plus is identity */
    }
    return parse_power(p);
}

/* parse_mul */
static AstNode *parse_mul(Parser *p)
{
    AstNode *left = parse_unary(p);
    if (!left) return NULL;
    for (;;) {
        TokenType tt = peek(p)->type;
        Token *op;
        AstNode *right;
        int line, col;
        if (tt != TOK_STAR && tt != TOK_SLASH &&
            tt != TOK_BACKSLASH && tt != TOK_MOD) break;
        op   = advance(p);
        line = op->line; col = op->col;
        token_free(op);
        right = parse_unary(p);
        left  = make_binop(tt, left, right, line, col);
        if (!left) return NULL;
    }
    return left;
}

/* parse_add */
static AstNode *parse_add(Parser *p)
{
    AstNode *left = parse_mul(p);
    if (!left) return NULL;
    for (;;) {
        TokenType tt = peek(p)->type;
        Token *op;
        AstNode *right;
        int line, col;
        if (tt != TOK_PLUS && tt != TOK_MINUS) break;
        op   = advance(p);
        line = op->line; col = op->col;
        token_free(op);
        right = parse_mul(p);
        left  = make_binop(tt, left, right, line, col);
        if (!left) return NULL;
    }
    return left;
}

/* parse_concat -- string concatenation via & */
static AstNode *parse_concat(Parser *p)
{
    AstNode *left = parse_add(p);
    if (!left) return NULL;
    while (peek(p)->type == TOK_AMPERSAND) {
        Token *op = advance(p);
        int line = op->line, col = op->col;
        AstNode *right;
        token_free(op);
        right = parse_add(p);
        left  = make_binop(TOK_AMPERSAND, left, right, line, col);
        if (!left) return NULL;
    }
    return left;
}

/* parse_comparison */
static AstNode *parse_comparison(Parser *p)
{
    AstNode *left = parse_concat(p);
    TokenType tt;
    Token *op;
    AstNode *right;
    int line, col;
    if (!left) return NULL;
    tt = peek(p)->type;
    if (tt != TOK_EQ && tt != TOK_NEQ && tt != TOK_LT &&
        tt != TOK_GT && tt != TOK_LE  && tt != TOK_GE)
        return left;
    op   = advance(p);
    line = op->line; col = op->col;
    token_free(op);
    right = parse_concat(p);
    return make_binop(tt, left, right, line, col);
}

/* parse_not */
static AstNode *parse_not(Parser *p)
{
    Token   *t = peek(p);
    AstNode *n, *operand;
    int      line = t->line, col = t->col;
    if (t->type == TOK_NOT) {
        token_free(advance(p));
        operand = parse_not(p);
        n = make_node(ND_UNOP, line, col);
        if (n) { n->u.binop.op = TOK_NOT; n->u.binop.left = operand; }
        return n;
    }
    return parse_comparison(p);
}

/* parse_and */
static AstNode *parse_and(Parser *p)
{
    AstNode *left = parse_not(p);
    if (!left) return NULL;
    while (peek(p)->type == TOK_AND) {
        Token *op = advance(p);
        int line = op->line, col = op->col;
        AstNode *right;
        token_free(op);
        right = parse_not(p);
        left  = make_binop(TOK_AND, left, right, line, col);
        if (!left) return NULL;
    }
    return left;
}

/* parse_or -- handles OR and XOR */
static AstNode *parse_or(Parser *p)
{
    AstNode *left = parse_and(p);
    if (!left) return NULL;
    for (;;) {
        TokenType tt = peek(p)->type;
        Token *op;
        AstNode *right;
        int line, col;
        if (tt != TOK_OR && tt != TOK_XOR) break;
        op   = advance(p);
        line = op->line; col = op->col;
        token_free(op);
        right = parse_and(p);
        left  = make_binop(tt, left, right, line, col);
        if (!left) return NULL;
    }
    return left;
}

/* parse_expr -- entry point for expression parsing */
static AstNode *parse_expr(Parser *p)
{
    return parse_or(p);
}

/* =========================================================================
 * Helper: parse_lhs
 * Parses a valid assignment left-hand side:
 *   variable, array element (name(indices)), field access (name.field),
 *   or MID$(var, s[, l]) -- which becomes ND_MID_ASSIGN node construction
 *   is handled by the caller.
 * Returns a VAR, ARRAY_INDEX, FIELD_ACCESS, or FUNC_CALL(MID$) node.
 * ====================================================================== */
static AstNode *parse_lhs(Parser *p)
{
    Token   *t    = peek(p);
    int      line = t->line, col = t->col;
    AstNode *n;

    /* MID$ on left side */
    if (t->type == TOK_MID ||
        (t->type == TOK_IDENT && t->text && strcmp(t->text, "MID$") == 0)) {
        /* Return a special marker: we use ND_FUNC_CALL("MID$") -- the
         * assign dispatcher recognises this and builds ND_MID_ASSIGN. */
        AstNode *str_expr, *start_expr, *len_expr;
        char *fname;
        token_free(advance(p)); /* consume MID */
        token_free(expect(p, TOK_LPAREN));
        str_expr   = parse_expr(p);
        token_free(expect(p, TOK_COMMA));
        start_expr = parse_expr(p);
        len_expr   = NULL;
        if (match(p, TOK_COMMA)) len_expr = parse_expr(p);
        token_free(expect(p, TOK_RPAREN));
        n = make_node(ND_FUNC_CALL, line, col);
        fname = strdup("MID$");
        if (n && fname) {
            n->u.func_call.name = fname;
            n->u.func_call.args = str_expr;
            if (str_expr)   str_expr->next   = start_expr;
            if (start_expr) start_expr->next = len_expr;
        } else {
            free(fname);
            free_ast(n);
            n = NULL;
        }
        return n;
    }

    if (t->type != TOK_IDENT) {
        syn_error(p, "expected variable name on left side of assignment");
        return NULL;
    }
    {
        char *name = strdup(t->text);
        token_free(advance(p));

        if (peek(p)->type == TOK_LPAREN) {
            /* array element */
            AstNode *var_n, *indices;
            token_free(advance(p));
            indices = parse_arg_list(p);
            var_n = make_node(ND_VAR, line, col);
            if (var_n) var_n->u.var.name = name;
            n = make_node(ND_ARRAY_INDEX, line, col);
            if (n) {
                n->u.array_index.array   = var_n;
                n->u.array_index.indices = indices;
            }
        } else if (peek(p)->type == TOK_DOT) {
            /* field access */
            AstNode *var_n;
            Token *field_tok;
            token_free(advance(p)); /* consume '.' */
            field_tok = peek(p);
            if (field_tok->type != TOK_IDENT) {
                syn_error(p, "expected field name after '.'");
                free(name);
                return NULL;
            }
            var_n = make_node(ND_VAR, line, col);
            if (var_n) var_n->u.var.name = name;
            n = make_node(ND_FIELD_ACCESS, line, col);
            if (n) {
                n->u.field_access.record = var_n;
                n->u.field_access.field  = strdup(field_tok->text);
            }
            token_free(advance(p));
        } else {
            n = make_node(ND_VAR, line, col);
            if (n) n->u.var.name = name;
            else free(name);
        }
    }
    return n;
}

/* =========================================================================
 * Statement parsers
 * ====================================================================== */

/* ------------------------------------------------------------------
 * parse_assign_or_call
 * ident followed by '=' -> assignment
 * otherwise -> sub call (ND_CALL)
 * ------------------------------------------------------------------ */
static AstNode *parse_assign_or_call(Parser *p)
{
    Token   *t    = peek(p);
    int      line = t->line, col = t->col;
    AstNode *n;

    /* MID$ assignment: MID$(var, ...) = expr */
    if (t->type == TOK_MID ||
        (t->type == TOK_IDENT && t->text && strcmp(t->text, "MID$") == 0)) {
        AstNode *lhs = parse_lhs(p);
        AstNode *rhs;
        AstNode *mid_n;
        /* lhs is ND_FUNC_CALL("MID$") with args = target, start, [length] */
        token_free(expect(p, TOK_EQ));
        rhs = parse_expr(p);
        mid_n = make_node(ND_MID_ASSIGN, line, col);
        if (mid_n && lhs) {
            /* Unpack the FUNC_CALL args */
            mid_n->u.mid_assign.target = lhs->u.func_call.args;
            if (lhs->u.func_call.args)
                mid_n->u.mid_assign.start  = lhs->u.func_call.args->next;
            if (mid_n->u.mid_assign.start)
                mid_n->u.mid_assign.length = mid_n->u.mid_assign.start->next;
            mid_n->u.mid_assign.value  = rhs;
            /* Disconnect the chain to avoid double-free */
            if (mid_n->u.mid_assign.target)
                mid_n->u.mid_assign.target->next = NULL;
            if (mid_n->u.mid_assign.start)
                mid_n->u.mid_assign.start->next = NULL;
            /* Free the shell FUNC_CALL node (children now owned by mid_n) */
            lhs->u.func_call.args = NULL;
            free(lhs->u.func_call.name);
            lhs->u.func_call.name = NULL;
            free(lhs);
        }
        return mid_n;
    }

    /* Normal ident */
    if (t->type == TOK_IDENT) {
        /* Need to decide: assignment or sub call.
         * Lookahead:
         *   ident =        -> assignment to simple variable
         *   ident (...)  = -> assignment to array element
         *   ident .field = -> assignment to field
         *   otherwise      -> sub call
         * We use peek2() to check what follows the ident. */
        TokenType next_tt = peek2(p)->type;

        if (next_tt == TOK_EQ) {
            /* simple var assignment */
            AstNode *lhs = parse_lhs(p);
            AstNode *rhs;
            token_free(expect(p, TOK_EQ));
            rhs = parse_expr(p);
            n = make_node(ND_ASSIGN, line, col);
            if (n) { n->u.assign.lhs = lhs; n->u.assign.rhs = rhs; }
            return n;
        }

        if (next_tt == TOK_LPAREN || next_tt == TOK_DOT) {
            /* Could be array[...] = expr  OR  field.x = expr
             * OR could be a function call used as a statement.
             * We parse the LHS first, then check for '='. */
            AstNode *lhs = parse_lhs(p);
            if (peek(p)->type == TOK_EQ) {
                AstNode *rhs;
                token_free(advance(p)); /* consume '=' */
                rhs = parse_expr(p);
                n = make_node(ND_ASSIGN, line, col);
                if (n) { n->u.assign.lhs = lhs; n->u.assign.rhs = rhs; }
                return n;
            }
            /* It was a function call used as statement -- 
             * lhs is ND_FUNC_CALL or ND_ARRAY_INDEX; wrap as ND_CALL */
            if (lhs && lhs->kind == ND_FUNC_CALL) {
                /* Re-wrap as ND_CALL */
                n = make_node(ND_CALL, line, col);
                if (n) {
                    n->u.call.name = lhs->u.func_call.name;
                    n->u.call.args = lhs->u.func_call.args;
                    lhs->u.func_call.name = NULL;
                    lhs->u.func_call.args = NULL;
                }
                free(lhs);
                return n;
            }
            /* Fall through: just return lhs as-is (array index stmt is odd
             * but we tolerate it for error recovery) */
            return lhs;
        }

        /* Sub call -- no arguments, or arguments without parens */
        {
            char *name = strdup(t->text);
            AstNode *args = NULL;
            AstNode *arg;
            AstNode *tail = NULL;
            token_free(advance(p)); /* consume ident */
            /* Collect optional argument list (comma-separated, no parens) */
            if (peek(p)->type != TOK_NEWLINE &&
                peek(p)->type != TOK_COLON   &&
                peek(p)->type != TOK_EOF) {
                arg = parse_expr(p);
                if (arg) { args = arg; tail = arg; }
                while (peek(p)->type == TOK_COMMA) {
                    token_free(advance(p));
                    arg = parse_expr(p);
                    if (arg) { tail->next = arg; tail = arg; }
                }
            }
            n = make_node(ND_CALL, line, col);
            if (n) { n->u.call.name = name; n->u.call.args = args; }
            else free(name);
            return n;
        }
    }

    syn_error(p, "expected statement");
    skip_to_newline(p);
    return NULL;
}

/* ------------------------------------------------------------------
 * parse_label  -- TOK_LABEL already peeked
 * ------------------------------------------------------------------ */
static AstNode *parse_label(Parser *p)
{
    Token   *t = peek(p);
    AstNode *n = make_node(ND_LABEL, t->line, t->col);
    if (n) n->u.label.name = strdup(t->text);
    token_free(advance(p)); /* consume TOK_LABEL */
    return n;
}

/* ------------------------------------------------------------------
 * parse_if
 * IF expr THEN
 *   stmts
 * [ELSEIF expr THEN stmts]*
 * [ELSE stmts]
 * END IF
 *
 * Single-line: IF expr THEN stmt  (no NEWLINE between THEN and stmt)
 * ------------------------------------------------------------------ */
static AstNode *parse_if(Parser *p)
{
    Token   *t    = peek(p);
    int      line = t->line, col = t->col;
    AstNode *n;
    AstNode *cond;
    AstNode *then_block;
    AstNode *elseif_head = NULL;
    AstNode *elseif_tail = NULL;
    AstNode *else_block  = NULL;
    int      single_line = 0;

    token_free(advance(p)); /* consume IF */
    cond = parse_expr(p);
    token_free(expect(p, TOK_THEN));

    /* Single-line IF: if next token is NOT a newline, it is a single stmt */
    if (peek(p)->type != TOK_NEWLINE && peek(p)->type != TOK_EOF) {
        single_line = 1;
    }

    if (single_line) {
        AstNode *stmt = parse_statement(p);
        then_block = make_node(ND_BLOCK, line, col);
        if (then_block) {
            then_block->u.block.stmts = stmt;
        }
        /* Optional ELSE on same line */
        if (match(p, TOK_ELSE)) {
            AstNode *else_stmt = parse_statement(p);
            else_block = make_node(ND_BLOCK, line, col);
            if (else_block) else_block->u.block.stmts = else_stmt;
        }
    } else {
        /* Multi-line IF */
        token_free(advance(p)); /* consume NEWLINE */
        then_block = parse_block(p, TOK_END, TOK_ELSEIF, TOK_ELSE);

        /* ELSEIF chain */
        while (peek(p)->type == TOK_ELSEIF) {
            AstNode *ei;
            AstNode *ei_cond;
            AstNode *ei_body;
            token_free(advance(p)); /* consume ELSEIF */
            ei_cond = parse_expr(p);
            token_free(expect(p, TOK_THEN));
            if (peek(p)->type == TOK_NEWLINE) token_free(advance(p));
            ei_body = parse_block(p, TOK_END, TOK_ELSEIF, TOK_ELSE);
            ei = make_node(ND_IF, peek(p)->line, peek(p)->col);
            if (ei) {
                ei->u.if_stmt.cond       = ei_cond;
                ei->u.if_stmt.then_block = ei_body;
            }
            if (!elseif_head) { elseif_head = ei; elseif_tail = ei; }
            else { elseif_tail->next = ei; elseif_tail = ei; }
        }

        /* ELSE clause */
        if (peek(p)->type == TOK_ELSE) {
            token_free(advance(p)); /* consume ELSE */
            if (peek(p)->type == TOK_NEWLINE) token_free(advance(p));
            else_block = parse_block(p, TOK_END, -1, -1);
        }

        /* END IF */
        {
            Token *end_tok = expect(p, TOK_END);
            if (end_tok) token_free(end_tok);
            {
                Token *if_tok = expect(p, TOK_IF);
                if (if_tok) token_free(if_tok);
            }
        }
    }

    n = make_node(ND_IF, line, col);
    if (n) {
        n->u.if_stmt.cond         = cond;
        n->u.if_stmt.then_block   = then_block;
        n->u.if_stmt.elseif_chain = elseif_head;
        n->u.if_stmt.else_block   = else_block;
    }
    return n;
}

/* ------------------------------------------------------------------
 * parse_for
 * FOR var = init TO limit [STEP step]
 *   stmts
 * NEXT [var]
 * ------------------------------------------------------------------ */
static AstNode *parse_for(Parser *p)
{
    Token   *t    = peek(p);
    int      line = t->line, col = t->col;
    char    *var_name;
    AstNode *init_expr, *limit_expr, *step_expr = NULL, *body;
    AstNode *n;
    Token   *var_tok;

    token_free(advance(p)); /* consume FOR */

    var_tok = expect(p, TOK_IDENT);
    var_name = var_tok ? strdup(var_tok->text) : strdup("");
    if (var_tok) token_free(var_tok);

    token_free(expect(p, TOK_EQ));
    init_expr  = parse_expr(p);
    token_free(expect(p, TOK_TO));
    limit_expr = parse_expr(p);
    if (match(p, TOK_STEP)) {
        step_expr = parse_expr(p);
    }
    expect_newline(p);

    body = parse_block(p, TOK_NEXT, -1, -1);
    /* Consume NEXT [var] */
    {
        Token *nxt = expect(p, TOK_NEXT);
        if (nxt) token_free(nxt);
    }
    /* Optional variable after NEXT */
    if (peek(p)->type == TOK_IDENT) token_free(advance(p));

    n = make_node(ND_FOR, line, col);
    if (n) {
        n->u.for_stmt.var   = var_name;
        n->u.for_stmt.init  = init_expr;
        n->u.for_stmt.limit = limit_expr;
        n->u.for_stmt.step  = step_expr;
        n->u.for_stmt.body  = body;
    }
    return n;
}

/* ------------------------------------------------------------------
 * parse_while
 * WHILE expr / stmts / WEND
 * ------------------------------------------------------------------ */
static AstNode *parse_while(Parser *p)
{
    Token   *t    = peek(p);
    int      line = t->line, col = t->col;
    AstNode *cond, *body, *n;

    token_free(advance(p)); /* consume WHILE */
    cond = parse_expr(p);
    expect_newline(p);
    body = parse_block(p, TOK_WEND, -1, -1);
    {
        Token *w = expect(p, TOK_WEND);
        if (w) token_free(w);
    }

    n = make_node(ND_WHILE, line, col);
    if (n) { n->u.while_stmt.cond = cond; n->u.while_stmt.body = body; }
    return n;
}

/* ------------------------------------------------------------------
 * parse_do
 * DO [WHILE|UNTIL expr] / stmts / LOOP [WHILE|UNTIL expr]
 * ------------------------------------------------------------------ */
static AstNode *parse_do(Parser *p)
{
    Token   *t    = peek(p);
    int      line = t->line, col = t->col;
    AstNode *cond = NULL, *body, *n;
    int      is_until     = 0;
    int      check_at_top = 0;

    token_free(advance(p)); /* consume DO */

    if (peek(p)->type == TOK_WHILE || peek(p)->type == TOK_UNTIL) {
        check_at_top = 1;
        is_until     = (peek(p)->type == TOK_UNTIL);
        token_free(advance(p));
        cond = parse_expr(p);
    }
    expect_newline(p);
    body = parse_block(p, TOK_LOOP, -1, -1);
    {
        Token *lp = expect(p, TOK_LOOP);
        if (lp) token_free(lp);
    }
    if (peek(p)->type == TOK_WHILE || peek(p)->type == TOK_UNTIL) {
        if (check_at_top) {
            syn_error(p, "LOOP may not have WHILE/UNTIL if DO already had one");
        }
        is_until = (peek(p)->type == TOK_UNTIL);
        token_free(advance(p));
        cond = parse_expr(p);
    }

    n = make_node(ND_DO, line, col);
    if (n) {
        n->u.do_stmt.cond         = cond;
        n->u.do_stmt.body         = body;
        n->u.do_stmt.is_until     = is_until;
        n->u.do_stmt.check_at_top = check_at_top;
    }
    return n;
}

/* ------------------------------------------------------------------
 * parse_select
 * SELECT CASE expr
 * CASE list / stmts
 * [CASE ELSE stmts]
 * END SELECT
 *
 * Each CASE block is stored as an ND_BLOCK node with an extra field
 * (we reuse the ND_IF node to carry CASE conditions; the ND_IF
 * cond field holds the case-expression list, and then_block holds
 * the body).  Using ND_IF for CASE keeps AST shape simple.
 * ------------------------------------------------------------------ */

/* Parse one CASE clause expression (range, IS compare, single value).
 * Returns a linked list of expressions representing the OR of all items. */
static AstNode *parse_case_expr(Parser *p)
{
    AstNode *head = NULL, *tail = NULL;
    for (;;) {
        AstNode *item;
        item = parse_expr(p);
        if (!item) break;
        /* Range: x TO y  -> store as a BINOP(TOK_TO, x, y) */
        if (peek(p)->type == TOK_TO) {
            AstNode *lo = item;
            AstNode *hi;
            int line = peek(p)->line, col = peek(p)->col;
            token_free(advance(p)); /* consume TO */
            hi = parse_expr(p);
            item = make_binop(TOK_TO, lo, hi, line, col);
        }
        if (!head) { head = item; tail = item; }
        else { tail->next = item; tail = item; }
        if (peek(p)->type != TOK_COMMA) break;
        token_free(advance(p)); /* consume comma */
    }
    return head;
}

static AstNode *parse_select(Parser *p)
{
    Token   *t     = peek(p);
    int      line  = t->line, col = t->col;
    AstNode *sel_n;
    AstNode *expr;
    AstNode *cases_head = NULL, *cases_tail = NULL;

    token_free(advance(p)); /* consume SELECT */
    token_free(expect(p, TOK_CASE));
    expr = parse_expr(p);
    expect_newline(p);
    skip_newlines(p);

    /* Parse CASE clauses */
    while (peek(p)->type == TOK_CASE) {
        AstNode *case_node;
        AstNode *case_cond = NULL;
        AstNode *case_body;
        int is_else = 0;

        token_free(advance(p)); /* consume CASE */

        if (peek(p)->type == TOK_ELSE) {
            token_free(advance(p));
            is_else = 1;
        } else {
            /* Check for IS comparison: IS <op> expr */
            if (peek(p)->type == TOK_IDENT &&
                strcmp(peek(p)->text, "IS") == 0) {
                AstNode *cmp_expr;
                TokenType cmp_op;
                Token *op_tok;
                token_free(advance(p)); /* consume IS */
                op_tok = peek(p);
                cmp_op = op_tok->type;
                if (cmp_op == TOK_EQ  || cmp_op == TOK_NEQ ||
                    cmp_op == TOK_LT  || cmp_op == TOK_GT  ||
                    cmp_op == TOK_LE  || cmp_op == TOK_GE) {
                    int cline = op_tok->line, ccol = op_tok->col;
                    token_free(advance(p));
                    cmp_expr = parse_expr(p);
                    /* Store as BINOP(cmp_op, NULL, cmp_expr) -- NULL left
                     * means "IS <op>" comparison vs. SELECT expr */
                    case_cond = make_binop(cmp_op, NULL, cmp_expr,
                                          cline, ccol);
                } else {
                    syn_error(p, "expected comparison operator after IS");
                }
            } else {
                case_cond = parse_case_expr(p);
            }
        }

        if (peek(p)->type == TOK_NEWLINE) token_free(advance(p));
        case_body = parse_block(p, TOK_CASE, TOK_END, -1);

        /* Represent each CASE as an ND_IF node:
         *   cond       = case_cond (NULL for CASE ELSE)
         *   then_block = case_body */
        case_node = make_node(ND_IF, line, col);
        if (case_node) {
            case_node->u.if_stmt.cond       = case_cond;
            case_node->u.if_stmt.then_block = case_body;
            /* Mark CASE ELSE by setting else_block to a non-NULL sentinel.
             * We use a zero-child ND_BLOCK. */
            if (is_else) {
                case_node->u.if_stmt.else_block =
                    make_node(ND_BLOCK, line, col);
            }
        }
        if (!cases_head) { cases_head = case_node; cases_tail = case_node; }
        else { cases_tail->next = case_node; cases_tail = case_node; }

        skip_newlines(p);
    }

    /* END SELECT */
    {
        Token *end_tok = expect(p, TOK_END);
        if (end_tok) token_free(end_tok);
    }
    {
        Token *sel_tok = expect(p, TOK_SELECT);
        if (sel_tok) token_free(sel_tok);
    }

    sel_n = make_node(ND_SELECT, line, col);
    if (sel_n) {
        sel_n->u.select_stmt.expr  = expr;
        sel_n->u.select_stmt.cases = cases_head;
    }
    return sel_n;
}

/* ------------------------------------------------------------------
 * parse_param_list -- (name [AS type] [, ...])
 * Caller has already consumed the '(' or checked it is present.
 * Returns linked ParamDef list. '(' is consumed here.
 * ------------------------------------------------------------------ */
static ParamDef *parse_param_list(Parser *p)
{
    ParamDef *head = NULL;
    ParamDef *tail = NULL;
    Token    *tok;

    if (peek(p)->type != TOK_LPAREN) return NULL;
    token_free(advance(p)); /* consume '(' */

    if (peek(p)->type == TOK_RPAREN) {
        token_free(advance(p));
        return NULL;
    }

    for (;;) {
        int is_byref = 0; /* default BYVAL for scalar primitives in C emission */
        BasicType btype = BT_SINGLE;
        int tidx = 0;
        int is_array = 0;
        char *pname;
        ParamDef *pd;

        /* Optional BYREF / BYVAL */
        if (peek(p)->type == TOK_BYREF) { is_byref = 1; token_free(advance(p)); }
        else if (peek(p)->type == TOK_BYVAL) { is_byref = 0; token_free(advance(p)); }

        tok = expect(p, TOK_IDENT);
        pname = tok ? strdup(tok->text) : strdup("");
        if (tok) token_free(tok);

        /* Sigil from name */
        btype = type_from_sigil(pname);

        /* Optional empty parens = array parameter */
        if (peek(p)->type == TOK_LPAREN) {
            token_free(advance(p));
            token_free(expect(p, TOK_RPAREN));
            is_array = 1;
        }

        /* Optional AS type */
        parse_type_tag(p, &btype, &tidx);

        pd = make_param(pname, btype, is_byref);
        free(pname);
        if (pd) {
            pd->is_array = is_array;
            pd->type_idx = tidx;
        }
        if (!head) { head = pd; tail = pd; }
        else { tail->next = pd; tail = pd; }

        if (peek(p)->type != TOK_COMMA) break;
        token_free(advance(p)); /* consume comma */
    }
    token_free(expect(p, TOK_RPAREN));
    return head;
}

/* ------------------------------------------------------------------
 * parse_sub_def
 * SUB name [(params)]
 *   stmts
 * END SUB
 * ------------------------------------------------------------------ */
static AstNode *parse_sub_def(Parser *p)
{
    Token    *t    = peek(p);
    int       line = t->line, col = t->col;
    Token    *name_tok;
    char     *name;
    ParamDef *params = NULL;
    AstNode  *body, *n;

    token_free(advance(p)); /* consume SUB */
    name_tok = expect(p, TOK_IDENT);
    name = name_tok ? strdup(name_tok->text) : strdup("");
    if (name_tok) token_free(name_tok);

    if (peek(p)->type == TOK_LPAREN) {
        params = parse_param_list(p);
    }
    expect_newline(p);
    body = parse_block(p, TOK_END, -1, -1);
    {
        Token *end_tok = expect(p, TOK_END);
        if (end_tok) token_free(end_tok);
    }
    {
        Token *sub_tok = expect(p, TOK_SUB);
        if (sub_tok) token_free(sub_tok);
    }

    n = make_node(ND_SUB_DEF, line, col);
    if (n) {
        n->u.sub_def.name     = name;
        n->u.sub_def.params   = params;
        n->u.sub_def.ret_type = BT_VOID;
        n->u.sub_def.body     = body;
    }
    return n;
}

/* ------------------------------------------------------------------
 * parse_function_def
 * FUNCTION name[(params)] [AS type]
 *   stmts
 * END FUNCTION
 * ------------------------------------------------------------------ */
static AstNode *parse_function_def(Parser *p)
{
    Token    *t    = peek(p);
    int       line = t->line, col = t->col;
    Token    *name_tok;
    char     *name;
    ParamDef *params = NULL;
    BasicType ret_type = BT_SINGLE;
    int       ret_type_idx = 0;
    AstNode  *body, *n;

    token_free(advance(p)); /* consume FUNCTION */
    name_tok = expect(p, TOK_IDENT);
    name = name_tok ? strdup(name_tok->text) : strdup("");
    if (name_tok) token_free(name_tok);

    /* Sigil on function name sets return type */
    ret_type = type_from_sigil(name);

    if (peek(p)->type == TOK_LPAREN) {
        params = parse_param_list(p);
    }
    parse_type_tag(p, &ret_type, &ret_type_idx);
    expect_newline(p);
    body = parse_block(p, TOK_END, -1, -1);
    {
        Token *end_tok = expect(p, TOK_END);
        if (end_tok) token_free(end_tok);
    }
    {
        Token *fn_tok = expect(p, TOK_FUNCTION);
        if (fn_tok) token_free(fn_tok);
    }

    n = make_node(ND_FUNC_DEF, line, col);
    if (n) {
        n->u.sub_def.name         = name;
        n->u.sub_def.params       = params;
        n->u.sub_def.ret_type     = ret_type;
        n->u.sub_def.ret_type_idx = ret_type_idx;
        n->u.sub_def.body         = body;
    }
    return n;
}

/* ------------------------------------------------------------------
 * parse_dim
 * DIM [SHARED] name[sigil] [(dims)] [AS type] [, ...]
 * ------------------------------------------------------------------ */
static AstNode *parse_dim_one(Parser *p, int is_redim, int preserve)
{
    Token    *t    = peek(p);
    int       line = t->line, col = t->col;
    Token    *name_tok;
    char     *name;
    BasicType btype;
    int       tidx = 0;
    DimSpec  *dims = NULL;
    int       ndims = 0;
    AstNode  *n;

    name_tok = expect(p, TOK_IDENT);
    name = name_tok ? strdup(name_tok->text) : strdup("");
    if (name_tok) token_free(name_tok);

    btype = type_from_sigil(name);

    /* Optional dimension list */
    if (peek(p)->type == TOK_LPAREN) {
        int cap = 4;
        token_free(advance(p)); /* consume '(' */
        dims = (DimSpec *)malloc((size_t)cap * sizeof(DimSpec));
        ndims = 0;
        if (peek(p)->type != TOK_RPAREN) {
            for (;;) {
                DimSpec ds;
                AstNode *first_expr = parse_expr(p);
                ds.lbound = NULL;
                ds.ubound = first_expr;
                /* Check for lbound TO ubound */
                if (peek(p)->type == TOK_TO) {
                    token_free(advance(p));
                    ds.lbound = first_expr;
                    ds.ubound = parse_expr(p);
                }
                if (ndims >= cap) {
                    cap *= 2;
                    dims = (DimSpec *)realloc(dims, (size_t)cap * sizeof(DimSpec));
                }
                dims[ndims++] = ds;
                if (peek(p)->type != TOK_COMMA) break;
                token_free(advance(p));
            }
        }
        token_free(expect(p, TOK_RPAREN));
    }

    parse_type_tag(p, &btype, &tidx);

    n = make_node(is_redim ? ND_REDIM : ND_DIM, line, col);
    if (n) {
        n->u.dim.name       = name;
        n->u.dim.var_type   = btype;
        n->u.dim.type_idx   = tidx;
        n->u.dim.is_dynamic = (dims != NULL);
        n->u.dim.preserve   = preserve;
        n->u.dim.ndims      = ndims;
        n->u.dim.dims       = dims;
    }
    return n;
}

static AstNode *parse_dim(Parser *p)
{
    AstNode *head = NULL, *tail = NULL;
    AstNode *item;
    token_free(advance(p)); /* consume DIM */
    match(p, TOK_LOCAL);   /* skip LOCAL if present */
    if (peek(p)->type == TOK_IDENT && strcmp(peek(p)->text, "SHARED") == 0) {
        token_free(advance(p)); /* consume SHARED */
    }

    for (;;) {
        item = parse_dim_one(p, 0, 0);
        if (item) {
            if (!head) { head = item; tail = item; }
            else { tail->next = item; tail = item; }
        }
        if (peek(p)->type != TOK_COMMA) break;
        token_free(advance(p));
    }
    return head;
}

static AstNode *parse_redim(Parser *p)
{
    int preserve = 0;
    AstNode *head = NULL, *tail = NULL;
    AstNode *item;

    token_free(advance(p)); /* consume REDIM */
    if (peek(p)->type == TOK_IDENT && strcmp(peek(p)->text, "SHARED") == 0) {
        token_free(advance(p)); /* consume SHARED */
    }
    if (match(p, TOK_PRESERVE)) preserve = 1;
    if (peek(p)->type == TOK_IDENT && strcmp(peek(p)->text, "SHARED") == 0) {
        token_free(advance(p)); /* consume SHARED */
    }

    for (;;) {
        item = parse_dim_one(p, 1, preserve);
        if (item) {
            if (!head) { head = item; tail = item; }
            else { tail->next = item; tail = item; }
        }
        if (peek(p)->type != TOK_COMMA) break;
        token_free(advance(p));
    }
    return head;
}

/* ------------------------------------------------------------------
 * parse_type_def
 * TYPE name
 *   field AS type
 *   ...
 * END TYPE
 * ------------------------------------------------------------------ */
static AstNode *parse_type_def(Parser *p)
{
    Token    *t    = peek(p);
    int       line = t->line, col = t->col;
    Token    *name_tok;
    char     *name;
    ParamDef *fields = NULL, *ftail = NULL;
    AstNode  *n;

    token_free(advance(p)); /* consume TYPE */
    name_tok = expect(p, TOK_IDENT);
    name = name_tok ? strdup(name_tok->text) : strdup("");
    if (name_tok) token_free(name_tok);
    register_or_find_type(p, name);
    expect_newline(p);
    skip_newlines(p);

    while (peek(p)->type != TOK_END && peek(p)->type != TOK_EOF) {
        Token    *fld_tok;
        char     *fld_name;
        BasicType fld_type = BT_SINGLE;
        int       tidx = 0;
        ParamDef *pd;

        fld_tok  = expect(p, TOK_IDENT);
        fld_name = fld_tok ? strdup(fld_tok->text) : strdup("");
        if (fld_tok) token_free(fld_tok);
        fld_type = type_from_sigil(fld_name);
        parse_type_tag(p, &fld_type, &tidx);
        expect_newline(p);
        skip_newlines(p);

        pd = make_param(fld_name, fld_type, 0);
        free(fld_name);
        if (pd) pd->type_idx = tidx;
        if (!fields) { fields = pd; ftail = pd; }
        else { ftail->next = pd; ftail = pd; }
    }

    {
        Token *end_tok = expect(p, TOK_END);
        if (end_tok) token_free(end_tok);
    }
    {
        Token *type_tok = expect(p, TOK_TYPE);
        if (type_tok) token_free(type_tok);
    }

    n = make_node(ND_TYPE_DEF, line, col);
    if (n) {
        n->u.type_def.name   = name;
        n->u.type_def.fields = fields;
    }
    return n;
}

/* ------------------------------------------------------------------
 * parse_print
 * PRINT [#filenum,] [USING fmt;] expr[;|,] ...
 * ------------------------------------------------------------------ */
static AstNode *parse_print(Parser *p)
{
    Token   *t    = peek(p);
    int      line = t->line, col = t->col;
    AstNode *filenum = NULL;
    AstNode *fmt     = NULL;
    AstNode *items   = NULL, *itail = NULL;
    int      to_file = 0;
    AstNode *n;

    token_free(advance(p)); /* consume PRINT */

    /* PRINT #n, */
    if (peek(p)->type == TOK_SIGIL_DOUBLE ||
        (peek(p)->type == TOK_IDENT && peek(p)->text[0] == '#')) {
        to_file = 1;
        token_free(advance(p)); /* consume '#' sigil or ident */
        filenum = parse_expr(p);
        token_free(expect(p, TOK_COMMA));
    }

    /* USING format$ ; */
    if (peek(p)->type == TOK_IDENT &&
        strcmp(peek(p)->text, "USING") == 0) {
        token_free(advance(p));
        fmt = parse_expr(p);
        match(p, TOK_SEMICOLON); /* ; between format and args */
    }

    /* Print items */
    while (peek(p)->type != TOK_NEWLINE &&
           peek(p)->type != TOK_COLON   &&
           peek(p)->type != TOK_EOF) {
        AstNode *item;
        /* Separator tokens are part of the item list: we include them as
         * ND_INT_LIT nodes with value 0=semicolon, 1=comma for simplicity.
         * The emitter interprets them. */
        if (peek(p)->type == TOK_SEMICOLON) {
            item = make_node(ND_INT_LIT, peek(p)->line, peek(p)->col);
            if (item) { item->u.int_lit.ival = 0; item->type_idx = -1; } /* semicolon separator */
            token_free(advance(p));
        } else if (peek(p)->type == TOK_COMMA) {
            item = make_node(ND_INT_LIT, peek(p)->line, peek(p)->col);
            if (item) { item->u.int_lit.ival = 1; item->type_idx = -1; } /* comma/tab separator */
            token_free(advance(p));
        } else {
            item = parse_expr(p);
        }
        if (item) {
            if (!items) { items = item; itail = item; }
            else { itail->next = item; itail = item; }
        }
    }

    n = make_node(ND_PRINT, line, col);
    if (n) {
        n->u.print.items   = items;
        n->u.print.fmt     = fmt;
        n->u.print.to_file = to_file;
        n->u.print.filenum = filenum;
    }
    return n;
}

/* ------------------------------------------------------------------
 * parse_lprint
 * LPRINT [USING fmt;] items...
 * LPRINT OPEN ["printer"]
 * LPRINT CLOSE
 * ------------------------------------------------------------------ */
static AstNode *parse_lprint(Parser *p)
{
    Token   *t    = peek(p);
    int      line = t->line, col = t->col;
    AstNode *fmt   = NULL;
    AstNode *items = NULL, *itail = NULL;
    AstNode *n;

    token_free(advance(p)); /* consume LPRINT */

    /* LPRINT OPEN ["printername"] */
    if (peek(p)->type == TOK_OPEN) {
        token_free(advance(p)); /* consume OPEN */
        n = make_node(ND_LPRINT_OPEN, line, col);
        if (n) {
            if (peek(p)->type == TOK_STRING_LIT) {
                n->u.lprint_open.printer = parse_expr(p);
            } else {
                n->u.lprint_open.printer = NULL; /* use default */
            }
        }
        return n;
    }

    /* LPRINT CLOSE */
    if (peek(p)->type == TOK_CLOSE) {
        token_free(advance(p));
        n = make_node(ND_LPRINT_OPEN, line, col);
        if (n) n->u.lprint_open.printer = NULL;
        /* Reuse LPRINT_OPEN with printer==NULL as "close" signal — emitter
         * distinguishes by checking a separate flag stored in type_idx */
        if (n) n->type_idx = 1; /* 1 = close */
        return n;
    }

    /* LPRINT USING fmt; items */
    if (peek(p)->type == TOK_IDENT &&
        strcmp(peek(p)->text, "USING") == 0) {
        token_free(advance(p));
        fmt = parse_expr(p);
        match(p, TOK_SEMICOLON);
    }

    /* Print items (same logic as PRINT) */
    while (peek(p)->type != TOK_NEWLINE &&
           peek(p)->type != TOK_COLON   &&
           peek(p)->type != TOK_EOF) {
        AstNode *item;
        if (peek(p)->type == TOK_SEMICOLON) {
            item = make_node(ND_INT_LIT, peek(p)->line, peek(p)->col);
            if (item) { item->u.int_lit.ival = 0; item->type_idx = -1; }
            token_free(advance(p));
        } else if (peek(p)->type == TOK_COMMA) {
            item = make_node(ND_INT_LIT, peek(p)->line, peek(p)->col);
            if (item) { item->u.int_lit.ival = 1; item->type_idx = -1; }
            token_free(advance(p));
        } else {
            item = parse_expr(p);
        }
        if (item) {
            if (!items) { items = item; itail = item; }
            else { itail->next = item; itail = item; }
        }
    }

    n = make_node(ND_LPRINT, line, col);
    if (n) {
        n->u.print.items   = items;
        n->u.print.fmt     = fmt;
        n->u.print.to_file = 0;
        n->u.print.filenum = NULL;
    }
    return n;
}

/* ------------------------------------------------------------------
 * parse_input
 * INPUT [#filenum,] ["prompt" {; | ,}] var[, ...]
 *   ; after prompt => show "? " before waiting for input
 *   , after prompt => no "? ", cursor stays on same line
 * ------------------------------------------------------------------ */
static AstNode *parse_input(Parser *p)
{
    Token   *t    = peek(p);
    int      line = t->line, col = t->col;
    AstNode *filenum = NULL;
    AstNode *prompt  = NULL;
    AstNode *vars    = NULL, *vtail = NULL;
    int      from_file  = 0;
    int      prompt_semi = 1; /* default: show ? (semicolon behaviour) */
    AstNode *n;

    token_free(advance(p)); /* consume INPUT */

    /* INPUT #n, */
    if (peek(p)->type == TOK_SIGIL_DOUBLE) {
        from_file = 1;
        token_free(advance(p));
        filenum = parse_expr(p);
        token_free(expect(p, TOK_COMMA));
    }

    /* Optional prompt string followed by ';' or ','
     *   ; => show "? " after prompt
     *   , => no "? " after prompt
     */
    if (!from_file && peek(p)->type == TOK_STRING_LIT) {
        if (peek2(p)->type == TOK_SEMICOLON || peek2(p)->type == TOK_COMMA) {
            prompt = parse_expr(p);
            prompt_semi = (peek(p)->type == TOK_SEMICOLON) ? 1 : 0;
            token_free(advance(p)); /* consume ';' or ',' */
        }
    }

    /* Variable list */
    for (;;) {
        AstNode *var = parse_lhs(p);
        if (var) {
            if (!vars) { vars = var; vtail = var; }
            else { vtail->next = var; vtail = var; }
        }
        if (peek(p)->type != TOK_COMMA) break;
        token_free(advance(p));
    }

    n = make_node(ND_INPUT, line, col);
    if (n) {
        n->u.input.vars        = vars;
        n->u.input.prompt      = prompt;
        n->u.input.from_file   = from_file;
        n->u.input.filenum     = filenum;
        n->u.input.prompt_semi = prompt_semi;
    }
    return n;
}

/* ------------------------------------------------------------------
 * parse_line_input
 * LINE INPUT [#filenum,] ["prompt" {; | ,}] var$
 *   ; after prompt => show "? " after prompt
 *   , after prompt => no "? " after prompt
 * ------------------------------------------------------------------ */
static AstNode *parse_line_input(Parser *p)
{
    Token   *t    = peek(p);
    int      line = t->line, col = t->col;
    AstNode *filenum = NULL;
    AstNode *prompt  = NULL;
    AstNode *var;
    int      from_file   = 0;
    int      prompt_semi = 1;
    AstNode *n;

    token_free(advance(p)); /* consume LINE */
    token_free(expect(p, TOK_INPUT));

    if (peek(p)->type == TOK_SIGIL_DOUBLE) {
        from_file = 1;
        token_free(advance(p));
        filenum = parse_expr(p);
        token_free(expect(p, TOK_COMMA));
    }

    /* Optional prompt string followed by ';' or ',' */
    if (!from_file && peek(p)->type == TOK_STRING_LIT) {
        if (peek2(p)->type == TOK_SEMICOLON || peek2(p)->type == TOK_COMMA) {
            prompt = parse_expr(p);
            prompt_semi = (peek(p)->type == TOK_SEMICOLON) ? 1 : 0;
            token_free(advance(p)); /* consume ';' or ',' */
        }
    }

    var = parse_lhs(p);

    n = make_node(ND_LINE_INPUT, line, col);
    if (n) {
        n->u.input.vars        = var;
        n->u.input.prompt      = prompt;
        n->u.input.from_file   = from_file;
        n->u.input.filenum     = filenum;
        n->u.input.prompt_semi = prompt_semi;
    }
    return n;
}

/* ------------------------------------------------------------------
 * parse_open
 * OPEN path FOR mode AS #filenum [LEN = reclen]
 * mode: INPUT OUTPUT APPEND RANDOM BINARY
 * ------------------------------------------------------------------ */
static AstNode *parse_open(Parser *p)
{
    Token   *t    = peek(p);
    int      line = t->line, col = t->col;
    AstNode *path_expr, *filenum_expr;
    AstNode *reclen_expr = NULL;
    int      mode = TOK_INPUT;
    AstNode *n;

    token_free(advance(p)); /* consume OPEN */
    path_expr = parse_expr(p);
    token_free(expect(p, TOK_FOR));
    {
        Token *mode_tok = peek(p);
        mode = (int)mode_tok->type;
        if (mode_tok->type == TOK_INPUT  || mode_tok->type == TOK_OUTPUT ||
            mode_tok->type == TOK_APPEND || mode_tok->type == TOK_RANDOM ||
            mode_tok->type == TOK_BINARY) {
            token_free(advance(p));
        } else {
            syn_error(p, "expected file mode (INPUT/OUTPUT/APPEND/RANDOM/BINARY)");
        }
    }
    token_free(expect(p, TOK_AS));
    /* Consume '#' if present */
    if (peek(p)->type == TOK_SIGIL_DOUBLE) token_free(advance(p));
    filenum_expr = parse_expr(p);

    if (peek(p)->type == TOK_IDENT &&
        strcmp(peek(p)->text, "LEN") == 0) {
        token_free(advance(p));
        token_free(expect(p, TOK_EQ));
        reclen_expr = parse_expr(p);
    }

    n = make_node(ND_OPEN, line, col);
    if (n) {
        n->u.open.path    = path_expr;
        n->u.open.mode    = mode;
        n->u.open.filenum = filenum_expr;
        n->u.open.reclen  = reclen_expr;
    }
    return n;
}

/* ------------------------------------------------------------------
 * parse_close
 * CLOSE [#filenum[, ...]]
 * ------------------------------------------------------------------ */
static AstNode *parse_close(Parser *p)
{
    Token   *t    = peek(p);
    int      line = t->line, col = t->col;
    AstNode *filenum = NULL;
    AstNode *n;

    token_free(advance(p)); /* consume CLOSE */

    if (peek(p)->type == TOK_SIGIL_DOUBLE) {
        token_free(advance(p));
        filenum = parse_expr(p);
        /* Additional #n args ignored for simplicity */
        while (peek(p)->type == TOK_COMMA) {
            token_free(advance(p));
            if (peek(p)->type == TOK_SIGIL_DOUBLE) token_free(advance(p));
            free_ast(parse_expr(p));
        }
    }

    n = make_node(ND_CLOSE, line, col);
    if (n) n->u.close_stmt.filenum = filenum;
    return n;
}

/* ------------------------------------------------------------------
 * parse_write
 * WRITE #filenum, expr[, ...]
 * ------------------------------------------------------------------ */
static AstNode *parse_write(Parser *p)
{
    Token   *t    = peek(p);
    int      line = t->line, col = t->col;
    AstNode *filenum_expr;
    AstNode *vars = NULL, *vtail = NULL;
    AstNode *n;

    token_free(advance(p)); /* consume WRITE */
    if (peek(p)->type == TOK_SIGIL_DOUBLE) token_free(advance(p));
    filenum_expr = parse_expr(p);
    token_free(expect(p, TOK_COMMA));

    for (;;) {
        AstNode *item = parse_expr(p);
        if (item) {
            if (!vars) { vars = item; vtail = item; }
            else { vtail->next = item; vtail = item; }
        }
        if (peek(p)->type != TOK_COMMA) break;
        token_free(advance(p));
    }

    n = make_node(ND_WRITE, line, col);
    if (n) {
        n->u.file_io.filenum = filenum_expr;
        n->u.file_io.recno   = NULL;
        n->u.file_io.vars    = vars;
    }
    return n;
}

/* ------------------------------------------------------------------
 * parse_read -- READ #filenum [, recno], vars
 * (Note: READ without # is DATA/READ in old BASIC, not implemented here.)
 * ------------------------------------------------------------------ */
static AstNode *parse_read(Parser *p)
{
    Token   *t    = peek(p);
    int      line = t->line, col = t->col;
    AstNode *filenum_expr;
    AstNode *recno_expr = NULL;
    AstNode *vars = NULL, *vtail = NULL;
    AstNode *n;

    token_free(advance(p)); /* consume READ */
    if (peek(p)->type == TOK_SIGIL_DOUBLE) token_free(advance(p));
    filenum_expr = parse_expr(p);

    /* Optional record number */
    if (peek(p)->type == TOK_COMMA) {
        token_free(advance(p));
        /* Check: is this a recno or a variable? Assume first is recno. */
        recno_expr = parse_expr(p);
    }
    /* Remaining: variable list */
    while (peek(p)->type == TOK_COMMA) {
        AstNode *v;
        token_free(advance(p));
        v = parse_lhs(p);
        if (v) {
            if (!vars) { vars = v; vtail = v; }
            else { vtail->next = v; vtail = v; }
        }
    }

    n = make_node(ND_READ, line, col);
    if (n) {
        n->u.file_io.filenum = filenum_expr;
        n->u.file_io.recno   = recno_expr;
        n->u.file_io.vars    = vars;
    }
    return n;
}

/* ------------------------------------------------------------------
 * parse_get
 * GET #filenum [, recno], var
 * ------------------------------------------------------------------ */
static AstNode *parse_get(Parser *p)
{
    Token   *t    = peek(p);
    int      line = t->line, col = t->col;
    AstNode *filenum_expr;
    AstNode *recno_expr = NULL;
    AstNode *var = NULL;
    AstNode *n;

    token_free(advance(p)); /* consume GET */
    if (peek(p)->type == TOK_SIGIL_DOUBLE) token_free(advance(p));
    filenum_expr = parse_expr(p);

    if (peek(p)->type == TOK_COMMA) {
        token_free(advance(p));
        if (peek(p)->type != TOK_COMMA &&
            peek(p)->type != TOK_NEWLINE &&
            peek(p)->type != TOK_COLON &&
            peek(p)->type != TOK_EOF) {
            recno_expr = parse_expr(p);
        }
    }
    if (peek(p)->type == TOK_COMMA) {
        token_free(advance(p));
        var = parse_lhs(p);
    }

    n = make_node(ND_GET, line, col);
    if (n) {
        n->u.file_io.filenum = filenum_expr;
        n->u.file_io.recno   = recno_expr;
        n->u.file_io.vars    = var;
    }
    return n;
}

/* ------------------------------------------------------------------
 * parse_put
 * PUT #filenum [, recno] [, expr]
 * ------------------------------------------------------------------ */
static AstNode *parse_put(Parser *p)
{
    Token   *t    = peek(p);
    int      line = t->line, col = t->col;
    AstNode *filenum_expr;
    AstNode *recno_expr = NULL;
    AstNode *val = NULL;
    AstNode *n;

    token_free(advance(p)); /* consume PUT */
    if (peek(p)->type == TOK_SIGIL_DOUBLE) token_free(advance(p));
    filenum_expr = parse_expr(p);

    if (peek(p)->type == TOK_COMMA) {
        token_free(advance(p));
        if (peek(p)->type != TOK_COMMA &&
            peek(p)->type != TOK_NEWLINE &&
            peek(p)->type != TOK_COLON &&
            peek(p)->type != TOK_EOF) {
            recno_expr = parse_expr(p);
        }
    }
    if (peek(p)->type == TOK_COMMA) {
        token_free(advance(p));
        val = parse_expr(p);
    }

    n = make_node(ND_PUT, line, col);
    if (n) {
        n->u.file_io.filenum = filenum_expr;
        n->u.file_io.recno   = recno_expr;
        n->u.file_io.vars    = val;
    }
    return n;
}

/* ------------------------------------------------------------------
 * parse_field
 * FIELD [#]filenum, width AS var$ [, width AS var$, ...]
 * ------------------------------------------------------------------ */
static AstNode *parse_field(Parser *p)
{
    Token   *t    = peek(p);
    int      line = t->line, col = t->col;
    AstNode *filenum_expr;
    AstNode *whead = NULL, *wtail = NULL;
    AstNode *vhead = NULL, *vtail = NULL;
    AstNode *n;

    token_free(advance(p)); /* consume FIELD */
    if (peek(p)->type == TOK_SIGIL_DOUBLE) token_free(advance(p));
    filenum_expr = parse_expr(p);
    token_free(expect(p, TOK_COMMA));

    for (;;) {
        AstNode *w = parse_expr(p);
        AstNode *v;
        token_free(expect(p, TOK_AS));
        v = parse_lhs(p);
        if (w) {
            if (!whead) { whead = w; wtail = w; }
            else { wtail->next = w; wtail = w; }
        }
        if (v) {
            if (!vhead) { vhead = v; vtail = v; }
            else { vtail->next = v; vtail = v; }
        }
        if (peek(p)->type != TOK_COMMA) break;
        token_free(advance(p));
    }

    n = make_node(ND_FIELD, line, col);
    if (n) {
        n->u.file_io.filenum = filenum_expr;
        n->u.file_io.recno   = whead; /* linked list of width expressions */
        n->u.file_io.vars    = vhead; /* linked list of target string vars */
    }
    return n;
}

/* ------------------------------------------------------------------
 * parse_lset_rset
 * LSET var$ = expr$   or   RSET var$ = expr$
 * ------------------------------------------------------------------ */
static AstNode *parse_lset_rset(Parser *p, int is_rset)
{
    Token   *t    = peek(p);
    int      line = t->line, col = t->col;
    AstNode *lhs;
    AstNode *rhs;
    AstNode *n;

    token_free(advance(p)); /* consume LSET or RSET */
    lhs = parse_lhs(p);
    token_free(expect(p, TOK_EQ));
    rhs = parse_expr(p);

    n = make_node(is_rset ? ND_RSET : ND_LSET, line, col);
    if (n) {
        n->u.assign.lhs = lhs;
        n->u.assign.rhs = rhs;
    }
    return n;
}

/* ------------------------------------------------------------------
 * parse_try
 * TRY / stmts / CATCH [var AS STRING] / stmts / END TRY
 * ------------------------------------------------------------------ */
static AstNode *parse_try(Parser *p)
{
    Token   *t    = peek(p);
    int      line = t->line, col = t->col;
    AstNode *body, *catch_var = NULL, *catch_body = NULL;
    AstNode *n;

    token_free(advance(p)); /* consume TRY */
    expect_newline(p);
    body = parse_block(p, TOK_CATCH, TOK_END, -1);

    if (peek(p)->type == TOK_CATCH) {
        token_free(advance(p)); /* consume CATCH */
        /* Optional: var AS STRING */
        if (peek(p)->type == TOK_IDENT &&
            (peek2(p)->type == TOK_AS || peek2(p)->type == TOK_NEWLINE)) {
            if (peek2(p)->type == TOK_AS) {
                Token  *vt = peek(p);
                AstNode *cv = make_node(ND_VAR, vt->line, vt->col);
                if (cv) cv->u.var.name = strdup(vt->text);
                token_free(advance(p)); /* consume var name */
                token_free(advance(p)); /* consume AS */
                /* expect STRING */
                if (peek(p)->type == TOK_STRING_TYPE)
                    token_free(advance(p));
                catch_var = cv;
            }
        }
        if (peek(p)->type == TOK_NEWLINE) token_free(advance(p));
        catch_body = parse_block(p, TOK_END, -1, -1);
    }

    {
        Token *end_tok = expect(p, TOK_END);
        if (end_tok) token_free(end_tok);
    }
    {
        Token *try_tok = expect(p, TOK_TRY);
        if (try_tok) token_free(try_tok);
    }

    n = make_node(ND_TRY, line, col);
    if (n) {
        n->u.try_stmt.body         = body;
        n->u.try_stmt.catch_var    = catch_var;
        n->u.try_stmt.catch_body   = catch_body;
        n->u.try_stmt.finally_body = NULL;
    }
    return n;
}

/* ------------------------------------------------------------------
 * parse_on_error
 * ON ERROR GOTO label | ON ERROR GOTO 0
 * ------------------------------------------------------------------ */
static AstNode *parse_on_error(Parser *p)
{
    Token   *t    = peek(p);
    int      line = t->line, col = t->col;
    char    *label = NULL;
    int      goto_zero = 0;
    AstNode *n;

    token_free(advance(p)); /* consume ON */
    token_free(expect(p, TOK_ERROR));
    token_free(expect(p, TOK_GOTO));

    if (peek(p)->type == TOK_INT_LIT &&
        strcmp(peek(p)->text, "0") == 0) {
        goto_zero = 1;
        token_free(advance(p));
    } else {
        Token *lbl = peek(p);
        if (lbl->type == TOK_IDENT || lbl->type == TOK_LABEL) {
            label = strdup(lbl->text);
            token_free(advance(p));
        } else {
            syn_error(p, "expected label after ON ERROR GOTO");
        }
    }

    n = make_node(ND_ON_ERROR, line, col);
    if (n) {
        n->u.on_error.label     = label;
        n->u.on_error.goto_zero = goto_zero;
    }
    return n;
}

/* ------------------------------------------------------------------
 * parse_resume_next -- RESUME NEXT
 * ------------------------------------------------------------------ */
static AstNode *parse_resume_next(Parser *p)
{
    Token   *t    = peek(p);
    int      line = t->line, col = t->col;
    AstNode *n;

    token_free(advance(p)); /* consume RESUME */
    if (peek(p)->type == TOK_NEXT) {
        token_free(advance(p));
    } else {
        syn_error(p, "expected NEXT after RESUME");
    }
    n = make_node(ND_RESUME_NEXT, line, col);
    return n;
}

/* ------------------------------------------------------------------
 * parse_gosub / parse_goto / parse_return
 * ------------------------------------------------------------------ */
static AstNode *parse_gosub(Parser *p)
{
    Token   *t    = peek(p);
    int      line = t->line, col = t->col;
    char    *label;
    Token   *lbl_tok;
    AstNode *n;

    token_free(advance(p)); /* consume GOSUB */
    lbl_tok = peek(p);
    label = (lbl_tok->type == TOK_IDENT || lbl_tok->type == TOK_LABEL)
            ? strdup(lbl_tok->text) : strdup("");
    token_free(advance(p));

    n = make_node(ND_GOSUB, line, col);
    if (n) n->u.jump.label = label;
    return n;
}

static AstNode *parse_goto(Parser *p)
{
    Token   *t    = peek(p);
    int      line = t->line, col = t->col;
    char    *label;
    Token   *lbl_tok;
    AstNode *n;

    token_free(advance(p)); /* consume GOTO */
    lbl_tok = peek(p);
    label = (lbl_tok->type == TOK_IDENT || lbl_tok->type == TOK_LABEL ||
             lbl_tok->type == TOK_INT_LIT)
            ? strdup(lbl_tok->text) : strdup("");
    token_free(advance(p));

    n = make_node(ND_GOTO, line, col);
    if (n) n->u.jump.label = label;
    return n;
}

static AstNode *parse_return(Parser *p)
{
    Token   *t = peek(p);
    AstNode *n = make_node(ND_RETURN, t->line, t->col);
    token_free(advance(p)); /* consume RETURN */
    return n;
}

/* ------------------------------------------------------------------
 * parse_exit -- EXIT SUB | EXIT FUNCTION | EXIT FOR | EXIT DO
 * ------------------------------------------------------------------ */
static AstNode *parse_exit(Parser *p)
{
    Token   *t    = peek(p);
    int      line = t->line, col = t->col;
    NodeKind kind = ND_EXIT_SUB;
    AstNode *n;

    token_free(advance(p)); /* consume EXIT */
    switch (peek(p)->type) {
    case TOK_SUB:      kind = ND_EXIT_SUB;  token_free(advance(p)); break;
    case TOK_FUNCTION: kind = ND_EXIT_FUNC; token_free(advance(p)); break;
    case TOK_FOR:      kind = ND_EXIT_FOR;  token_free(advance(p)); break;
    case TOK_DO:       kind = ND_EXIT_DO;   token_free(advance(p)); break;
    default:
        syn_error(p, "expected SUB, FUNCTION, FOR, or DO after EXIT");
        break;
    }
    n = make_node(kind, line, col);
    return n;
}

/* ------------------------------------------------------------------
 * parse_end
 * END alone      -> ND_END (program terminator)
 * END IF         -> returns NULL (consumed by parse_if / parse_block)
 *   ... etc. -- handled in parse_block stop logic
 * ------------------------------------------------------------------ */
static AstNode *parse_end_stmt(Parser *p)
{
    Token   *t    = peek(p);
    int      line = t->line, col = t->col;
    AstNode *n;

    /* peek2 to decide what END means */
    token_free(advance(p)); /* consume END */

    switch (peek(p)->type) {
    case TOK_IF:
    case TOK_SUB:
    case TOK_FUNCTION:
    case TOK_SELECT:
    case TOK_TYPE:
    case TOK_TRY:
        /* These are block terminators; parse_block already consumed END
         * and now the caller will consume the specific keyword.
         * If we reach here unexpectedly, emit error. */
        syn_error(p, "unexpected END block-closer outside block");
        token_free(advance(p)); /* consume IF/SUB/etc. */
        return NULL;
    default:
        break;
    }

    n = make_node(ND_END, line, col);
    return n;
}

/* =========================================================================
 * parse_block
 * Parse a sequence of statements until one of stop1/stop2/stop3 is peeked
 * (or EOF).  stop values are TokenType; -1 means "not used".
 * The stopping token is NOT consumed.
 * Returns ND_BLOCK with stmts linked via ->next.
 * ====================================================================== */
static AstNode *parse_block(Parser *p, int stop1, int stop2, int stop3)
{
    AstNode *block = make_node(ND_BLOCK, peek(p)->line, peek(p)->col);
    AstNode *head  = NULL;
    AstNode *tail  = NULL;

    for (;;) {
        AstNode *stmt;
        TokenType tt;

        skip_newlines(p);
        tt = peek(p)->type;

        if (tt == TOK_EOF) break;
        if (stop1 >= 0 && tt == (TokenType)stop1) break;
        if (stop2 >= 0 && tt == (TokenType)stop2) break;
        if (stop3 >= 0 && tt == (TokenType)stop3) break;

        stmt = parse_statement(p);
        if (!stmt) {
            /* Error recovery already done in parse_statement */
            continue;
        }

        /* Append statement (or chain of statements from multi-dim DIM) */
        {
            AstNode *cur = stmt;
            while (cur) {
                AstNode *nx = cur->next;
                cur->next = NULL;
                if (!head) { head = cur; tail = cur; }
                else { tail->next = cur; tail = cur; }
                cur = nx;
            }
        }

        /* Consume statement terminator */
        {
            TokenType term = peek(p)->type;
            if (term == TOK_NEWLINE || term == TOK_COLON) {
                token_free(advance(p));
            }
        }
    }

    if (block) block->u.block.stmts = head;
    return block;
}

/* =========================================================================
 * parse_statement -- dispatcher
 * ====================================================================== */
static AstNode *parse_statement(Parser *p)
{
    Token    *t  = peek(p);
    TokenType tt = t->type;

    switch (tt) {
    case TOK_CALL:
        {
            /* CALL SubName [args] -- consume CALL keyword, parse as ND_CALL */
            AstNode *n;
            char    *name;
            AstNode *args = NULL;
            AstNode *arg;
            AstNode *tail = NULL;
            int      ln, cn;
            token_free(advance(p)); /* consume CALL */
            t  = peek(p);
            ln = t->line; cn = t->col;
            if (t->type != TOK_IDENT) {
                syn_error(p, "expected sub/function name after CALL");
                return NULL;
            }
            name = strdup(t->text);
            token_free(advance(p)); /* consume sub name */
            /* Optional argument list: CALL Sub(args) or CALL Sub args */
            if (peek(p)->type == TOK_LPAREN) {
                token_free(advance(p)); /* consume '(' */
                if (peek(p)->type != TOK_RPAREN) {
                    arg = parse_expr(p);
                    if (arg) { args = arg; tail = arg; }
                    while (peek(p)->type == TOK_COMMA) {
                        token_free(advance(p));
                        arg = parse_expr(p);
                        if (arg) { tail->next = arg; tail = arg; }
                    }
                }
                token_free(expect(p, TOK_RPAREN));
            } else if (peek(p)->type != TOK_NEWLINE &&
                       peek(p)->type != TOK_COLON   &&
                       peek(p)->type != TOK_EOF) {
                arg = parse_expr(p);
                if (arg) { args = arg; tail = arg; }
                while (peek(p)->type == TOK_COMMA) {
                    token_free(advance(p));
                    arg = parse_expr(p);
                    if (arg) { tail->next = arg; tail = arg; }
                }
            }
            n = make_node(ND_CALL, ln, cn);
            if (n) { n->u.call.name = name; n->u.call.args = args; }
            else   free(name);
            return n;
        }
    case TOK_IF:        return parse_if(p);
    case TOK_FOR:       return parse_for(p);
    case TOK_WHILE:     return parse_while(p);
    case TOK_DO:        return parse_do(p);
    case TOK_SELECT:    return parse_select(p);
    case TOK_SUB:       return parse_sub_def(p);
    case TOK_FUNCTION:  return parse_function_def(p);
    case TOK_DIM:       return parse_dim(p);
    case TOK_REDIM:     return parse_redim(p);
    case TOK_TYPE:      return parse_type_def(p);
    case TOK_PRINT:     return parse_print(p);
    case TOK_LPRINT:    return parse_lprint(p);
    case TOK_INPUT:     return parse_input(p);
    case TOK_LINE:      return parse_line_input(p);
    case TOK_OPEN:      return parse_open(p);
    case TOK_CLOSE:     return parse_close(p);
    case TOK_WRITE:     return parse_write(p);
    case TOK_READ:      return parse_read(p);
    case TOK_GET:       return parse_get(p);
    case TOK_PUT:       return parse_put(p);
    case TOK_TRY:       return parse_try(p);
    case TOK_ON:        return parse_on_error(p);
    case TOK_RESUME:    return parse_resume_next(p);
    case TOK_GOSUB:     return parse_gosub(p);
    case TOK_GOTO:      return parse_goto(p);
    case TOK_RETURN:    return parse_return(p);
    case TOK_EXIT:      return parse_exit(p);
    case TOK_END:       return parse_end_stmt(p);
    case TOK_RANDOMIZE:
        {
            AstNode *n;
            int ln = t->line, cn = t->col;
            token_free(advance(p)); /* consume RANDOMIZE */
            n = make_node(ND_CALL, ln, cn);
            if (n) {
                n->u.call.name = strdup("RANDOMIZE");
                if (peek(p)->type != TOK_NEWLINE &&
                    peek(p)->type != TOK_COLON   &&
                    peek(p)->type != TOK_EOF) {
                    if (peek(p)->type == TOK_IDENT &&
                        strcmp(peek(p)->text, "TIMER") == 0) {
                        token_free(advance(p)); /* RANDOMIZE TIMER => default -1 */
                    } else {
                        n->u.call.args = parse_expr(p);
                    }
                }
            }
            return n;
        }
    case TOK_OPTION:
        {
            token_free(advance(p)); /* consume OPTION */
            skip_to_newline(p);
            return NULL;
        }
    case TOK_STOP:
        {
            AstNode *n = make_node(ND_END, t->line, t->col);
            token_free(advance(p));
            return n;
        }
    case TOK_SHELL:
        {
            /* SHELL "command" or SHELL cmd$ */
            AstNode *n;
            int ln = t->line, cn = t->col;
            token_free(advance(p)); /* consume SHELL */
            n = make_node(ND_SHELL, ln, cn);
            /* If next token is a newline/colon/EOF, shell with empty string */
            if (n) {
                if (peek(p)->type != TOK_NEWLINE &&
                    peek(p)->type != TOK_COLON   &&
                    peek(p)->type != TOK_EOF) {
                    n->u.shell.cmd = parse_expr(p);
                }
            }
            return n;
        }
    case TOK_CLS:
        {
            AstNode *n = make_node(ND_CLS, t->line, t->col);
            token_free(advance(p));
            return n;
        }
    case TOK_LOCATE:
        {
            /* LOCATE row [, col] */
            AstNode *n;
            int ln = t->line, cn = t->col;
            token_free(advance(p)); /* consume LOCATE */
            n = make_node(ND_LOCATE, ln, cn);
            if (n) {
                n->u.locate.row = parse_expr(p);
                if (peek(p)->type == TOK_COMMA) {
                    token_free(advance(p));
                    n->u.locate.col = parse_expr(p);
                }
            }
            return n;
        }
    case TOK_COLOR:
        {
            /* COLOR fg [, bg] */
            AstNode *n;
            int ln = t->line, cn = t->col;
            token_free(advance(p)); /* consume COLOR */
            n = make_node(ND_COLOR, ln, cn);
            if (n) {
                n->u.color.fg = parse_expr(p);
                if (peek(p)->type == TOK_COMMA) {
                    token_free(advance(p));
                    n->u.color.bg = parse_expr(p);
                }
            }
            return n;
        }
    case TOK_BGCOLOR:
        {
            /* BGCOLOR bg  -- set background colour only */
            AstNode *n;
            int ln = t->line, cn = t->col;
            token_free(advance(p)); /* consume BGCOLOR */
            n = make_node(ND_BGCOLOR, ln, cn);
            if (n) {
                n->u.color.fg = NULL;        /* no foreground change */
                n->u.color.bg = parse_expr(p);
            }
            return n;
        }
    case TOK_LABEL:     return parse_label(p);

    /* Consume stray NEWLINE/COLON here (called from single-line IF etc.) */
    case TOK_NEWLINE:
    case TOK_COLON:
        token_free(advance(p));
        return NULL;

    /* Identifiers: FIELD / LSET / RSET, assignment, or sub call */
    case TOK_IDENT:
        if (t->text) {
            if (strcmp(t->text, "FIELD") == 0) return parse_field(p);
            if (strcmp(t->text, "LSET")  == 0) return parse_lset_rset(p, 0);
            if (strcmp(t->text, "RSET")  == 0) return parse_lset_rset(p, 1);
        }
        return parse_assign_or_call(p);
    case TOK_MID:      /* MID$ on LHS */
        return parse_assign_or_call(p);

    default:
        {
            char msg[128];
            sprintf(msg, "unexpected token '%s' in statement",
                    t->text ? t->text : token_type_name(tt));
            syn_error(p, msg);
            skip_to_newline(p);
            if (peek(p)->type == TOK_NEWLINE) token_free(advance(p));
            return NULL;
        }
    }
}

/* =========================================================================
 * Public API
 * ====================================================================== */

Parser *parser_new(Lexer *lex)
{
    Parser *p = (Parser *)calloc(1, sizeof(Parser));
    if (!p) return NULL;
    p->lex       = lex;
    p->had_error = 0;
    /* Cache filename from a temporary peek of the first token; the lexer
     * doesn't expose its filename directly, so we use a known-static string
     * passed via the error module.  We store empty string here since errors
     * are reported through error_report() which gets the filename from the
     * lexer internally. */
    p->filename = "";
    buf_init(p);
    return p;
}

void parser_free(Parser *p)
{
    int i;
    if (!p) return;
    if (p->buf[0]) token_free(p->buf[0]);
    if (p->buf[1]) token_free(p->buf[1]);
    for (i = 0; i < p->n_types; i++) free(p->type_names[i]);
    free(p);
}

int parser_had_error(Parser *p)
{
    return p ? p->had_error : 0;
}

AstNode *parser_run(Parser *p)
{
    AstNode *root;
    if (!p) return NULL;
    root = parse_block(p, -1, -1, -1);
    return root;
}

/* ast_dump is a thin wrapper around print_ast for backwards compat */
void ast_dump(const AstNode *node, int indent)
{
    print_ast((AstNode *)node, indent);
}
