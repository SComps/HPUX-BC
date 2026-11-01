/*
 * emitter.h -- Public interface for the C code emitter.
 * C89 compatible.
 */

#ifndef EMITTER_H
#define EMITTER_H

#include <stdio.h>
#include "ast.h"
#include "symtab.h"

/* Run the emitter: walk the annotated AST and write C source to out.
 * st may be NULL if no symbol table is available (symbols resolved via AST).
 * Returns 0 on success, non-zero on error. */
int emitter_run(const AstNode *root, const SymTab *st, FILE *out);

#endif /* EMITTER_H */
