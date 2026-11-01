/*
 * parser.h -- Public interface for the BASIC recursive-descent parser.
 * C89 compatible.
 */

#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include "ast.h"

/* Opaque parser state */
typedef struct Parser Parser;

/* Create / destroy */
Parser  *parser_new(Lexer *lex);
void     parser_free(Parser *p);

/* Parse the full program; returns the root ND_BLOCK node.
 * Returns NULL on unrecoverable error (error already reported). */
AstNode *parser_run(Parser *p);

/* Return non-zero if any syntax error was encountered. */
int      parser_had_error(Parser *p);

/* Debug: pretty-print the AST to stdout (implemented in ast.c) */
void     ast_dump(const AstNode *node, int indent);

#endif /* PARSER_H */
