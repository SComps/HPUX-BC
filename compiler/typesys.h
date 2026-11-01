/*
 * typesys.h -- Public interface for the type-checking / semantic-analysis pass.
 * C89 compatible.
 *
 * DEVELOPMENT ENVIRONMENT NOTE:
 *   Authored on Windows. Target: HP-UX 11i, gcc 4.2, gmake.
 *   Do not attempt to build on the authoring machine.
 */

#ifndef TYPESYS_H
#define TYPESYS_H

#include "ast.h"
#include "symtab.h"

/* ------------------------------------------------------------------ */
/* User-defined TYPE registry entry (TYPE...END TYPE)                 */
/* ------------------------------------------------------------------ */
typedef struct UserType {
    char     *name;       /* uppercased type name                    */
    ParamDef *fields;     /* linked list of field definitions        */
} UserType;

/* ------------------------------------------------------------------ */
/* TypeSys — opaque semantic-analysis state                           */
/* ------------------------------------------------------------------ */
typedef struct TypeSys TypeSys;

/* Create a TypeSys bound to an existing SymTab.
 * The SymTab is not owned by TypeSys — caller must free it separately. */
TypeSys *typesys_new(SymTab *st);

/* Free TypeSys and all symbols it owns.
 * Does NOT free the SymTab passed to typesys_new. */
void     typesys_free(TypeSys *t);

/* Walk the AST, populate the symbol table, annotate every expression
 * node with its resolved BasicType, and report semantic errors.
 * Returns the number of errors found (0 = clean). */
int      typesys_check(TypeSys *t, AstNode *program);

/* Return the resolved BasicType of an expression node.
 * Valid only after typesys_check has been called. */
BasicType typesys_expr_type(const AstNode *expr);

/* Map a sigil character to a BasicType.
 *   %  -> BT_INTEGER
 *   &  -> BT_LONG
 *   !  -> BT_SINGLE
 *   #  -> BT_DOUBLE
 *   $  -> BT_STRING
 *   \0 -> BT_SINGLE (default, no sigil)
 */
BasicType typesys_sigil_to_type(char sigil);

#endif /* TYPESYS_H */
