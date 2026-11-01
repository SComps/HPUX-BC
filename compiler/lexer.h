/*
 * lexer.h -- Public interface for the BASIC lexer.
 * C89 compatible.
 */

#ifndef LEXER_H
#define LEXER_H

#include "token.h"

/* Opaque lexer state */
typedef struct Lexer Lexer;

/* Create / destroy */
Lexer *lexer_new(const char *source, const char *filename);
void   lexer_free(Lexer *lex);

/* Token stream -- returns heap-allocated Token; caller frees with token_free() */
Token *lexer_next(Lexer *lex);
Token *lexer_peek(Lexer *lex);

/* Free a token returned by the lexer */
void   token_free(Token *tok);

/* Utility: token type name for error messages */
const char *token_type_name(TokenType t);

#endif /* LEXER_H */
