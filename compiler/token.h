/*
 * token.h -- Token types and Token struct for the BASIC lexer.
 * C89 compatible.
 */

#ifndef TOKEN_H
#define TOKEN_H

typedef enum {
    /* End of file */
    TOK_EOF = 0,

    /* Literals */
    TOK_INT_LIT,
    TOK_LONG_LIT,
    TOK_FLOAT_LIT,
    TOK_DOUBLE_LIT,
    TOK_STRING_LIT,

    /* Identifier / label */
    TOK_IDENT,
    TOK_LABEL,      /* identifier followed by ':' */

    /* Type sigils */
    TOK_SIGIL_INT,     /* % */
    TOK_SIGIL_LONG,    /* & */
    TOK_SIGIL_SINGLE,  /* ! */
    TOK_SIGIL_DOUBLE,  /* # */
    TOK_SIGIL_STRING,  /* $ */

    /* Arithmetic / string operators */
    TOK_PLUS,       /* + */
    TOK_MINUS,      /* - */
    TOK_STAR,       /* * */
    TOK_SLASH,      /* / */
    TOK_BACKSLASH,  /* \ integer divide */
    TOK_CARET,      /* ^ exponentiation */
    TOK_AMPERSAND,  /* & string concat (also SIGIL_LONG -- disambiguated by lexer) */

    /* Relational */
    TOK_EQ,   /* = */
    TOK_NEQ,  /* <> */
    TOK_LT,   /* < */
    TOK_GT,   /* > */
    TOK_LE,   /* <= */
    TOK_GE,   /* >= */

    /* Punctuation */
    TOK_LPAREN,   /* ( */
    TOK_RPAREN,   /* ) */
    TOK_COMMA,    /* , */
    TOK_SEMICOLON, /* ; */
    TOK_COLON,    /* : */
    TOK_NEWLINE,  /* \n  (significant in BASIC) */
    TOK_DOT,      /* .  (for TYPE field access) */

    /* Keywords -- alphabetical */
    TOK_AND,
    TOK_APPEND,
    TOK_AS,
    TOK_ASC,
    TOK_ATN,
    TOK_BINARY,
    TOK_BYREF,
    TOK_BYVAL,
    TOK_CALL,
    TOK_CASE,
    TOK_CATCH,
    TOK_CLS,
    TOK_COLOR,
    TOK_BGCOLOR,
    TOK_CDBL,
    TOK_CHR,
    TOK_CINT,
    TOK_CLNG,
    TOK_CLOSE,
    TOK_CSNG,
    TOK_DIM,
    TOK_DO,
    TOK_DOUBLE,
    TOK_ELSE,
    TOK_ELSEIF,
    TOK_END,
    TOK_EOF_KW,   /* EOF() built-in */
    TOK_ERL,
    TOK_ERR,
    TOK_ERROR,
    TOK_EXIT,
    TOK_EXP,
    TOK_FIX,
    TOK_FOR,
    TOK_FUNCTION,
    TOK_GET,
    TOK_GOSUB,
    TOK_GOTO,
    TOK_HEX,
    TOK_IF,
    TOK_INPUT,
    TOK_INT_KW,   /* INT() built-in (avoid conflict with type name) */
    TOK_INTEGER,
    TOK_LEFT,
    TOK_LOCATE,
    TOK_LEN,
    TOK_LINE,
    TOK_LOC,
    TOK_LOCAL,
    TOK_LOF,
    TOK_LOG,
    TOK_LONG,
    TOK_LOOP,
    TOK_LCASE,
    TOK_LTRIM,
    TOK_MID,
    TOK_MOD,
    TOK_NEXT,
    TOK_NOT,
    TOK_OCT,
    TOK_ON,
    TOK_OPEN,
    TOK_OPTION,
    TOK_OR,
    TOK_OUTPUT,
    TOK_PRESERVE,
    TOK_LPRINT,
    TOK_PRINT,
    TOK_PUT,
    TOK_RANDOM,
    TOK_RANDOMIZE,
    TOK_READ,
    TOK_REDIM,
    TOK_REM,
    TOK_RESUME,
    TOK_RETURN,
    TOK_RIGHT,
    TOK_RND,
    TOK_RTRIM,
    TOK_SELECT,
    TOK_SGN,
    TOK_SHELL,
    TOK_SIN,
    TOK_SINGLE,
    TOK_SPACE,
    TOK_SQR,
    TOK_STEP,
    TOK_STOP,
    TOK_STRING_KW, /* STRING$ built-in */
    TOK_STRING_TYPE, /* STRING type keyword */
    TOK_STR,
    TOK_SUB,
    TOK_TAN,
    TOK_THEN,
    TOK_TO,
    TOK_TRIM,
    TOK_TRY,
    TOK_TYPE,
    TOK_UCASE,
    TOK_UNTIL,
    TOK_VAL,
    TOK_WEND,
    TOK_WHILE,
    TOK_WRITE,
    TOK_XOR,

    TOK_COUNT /* sentinel -- must be last */
} TokenType;

typedef struct {
    TokenType  type;
    char      *text;  /* NUL-terminated, heap-allocated copy */
    int        line;
    int        col;
} Token;

#endif /* TOKEN_H */
