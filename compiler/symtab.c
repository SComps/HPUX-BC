/*
 * symtab.c -- Scoped symbol table implementation.
 * C89 compatible.
 *
 * DEVELOPMENT ENVIRONMENT NOTE:
 *   Authored on Windows. Target: HP-UX 11i, gcc 4.2, gmake.
 *   Do not attempt to build on the authoring machine.
 */

#include <stdlib.h>
#include <string.h>
#include "symtab.h"

#define HASH_SIZE 64

typedef struct Bucket {
    Symbol        *sym;
    struct Bucket *next;
} Bucket;

typedef struct Scope {
    Bucket        *buckets[HASH_SIZE];
    struct Scope  *prev;
} Scope;

struct SymTab {
    Scope *top;
    int    level;
};

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static unsigned int hash_str(const char *s)
{
    unsigned int h = 0;
    while (*s) h = h * 31u + (unsigned char)*s++;
    return h % HASH_SIZE;
}

/* ------------------------------------------------------------------ */
/* Create / destroy                                                    */
/* ------------------------------------------------------------------ */

SymTab *symtab_new(void)
{
    SymTab *st = (SymTab *)calloc(1, sizeof(SymTab));
    if (!st) return NULL;
    symtab_push_scope(st);  /* global scope, level 1 */
    return st;
}

void symtab_free(SymTab *st)
{
    if (!st) return;
    while (st->top) symtab_pop_scope(st);
    free(st);
}

/* ------------------------------------------------------------------ */
/* Scope management                                                    */
/* ------------------------------------------------------------------ */

void symtab_push_scope(SymTab *st)
{
    Scope *s = (Scope *)calloc(1, sizeof(Scope));
    if (!s) return;
    s->prev = st->top;
    st->top = s;
    st->level++;
}

void symtab_pop_scope(SymTab *st)
{
    Scope *s;
    int i;
    if (!st->top) return;
    s = st->top;
    for (i = 0; i < HASH_SIZE; i++) {
        Bucket *b = s->buckets[i];
        while (b) {
            Bucket *nb = b->next;
            /* Symbols are owned by the caller (TypeSys); only free the bucket. */
            free(b);
            b = nb;
        }
    }
    st->top = s->prev;
    st->level--;
    free(s);
}

int symtab_current_level(SymTab *st)
{
    return st ? st->level : 0;
}

/* ------------------------------------------------------------------ */
/* Insert and lookup                                                   */
/* ------------------------------------------------------------------ */

/*
 * Returns 0 on success, -1 if a symbol with the same name already
 * exists in the current (innermost) scope.
 */
int symtab_insert(SymTab *st, Symbol *sym)
{
    unsigned int h;
    Bucket *b;
    if (!st || !st->top) return -1;
    if (symtab_lookup_local(st, sym->name)) return -1; /* duplicate */
    h = hash_str(sym->name);
    b = (Bucket *)malloc(sizeof(Bucket));
    if (!b) return -1;
    b->sym  = sym;
    b->next = st->top->buckets[h];
    st->top->buckets[h] = b;
    sym->scope_level = st->level;
    return 0;
}

Symbol *symtab_lookup(SymTab *st, const char *name)
{
    Scope       *s;
    unsigned int h;
    if (!st) return NULL;
    h = hash_str(name);
    for (s = st->top; s; s = s->prev) {
        Bucket *b = s->buckets[h];
        while (b) {
            if (strcmp(b->sym->name, name) == 0) return b->sym;
            b = b->next;
        }
    }
    return NULL;
}

Symbol *symtab_lookup_local(SymTab *st, const char *name)
{
    unsigned int h;
    Bucket      *b;
    if (!st || !st->top) return NULL;
    h = hash_str(name);
    b = st->top->buckets[h];
    while (b) {
        if (strcmp(b->sym->name, name) == 0) return b->sym;
        b = b->next;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Symbol allocation helpers                                           */
/* ------------------------------------------------------------------ */

Symbol *make_symbol(const char *name, SymKind kind, BasicType type)
{
    Symbol *s = (Symbol *)calloc(1, sizeof(Symbol));
    if (!s) return NULL;
    s->name = (char *)malloc(strlen(name) + 1);
    if (!s->name) { free(s); return NULL; }
    strcpy(s->name, name);
    s->kind = kind;
    s->type = type;
    return s;
}

void symbol_free(Symbol *s)
{
    if (!s) return;
    free(s->name);
    /* params are owned by the AST node — do not free here */
    free(s);
}
