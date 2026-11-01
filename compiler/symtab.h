/*
 * symtab.h -- Public interface for the scoped symbol table.
 * C89 compatible.
 *
 * DEVELOPMENT ENVIRONMENT NOTE:
 *   This file is authored on Windows (Visual Studio / MSVC environment).
 *   It is NOT compiled or tested on the authoring machine.
 *   The target build system is HP-UX 11i with gcc 4.2 / gmake.
 *   Transfer to HP-UX and build there with:
 *       gmake -f Makefile
 */

#ifndef SYMTAB_H
#define SYMTAB_H

#include "ast.h"   /* provides BasicType, AstNode, ParamDef */

/* ------------------------------------------------------------------ */
/* Symbol kinds                                                        */
/* ------------------------------------------------------------------ */
typedef enum {
    SK_VAR,       /* scalar variable                     */
    SK_ARRAY,     /* array variable                      */
    SK_SUB,       /* SUB definition                      */
    SK_FUNCTION,  /* FUNCTION definition                 */
    SK_TYPE,      /* user-defined TYPE record            */
    SK_FIELD      /* field inside a user-defined TYPE    */
} SymKind;

/* Legacy alias kept for any pre-existing code that uses SymbolKind */
typedef SymKind SymbolKind;
#define SYM_VARIABLE SK_VAR
#define SYM_ARRAY    SK_ARRAY
#define SYM_SUB      SK_SUB
#define SYM_FUNCTION SK_FUNCTION
#define SYM_TYPE     SK_TYPE
#define SYM_FIELD    SK_FIELD

/* ------------------------------------------------------------------ */
/* Symbol struct                                                       */
/* ------------------------------------------------------------------ */
typedef struct Symbol {
    char      *name;         /* uppercased name (includes sigil if any) */
    SymKind    kind;
    BasicType  type;         /* resolved basic type                     */
    int        type_idx;     /* index into user-type table if BT_USER   */
    int        scope_level;  /* nesting depth at declaration point      */
    int        is_byref;     /* 1 = BYREF parameter                     */
    int        is_shared;    /* 1 = DIM SHARED — visible everywhere     */
    int        ndims;        /* for SK_ARRAY: number of dimensions      */
    int        is_dynamic;   /* for SK_ARRAY: heap-allocated array      */
    ParamDef  *params;       /* for SK_SUB/SK_FUNCTION: parameter list  */
    BasicType  ret_type;     /* for SK_FUNCTION: return type            */
    int        ret_type_idx; /* for SK_FUNCTION: ret type_idx if BT_USER*/
} Symbol;

/* ------------------------------------------------------------------ */
/* Opaque symbol table                                                 */
/* ------------------------------------------------------------------ */
typedef struct SymTab SymTab;

/* Create / destroy */
SymTab  *symtab_new(void);
void     symtab_free(SymTab *st);

/* Scope management */
void     symtab_push_scope(SymTab *st);
void     symtab_pop_scope(SymTab *st);

/* Insert and lookup.
 * symtab_insert: returns 0 on success, -1 on duplicate. */
int      symtab_insert(SymTab *st, Symbol *sym);
Symbol  *symtab_lookup(SymTab *st, const char *name);       /* walks scope chain */
Symbol  *symtab_lookup_local(SymTab *st, const char *name); /* current scope only */

/* Return the current scope nesting depth (1 = global). */
int      symtab_current_level(SymTab *st);

/* Convenience: allocate and initialise a Symbol. */
Symbol  *make_symbol(const char *name, SymKind kind, BasicType type);

/* Free a Symbol and its owned strings (does NOT free params — caller owns). */
void     symbol_free(Symbol *s);

#endif /* SYMTAB_H */
