/*
 * lexer.c -- BASIC lexer: converts source text into a flat token stream.
 * C89 compatible -- no // comments, no C99 features.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "lexer.h"
#include "error.h"

/* -------------------------------------------------------------------------
 * Internal state
 * ---------------------------------------------------------------------- */

struct Lexer {
    const char *source;    /* NUL-terminated source string (not owned)   */
    const char *filename;  /* for error messages (not owned)              */
    int         pos;       /* current byte offset into source             */
    int         line;      /* 1-based current line                        */
    int         col;       /* 1-based current column                      */
    Token      *peeked;    /* one-token lookahead buffer (heap-allocated) */
};

/* -------------------------------------------------------------------------
 * Dynamic string buffer (for building token text)
 * ---------------------------------------------------------------------- */

typedef struct {
    char *data;
    int   len;
    int   cap;
} StrBuf;

static void sbuf_init(StrBuf *b)
{
    b->data = (char *)malloc(32);
    b->len  = 0;
    b->cap  = 32;
    if (b->data) b->data[0] = '\0';
}

static void sbuf_push(StrBuf *b, char c)
{
    char *tmp;
    if (b->len + 1 >= b->cap) {
        b->cap *= 2;
        tmp = (char *)realloc(b->data, (size_t)b->cap);
        if (!tmp) return; /* OOM: silently truncate */
        b->data = tmp;
    }
    b->data[b->len++] = c;
    b->data[b->len]   = '\0';
}

static void sbuf_free(StrBuf *b)
{
    free(b->data);
    b->data = NULL;
}

/* -------------------------------------------------------------------------
 * Keyword table (sorted, for binary search)
 * ---------------------------------------------------------------------- */

typedef struct {
    const char *word;
    TokenType   type;
} KwEntry;

static const KwEntry kw_table[] = {
    { "AND",       TOK_AND       },
    { "APPEND",    TOK_APPEND    },
    { "AS",        TOK_AS        },
    { "ASC",       TOK_ASC       },
    { "ATN",       TOK_ATN       },
    { "BGCOLOR",   TOK_BGCOLOR   },
    { "BINARY",    TOK_BINARY    },
    { "BYREF",     TOK_BYREF     },
    { "BYVAL",     TOK_BYVAL     },
    { "CALL",      TOK_CALL      },
    { "CASE",      TOK_CASE      },
    { "CATCH",     TOK_CATCH     },
    { "CDBL",      TOK_CDBL      },
    { "CHR",       TOK_CHR       },
    { "CINT",      TOK_CINT      },
    { "CLNG",      TOK_CLNG      },
    { "CLOSE",     TOK_CLOSE     },
    { "CLS",       TOK_CLS       },
    { "COLOR",     TOK_COLOR     },
    { "CSNG",      TOK_CSNG      },
    { "DIM",       TOK_DIM       },
    { "DO",        TOK_DO        },
    { "DOUBLE",    TOK_DOUBLE    },
    { "ELSE",      TOK_ELSE      },
    { "ELSEIF",    TOK_ELSEIF    },
    { "END",       TOK_END       },
    { "EOF",       TOK_EOF_KW    },
    { "ERL",       TOK_ERL       },
    { "ERR",       TOK_ERR       },
    { "ERROR",     TOK_ERROR     },
    { "EXIT",      TOK_EXIT      },
    { "EXP",       TOK_EXP       },
    { "FIX",       TOK_FIX       },
    { "FOR",       TOK_FOR       },
    { "FUNCTION",  TOK_FUNCTION  },
    { "GET",       TOK_GET       },
    { "GOSUB",     TOK_GOSUB     },
    { "GOTO",      TOK_GOTO      },
    { "HEX",       TOK_HEX       },
    { "IF",        TOK_IF        },
    { "INPUT",     TOK_INPUT     },
    { "INT",       TOK_INT_KW    },
    { "INTEGER",   TOK_INTEGER   },
    { "LCASE",     TOK_LCASE     },
    { "LEFT",      TOK_LEFT      },
    { "LEN",       TOK_LEN       },
    { "LINE",      TOK_LINE      },
    { "LOC",       TOK_LOC       },
    { "LOCAL",     TOK_LOCAL     },
    { "LOCATE",    TOK_LOCATE    },
    { "LOF",       TOK_LOF       },
    { "LOG",       TOK_LOG       },
    { "LONG",      TOK_LONG      },
    { "LOOP",      TOK_LOOP      },
    { "LPRINT",    TOK_LPRINT    },
    { "LTRIM",     TOK_LTRIM     },
    { "MID",       TOK_MID       },
    { "MOD",       TOK_MOD       },
    { "NEXT",      TOK_NEXT      },
    { "NOT",       TOK_NOT       },
    { "OCT",       TOK_OCT       },
    { "ON",        TOK_ON        },
    { "OPEN",      TOK_OPEN      },
    { "OPTION",    TOK_OPTION    },
    { "OR",        TOK_OR        },
    { "OUTPUT",    TOK_OUTPUT    },
    { "PRESERVE",  TOK_PRESERVE  },
    { "PRINT",     TOK_PRINT     },
    { "PUT",       TOK_PUT       },
    { "RANDOM",    TOK_RANDOM    },
    { "RANDOMIZE", TOK_RANDOMIZE },
    { "READ",      TOK_READ      },
    { "REDIM",     TOK_REDIM     },
    { "REM",       TOK_REM       },
    { "RESUME",    TOK_RESUME    },
    { "RETURN",    TOK_RETURN    },
    { "RIGHT",     TOK_RIGHT     },
    { "RND",       TOK_RND       },
    { "RTRIM",     TOK_RTRIM     },
    { "SELECT",    TOK_SELECT    },
    { "SGN",       TOK_SGN       },
    { "SHELL",     TOK_SHELL     },
    { "SIN",       TOK_SIN       },
    { "SINGLE",    TOK_SINGLE    },
    { "SPACE",     TOK_SPACE     },
    { "SQR",       TOK_SQR       },
    { "STEP",      TOK_STEP      },
    { "STOP",      TOK_STOP      },
    { "STR",       TOK_STR       },
    { "STRING",    TOK_STRING_TYPE },
    { "SUB",       TOK_SUB       },
    { "TAN",       TOK_TAN       },
    { "THEN",      TOK_THEN      },
    { "TO",        TOK_TO        },
    { "TRIM",      TOK_TRIM      },
    { "TRY",       TOK_TRY       },
    { "TYPE",      TOK_TYPE      },
    { "UCASE",     TOK_UCASE     },
    { "UNTIL",     TOK_UNTIL     },
    { "VAL",       TOK_VAL       },
    { "WEND",      TOK_WEND      },
    { "WHILE",     TOK_WHILE     },
    { "WRITE",     TOK_WRITE     },
    { "XOR",       TOK_XOR       }
};

#define KW_TABLE_SIZE ((int)(sizeof(kw_table) / sizeof(kw_table[0])))

static TokenType keyword_lookup(const char *upper_word)
{
    int lo  = 0;
    int hi  = KW_TABLE_SIZE - 1;
    int mid, cmp;
    while (lo <= hi) {
        mid = (lo + hi) / 2;
        cmp = strcmp(upper_word, kw_table[mid].word);
        if (cmp == 0) return kw_table[mid].type;
        if (cmp < 0)  hi = mid - 1;
        else          lo = mid + 1;
    }
    return TOK_IDENT;
}

/* -------------------------------------------------------------------------
 * Character helpers
 * ---------------------------------------------------------------------- */

static char src_at(const Lexer *lex, int offset)
{
    return lex->source[lex->pos + offset];
}

static char cur(const Lexer *lex)  { return src_at(lex, 0); }
static char peek1(const Lexer *lex) { return src_at(lex, 1); }

static void advance(Lexer *lex)
{
    if (lex->source[lex->pos] == '\0') return;
    if (lex->source[lex->pos] == '\n') {
        lex->line++;
        lex->col = 1;
    } else {
        lex->col++;
    }
    lex->pos++;
}

/* Skip horizontal whitespace (spaces and tabs) */
static void skip_hspace(Lexer *lex)
{
    while (cur(lex) == ' ' || cur(lex) == '\t' || cur(lex) == '\r')
        advance(lex);
}

/* -------------------------------------------------------------------------
 * Token allocation helpers
 * ---------------------------------------------------------------------- */

static Token *make_token(TokenType type, const char *text, int line, int col)
{
    Token *t = (Token *)malloc(sizeof(Token));
    if (!t) return NULL;
    t->type = type;
    t->text = (char *)malloc(strlen(text) + 1);
    if (t->text) strcpy(t->text, text);
    t->line = line;
    t->col  = col;
    return t;
}

static Token *make_eof_token(int line, int col)
{
    return make_token(TOK_EOF, "", line, col);
}

void token_free(Token *tok)
{
    if (!tok) return;
    free(tok->text);
    free(tok);
}

/* -------------------------------------------------------------------------
 * Core scan function
 * ---------------------------------------------------------------------- */

static Token *scan_next(Lexer *lex)
{
    int       tok_line, tok_col;
    StrBuf    buf;
    char      c, up;
    int       is_float, has_dot, has_exp;
    /* identifier keyword-check temporaries (hoisted for C89) */
    char      last_ch;
    int       has_sigil;
    char      saved;
    TokenType num_type;
    TokenType id_type;

    tok_line  = 0;
    tok_col   = 0;
    c         = '\0';
    up        = '\0';
    is_float  = 0;
    has_dot   = 0;
    has_exp   = 0;
    last_ch   = '\0';
    has_sigil = 0;
    saved     = '\0';
    num_type  = TOK_INT_LIT;
    id_type   = TOK_IDENT;

    sbuf_init(&buf);

top:
    /* Skip horizontal whitespace */
    skip_hspace(lex);

    c = cur(lex);

    /* End of source */
    if (c == '\0') {
        sbuf_free(&buf);
        return make_eof_token(lex->line, lex->col);
    }

    tok_line = lex->line;
    tok_col  = lex->col;

    /* ---------------------------------------------------------------
     * Newline / line continuation
     * ------------------------------------------------------------ */
    if (c == '\n') {
        advance(lex);
        sbuf_free(&buf);
        return make_token(TOK_NEWLINE, "\n", tok_line, tok_col);
    }

    /* Windows CRLF: '\r' is skipped in skip_hspace; lone '\r' here */
    if (c == '\r') {
        advance(lex);
        goto top;
    }

    /* ---------------------------------------------------------------
     * Comments: ' or REM keyword (checked later in identifier path)
     * ------------------------------------------------------------ */
    if (c == '\'') {
        /* Consume to end of line */
        while (cur(lex) != '\n' && cur(lex) != '\0')
            advance(lex);
        goto top; /* re-scan; next char will be '\n' or EOF */
    }

    /* ---------------------------------------------------------------
     * String literals
     * ------------------------------------------------------------ */
    if (c == '"') {
        advance(lex); /* consume opening " */
        for (;;) {
            c = cur(lex);
            if (c == '\0' || c == '\n') {
                /* Unterminated string */
                error_report(lex->filename, tok_line, tok_col,
                             "unterminated string literal");
                break;
            }
            if (c == '"') {
                advance(lex);
                if (cur(lex) == '"') {
                    /* "" = escaped double-quote inside string */
                    sbuf_push(&buf, '"');
                    advance(lex);
                } else {
                    break; /* end of string */
                }
            } else {
                sbuf_push(&buf, c);
                advance(lex);
            }
        }
        {
            Token *t = make_token(TOK_STRING_LIT, buf.data, tok_line, tok_col);
            sbuf_free(&buf);
            return t;
        }
    }

    /* ---------------------------------------------------------------
     * Numeric literals
     * ------------------------------------------------------------ */
    if (isdigit((unsigned char)c) || (c == '.' && isdigit((unsigned char)peek1(lex)))) {
        is_float = 0;
        has_dot  = 0;
        has_exp  = 0;

        /* Integer / float part */
        while (isdigit((unsigned char)cur(lex))) {
            sbuf_push(&buf, cur(lex));
            advance(lex);
        }

        /* Decimal point */
        if (cur(lex) == '.' && isdigit((unsigned char)peek1(lex))) {
            has_dot  = 1;
            is_float = 1;
            sbuf_push(&buf, '.');
            advance(lex);
            while (isdigit((unsigned char)cur(lex))) {
                sbuf_push(&buf, cur(lex));
                advance(lex);
            }
        } else if (cur(lex) == '.' && peek1(lex) != '.') {
            /* Trailing dot (e.g. "1.") treated as float */
            has_dot  = 1;
            is_float = 1;
            sbuf_push(&buf, '.');
            advance(lex);
        }

        /* Exponent */
        if (cur(lex) == 'E' || cur(lex) == 'e') {
            has_exp  = 1;
            is_float = 1;
            sbuf_push(&buf, (char)toupper((unsigned char)cur(lex)));
            advance(lex);
            if (cur(lex) == '+' || cur(lex) == '-') {
                sbuf_push(&buf, cur(lex));
                advance(lex);
            }
            while (isdigit((unsigned char)cur(lex))) {
                sbuf_push(&buf, cur(lex));
                advance(lex);
            }
        }
        (void)has_dot;
        (void)has_exp;

        /* Type suffix */
        if (!is_float && cur(lex) == '&') {
            /* LONG literal: 32768& */
            sbuf_push(&buf, '&');
            advance(lex);
            num_type = TOK_LONG_LIT;
        } else if (cur(lex) == '!' && !is_float) {
            /* SINGLE literal: 3.14! -- only valid on floats but accept anyway */
            sbuf_push(&buf, '!');
            advance(lex);
            num_type = TOK_FLOAT_LIT;
        } else if (cur(lex) == '#') {
            /* DOUBLE literal */
            sbuf_push(&buf, '#');
            advance(lex);
            num_type = TOK_DOUBLE_LIT;
        } else if (is_float) {
            num_type = TOK_FLOAT_LIT;
        } else {
            num_type = TOK_INT_LIT;
        }

        {
            Token *t = make_token(num_type, buf.data, tok_line, tok_col);
            sbuf_free(&buf);
            return t;
        }
    }

    /* ---------------------------------------------------------------
     * Identifiers and keywords
     * ------------------------------------------------------------ */
    if (isalpha((unsigned char)c) || c == '_') {
        while (isalnum((unsigned char)cur(lex)) || cur(lex) == '_') {
            up = (char)toupper((unsigned char)cur(lex));
            sbuf_push(&buf, up);
            advance(lex);
        }

        /* Check for type sigil immediately attached to identifier */
        c = cur(lex);
        if (c == '%' || c == '!' || c == '#' || c == '$') {
            sbuf_push(&buf, c);
            advance(lex);
        }
        /* Note: '&' sigil is handled below — we need to see if it's
         * immediately after identifier (LONG sigil) or preceded by
         * whitespace (concat).  Since we just consumed the identifier with
         * no whitespace between, '&' here IS a sigil. */
        else if (c == '&') {
            sbuf_push(&buf, '&');
            advance(lex);
        }

        /* Strip trailing sigil for keyword test (sigil stays in text) */
        last_ch   = buf.data[buf.len - 1];
        has_sigil = (last_ch == '%' || last_ch == '&' || last_ch == '!'
                     || last_ch == '#' || last_ch == '$');
        if (!has_sigil) {
            id_type = keyword_lookup(buf.data);
        } else {
            /* Remove sigil temporarily to test keyword */
            saved = buf.data[buf.len - 1];
            buf.data[buf.len - 1] = '\0';
            id_type = keyword_lookup(buf.data);
            buf.data[buf.len - 1] = saved;
            if (id_type != TOK_IDENT) {
                /* e.g. "END$" is not a real thing; treat as IDENT */
                id_type = TOK_IDENT;
            }
        }

        if (id_type == TOK_REM) {
            /* REM comment -- consume to end of line, re-scan */
            while (cur(lex) != '\n' && cur(lex) != '\0')
                advance(lex);
            sbuf_free(&buf);
            sbuf_init(&buf);
            goto top;
        }

        /* Label detection: identifier immediately followed by ':' at start of
         * statement.  We always emit TOK_LABEL when we see ident+':' because
         * the parser knows from context whether it is a label or a statement
         * separator.  Colons used as statement separators are emitted as
         * TOK_COLON (handled below); a colon right after an identifier
         * (no space) is a label. */
        if (id_type == TOK_IDENT && cur(lex) == ':') {
            /* Consume the ':' */
            advance(lex);
            {
                Token *t = make_token(TOK_LABEL, buf.data, tok_line, tok_col);
                sbuf_free(&buf);
                return t;
            }
        }

        /* Line continuation: '_' at end of line */
        if (id_type == TOK_IDENT && buf.data[0] == '_' && buf.len == 1) {
            /* A bare underscore token right before a newline is continuation */
            skip_hspace(lex);
            if (cur(lex) == '\'') {
                while (cur(lex) != '\n' && cur(lex) != '\0')
                    advance(lex);
            }
            if (cur(lex) == '\n') {
                advance(lex); /* consume the newline */
                sbuf_free(&buf);
                sbuf_init(&buf);
                goto top;
            }
        }

        {
            Token *t = make_token(id_type, buf.data, tok_line, tok_col);
            sbuf_free(&buf);
            return t;
        }
    }

    /* ---------------------------------------------------------------
     * Line continuation: '_' as a standalone punctuation character
     * followed immediately by optional comment + newline
     * ------------------------------------------------------------ */
    if (c == '_') {
        advance(lex);
        skip_hspace(lex);
        if (cur(lex) == '\'') {
            while (cur(lex) != '\n' && cur(lex) != '\0')
                advance(lex);
        }
        if (cur(lex) == '\n') {
            advance(lex);
        }
        sbuf_free(&buf);
        sbuf_init(&buf);
        goto top;
    }

    /* ---------------------------------------------------------------
     * Operators and punctuation
     * ------------------------------------------------------------ */
    switch (c) {
    case '+':
        advance(lex);
        sbuf_free(&buf);
        return make_token(TOK_PLUS, "+", tok_line, tok_col);

    case '-':
        advance(lex);
        sbuf_free(&buf);
        return make_token(TOK_MINUS, "-", tok_line, tok_col);

    case '*':
        advance(lex);
        sbuf_free(&buf);
        return make_token(TOK_STAR, "*", tok_line, tok_col);

    case '/':
        advance(lex);
        sbuf_free(&buf);
        return make_token(TOK_SLASH, "/", tok_line, tok_col);

    case '\\':
        advance(lex);
        sbuf_free(&buf);
        return make_token(TOK_BACKSLASH, "\\", tok_line, tok_col);

    case '^':
        advance(lex);
        sbuf_free(&buf);
        return make_token(TOK_CARET, "^", tok_line, tok_col);

    case '=':
        advance(lex);
        sbuf_free(&buf);
        return make_token(TOK_EQ, "=", tok_line, tok_col);

    case '<':
        advance(lex);
        if (cur(lex) == '>') {
            advance(lex);
            sbuf_free(&buf);
            return make_token(TOK_NEQ, "<>", tok_line, tok_col);
        }
        if (cur(lex) == '=') {
            advance(lex);
            sbuf_free(&buf);
            return make_token(TOK_LE, "<=", tok_line, tok_col);
        }
        sbuf_free(&buf);
        return make_token(TOK_LT, "<", tok_line, tok_col);

    case '>':
        advance(lex);
        if (cur(lex) == '=') {
            advance(lex);
            sbuf_free(&buf);
            return make_token(TOK_GE, ">=", tok_line, tok_col);
        }
        sbuf_free(&buf);
        return make_token(TOK_GT, ">", tok_line, tok_col);

    case '&':
        /* Context: after whitespace/operator this is string concat.
         * Because skip_hspace already consumed whitespace above, when we
         * reach here '&' cannot be a sigil (sigils are consumed directly
         * after the identifier/number in those code paths above). */
        advance(lex);
        sbuf_free(&buf);
        return make_token(TOK_AMPERSAND, "&", tok_line, tok_col);

    case '(':
        advance(lex);
        sbuf_free(&buf);
        return make_token(TOK_LPAREN, "(", tok_line, tok_col);

    case ')':
        advance(lex);
        sbuf_free(&buf);
        return make_token(TOK_RPAREN, ")", tok_line, tok_col);

    case ',':
        advance(lex);
        sbuf_free(&buf);
        return make_token(TOK_COMMA, ",", tok_line, tok_col);

    case ';':
        advance(lex);
        sbuf_free(&buf);
        return make_token(TOK_SEMICOLON, ";", tok_line, tok_col);

    case ':':
        advance(lex);
        sbuf_free(&buf);
        return make_token(TOK_COLON, ":", tok_line, tok_col);

    case '.':
        advance(lex);
        sbuf_free(&buf);
        return make_token(TOK_DOT, ".", tok_line, tok_col);

    case '#':
        /* Standalone '#' used as file-number prefix: OPEN...AS #n, PRINT #n, etc. */
        advance(lex);
        sbuf_free(&buf);
        return make_token(TOK_SIGIL_DOUBLE, "#", tok_line, tok_col);

    default:
        /* Unknown character: report and skip */
        {
            char msg[64];
            sprintf(msg, "unexpected character '%c' (0x%02X)", c, (unsigned char)c);
            error_report(lex->filename, tok_line, tok_col, msg);
        }
        advance(lex);
        sbuf_free(&buf);
        sbuf_init(&buf);
        goto top;
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

Lexer *lexer_new(const char *source, const char *filename)
{
    Lexer *lex = (Lexer *)malloc(sizeof(Lexer));
    if (!lex) return NULL;
    lex->source   = source;
    lex->filename = filename;
    lex->pos      = 0;
    lex->line     = 1;
    lex->col      = 1;
    lex->peeked   = NULL;
    return lex;
}

void lexer_free(Lexer *lex)
{
    if (!lex) return;
    if (lex->peeked) token_free(lex->peeked);
    free(lex);
}

Token *lexer_next(Lexer *lex)
{
    Token *t;
    if (lex->peeked) {
        t = lex->peeked;
        lex->peeked = NULL;
        return t;
    }
    return scan_next(lex);
}

Token *lexer_peek(Lexer *lex)
{
    if (!lex->peeked) {
        lex->peeked = scan_next(lex);
    }
    return lex->peeked;
}

/* -------------------------------------------------------------------------
 * Utility: token type name (for debug / error messages)
 * ---------------------------------------------------------------------- */

const char *token_type_name(TokenType t)
{
    switch (t) {
    case TOK_EOF:          return "EOF";
    case TOK_INT_LIT:      return "INT_LIT";
    case TOK_LONG_LIT:     return "LONG_LIT";
    case TOK_FLOAT_LIT:    return "FLOAT_LIT";
    case TOK_DOUBLE_LIT:   return "DOUBLE_LIT";
    case TOK_STRING_LIT:   return "STRING_LIT";
    case TOK_IDENT:        return "IDENT";
    case TOK_LABEL:        return "LABEL";
    case TOK_NEWLINE:      return "NEWLINE";
    case TOK_PLUS:         return "PLUS";
    case TOK_MINUS:        return "MINUS";
    case TOK_STAR:         return "STAR";
    case TOK_SLASH:        return "SLASH";
    case TOK_BACKSLASH:    return "BACKSLASH";
    case TOK_CARET:        return "CARET";
    case TOK_AMPERSAND:    return "AMPERSAND";
    case TOK_EQ:           return "EQ";
    case TOK_NEQ:          return "NEQ";
    case TOK_LT:           return "LT";
    case TOK_GT:           return "GT";
    case TOK_LE:           return "LE";
    case TOK_GE:           return "GE";
    case TOK_LPAREN:       return "LPAREN";
    case TOK_RPAREN:       return "RPAREN";
    case TOK_COMMA:        return "COMMA";
    case TOK_SEMICOLON:    return "SEMICOLON";
    case TOK_COLON:        return "COLON";
    case TOK_DOT:          return "DOT";
    case TOK_SIGIL_INT:    return "SIGIL_INT";
    case TOK_SIGIL_LONG:   return "SIGIL_LONG";
    case TOK_SIGIL_SINGLE: return "SIGIL_SINGLE";
    case TOK_SIGIL_DOUBLE: return "SIGIL_DOUBLE";
    case TOK_SIGIL_STRING: return "SIGIL_STRING";
    case TOK_AND:          return "AND";
    case TOK_APPEND:       return "APPEND";
    case TOK_AS:           return "AS";
    case TOK_ASC:          return "ASC";
    case TOK_ATN:          return "ATN";
    case TOK_BINARY:       return "BINARY";
    case TOK_BYREF:        return "BYREF";
    case TOK_BYVAL:        return "BYVAL";
    case TOK_CALL:         return "CALL";
    case TOK_CASE:         return "CASE";
    case TOK_CATCH:        return "CATCH";
    case TOK_CLS:          return "CLS";
    case TOK_BGCOLOR:      return "BGCOLOR";
    case TOK_COLOR:        return "COLOR";
    case TOK_CDBL:         return "CDBL";
    case TOK_CHR:          return "CHR";
    case TOK_CINT:         return "CINT";
    case TOK_CLNG:         return "CLNG";
    case TOK_CLOSE:        return "CLOSE";
    case TOK_CSNG:         return "CSNG";
    case TOK_DIM:          return "DIM";
    case TOK_DO:           return "DO";
    case TOK_DOUBLE:       return "DOUBLE";
    case TOK_ELSE:         return "ELSE";
    case TOK_ELSEIF:       return "ELSEIF";
    case TOK_END:          return "END";
    case TOK_EOF_KW:       return "EOF_KW";
    case TOK_ERL:          return "ERL";
    case TOK_ERR:          return "ERR";
    case TOK_ERROR:        return "ERROR";
    case TOK_EXIT:         return "EXIT";
    case TOK_EXP:          return "EXP";
    case TOK_FIX:          return "FIX";
    case TOK_FOR:          return "FOR";
    case TOK_FUNCTION:     return "FUNCTION";
    case TOK_GET:          return "GET";
    case TOK_GOSUB:        return "GOSUB";
    case TOK_GOTO:         return "GOTO";
    case TOK_HEX:          return "HEX";
    case TOK_IF:           return "IF";
    case TOK_INPUT:        return "INPUT";
    case TOK_INT_KW:       return "INT_KW";
    case TOK_INTEGER:      return "INTEGER";
    case TOK_LCASE:        return "LCASE";
    case TOK_LEFT:         return "LEFT";
    case TOK_LEN:          return "LEN";
    case TOK_LINE:         return "LINE";
    case TOK_LOCATE:       return "LOCATE";
    case TOK_LOC:          return "LOC";
    case TOK_LOCAL:        return "LOCAL";
    case TOK_LOF:          return "LOF";
    case TOK_LOG:          return "LOG";
    case TOK_LONG:         return "LONG";
    case TOK_LOOP:         return "LOOP";
    case TOK_LPRINT:       return "LPRINT";
    case TOK_LTRIM:        return "LTRIM";
    case TOK_MID:          return "MID";
    case TOK_MOD:          return "MOD";
    case TOK_NEXT:         return "NEXT";
    case TOK_NOT:          return "NOT";
    case TOK_OCT:          return "OCT";
    case TOK_ON:           return "ON";
    case TOK_OPEN:         return "OPEN";
    case TOK_OPTION:       return "OPTION";
    case TOK_OR:           return "OR";
    case TOK_OUTPUT:       return "OUTPUT";
    case TOK_PRESERVE:     return "PRESERVE";
    case TOK_PRINT:        return "PRINT";
    case TOK_PUT:          return "PUT";
    case TOK_RANDOM:       return "RANDOM";
    case TOK_RANDOMIZE:    return "RANDOMIZE";
    case TOK_READ:         return "READ";
    case TOK_REDIM:        return "REDIM";
    case TOK_REM:          return "REM";
    case TOK_RESUME:       return "RESUME";
    case TOK_RETURN:       return "RETURN";
    case TOK_RIGHT:        return "RIGHT";
    case TOK_RND:          return "RND";
    case TOK_RTRIM:        return "RTRIM";
    case TOK_SELECT:       return "SELECT";
    case TOK_SGN:          return "SGN";
    case TOK_SHELL:        return "SHELL";
    case TOK_SIN:          return "SIN";
    case TOK_SINGLE:       return "SINGLE";
    case TOK_SPACE:        return "SPACE";
    case TOK_SQR:          return "SQR";
    case TOK_STEP:         return "STEP";
    case TOK_STOP:         return "STOP";
    case TOK_STR:          return "STR";
    case TOK_STRING_KW:    return "STRING_KW";
    case TOK_STRING_TYPE:  return "STRING_TYPE";
    case TOK_SUB:          return "SUB";
    case TOK_TAN:          return "TAN";
    case TOK_THEN:         return "THEN";
    case TOK_TO:           return "TO";
    case TOK_TRIM:         return "TRIM";
    case TOK_TRY:          return "TRY";
    case TOK_TYPE:         return "TYPE";
    case TOK_UCASE:        return "UCASE";
    case TOK_UNTIL:        return "UNTIL";
    case TOK_VAL:          return "VAL";
    case TOK_WEND:         return "WEND";
    case TOK_WHILE:        return "WHILE";
    case TOK_WRITE:        return "WRITE";
    case TOK_XOR:          return "XOR";
    default:               return "???";
    }
}
