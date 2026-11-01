/*
 * ast.c -- AST node allocator, deallocator, and debug pretty-printer.
 * C89 compatible.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

char *c89_strdup(const char *s)
{
    size_t len;
    char *copy;
    if (!s) return NULL;
    len = strlen(s) + 1;
    copy = (char *)malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

static void free_dimspec_array(DimSpec *dims, int ndims)
{
    int i;
    if (!dims) return;
    for (i = 0; i < ndims; i++) {
        free_ast(dims[i].lbound);
        free_ast(dims[i].ubound);
    }
    free(dims);
}

/* ------------------------------------------------------------------ */
/* Public: make_node                                                   */
/* ------------------------------------------------------------------ */

AstNode *make_node(NodeKind kind, int line, int col)
{
    AstNode *n = (AstNode *)calloc(1, sizeof(AstNode));
    if (!n) return NULL;
    n->kind = kind;
    n->line = line;
    n->col  = col;
    return n;
}

/* ------------------------------------------------------------------ */
/* Public: make_param / free_params                                   */
/* ------------------------------------------------------------------ */

ParamDef *make_param(const char *name, BasicType type, int is_byref)
{
    ParamDef *p = (ParamDef *)calloc(1, sizeof(ParamDef));
    if (!p) return NULL;
    p->name     = name ? strdup(name) : NULL;
    p->type     = type;
    p->is_byref = is_byref;
    return p;
}

void free_params(ParamDef *p)
{
    while (p) {
        ParamDef *nx = p->next;
        free(p->name);
        free(p);
        p = nx;
    }
}

/* ------------------------------------------------------------------ */
/* Public: free_ast                                                    */
/* ------------------------------------------------------------------ */

void free_ast(AstNode *node)
{
    AstNode *nx;
    if (!node) return;

    /* Capture sibling before freeing this node */
    nx = node->next;

    switch (node->kind) {

    case ND_BLOCK:
        free_ast(node->u.block.stmts);
        break;

    case ND_ASSIGN:
    case ND_LSET:
    case ND_RSET:
        free_ast(node->u.assign.lhs);
        free_ast(node->u.assign.rhs);
        break;

    case ND_IF:
        free_ast(node->u.if_stmt.cond);
        free_ast(node->u.if_stmt.then_block);
        free_ast(node->u.if_stmt.elseif_chain);
        free_ast(node->u.if_stmt.else_block);
        break;

    case ND_FOR:
        free(node->u.for_stmt.var);
        free_ast(node->u.for_stmt.init);
        free_ast(node->u.for_stmt.limit);
        free_ast(node->u.for_stmt.step);
        free_ast(node->u.for_stmt.body);
        break;

    case ND_WHILE:
        free_ast(node->u.while_stmt.cond);
        free_ast(node->u.while_stmt.body);
        break;

    case ND_DO:
        free_ast(node->u.do_stmt.cond);
        free_ast(node->u.do_stmt.body);
        break;

    case ND_SELECT:
        free_ast(node->u.select_stmt.expr);
        free_ast(node->u.select_stmt.cases);
        break;

    case ND_GOSUB:
    case ND_GOTO:
        free(node->u.jump.label);
        break;

    case ND_LABEL:
        free(node->u.label.name);
        break;

    case ND_SUB_DEF:
    case ND_FUNC_DEF:
        free(node->u.sub_def.name);
        free_params(node->u.sub_def.params);
        free_ast(node->u.sub_def.body);
        break;

    case ND_CALL:
        free(node->u.call.name);
        free_ast(node->u.call.args);
        break;

    case ND_DIM:
    case ND_REDIM:
        free(node->u.dim.name);
        free_dimspec_array(node->u.dim.dims, node->u.dim.ndims);
        break;

    case ND_TYPE_DEF:
        free(node->u.type_def.name);
        free_params(node->u.type_def.fields);
        break;

    case ND_PRINT:
        free_ast(node->u.print.items);
        free_ast(node->u.print.fmt);
        free_ast(node->u.print.filenum);
        break;

    case ND_INPUT:
    case ND_LINE_INPUT:
        free_ast(node->u.input.vars);
        free_ast(node->u.input.prompt);
        free_ast(node->u.input.filenum);
        break;

    case ND_OPEN:
        free_ast(node->u.open.path);
        free_ast(node->u.open.filenum);
        free_ast(node->u.open.reclen);
        break;

    case ND_CLOSE:
        free_ast(node->u.close_stmt.filenum);
        break;

    case ND_READ:
    case ND_WRITE:
    case ND_GET:
    case ND_PUT:
    case ND_FIELD:
        free_ast(node->u.file_io.filenum);
        free_ast(node->u.file_io.recno);
        free_ast(node->u.file_io.vars);
        break;

    case ND_TRY:
        free_ast(node->u.try_stmt.body);
        free_ast(node->u.try_stmt.catch_var);
        free_ast(node->u.try_stmt.catch_body);
        free_ast(node->u.try_stmt.finally_body);
        break;

    case ND_ON_ERROR:
        free(node->u.on_error.label);
        break;

    case ND_BINOP:
    case ND_UNOP:
        free_ast(node->u.binop.left);
        free_ast(node->u.binop.right);
        break;

    case ND_STRING_LIT:
        free(node->u.string_lit.sval);
        break;

    case ND_VAR:
        free(node->u.var.name);
        break;

    case ND_ARRAY_INDEX:
        free_ast(node->u.array_index.array);
        free_ast(node->u.array_index.indices);
        break;

    case ND_FIELD_ACCESS:
        free_ast(node->u.field_access.record);
        free(node->u.field_access.field);
        break;

    case ND_FUNC_CALL:
        free(node->u.func_call.name);
        free_ast(node->u.func_call.args);
        break;

    case ND_CAST:
        free_ast(node->u.cast.expr);
        break;

    case ND_MID_ASSIGN:
        free_ast(node->u.mid_assign.target);
        free_ast(node->u.mid_assign.start);
        free_ast(node->u.mid_assign.length);
        free_ast(node->u.mid_assign.value);
        break;

    /* Nodes with no heap-allocated children */
    case ND_INT_LIT:
    case ND_LONG_LIT:
    case ND_FLOAT_LIT:
    case ND_DOUBLE_LIT:
    case ND_RETURN:
    case ND_CATCH:
    case ND_RESUME_NEXT:
    case ND_EXIT_SUB:
    case ND_EXIT_FOR:
    case ND_EXIT_DO:
    case ND_EXIT_FUNC:
        break;

    case ND_SHELL:
        free_ast(node->u.shell.cmd);
        break;

    case ND_CLS:
    case ND_END:
    case ND_LPRINT:      /* reuses print union — children freed via ND_PRINT path above */
        break;

    case ND_LPRINT_OPEN:
        free_ast(node->u.lprint_open.printer);
        break;

    case ND_LOCATE:
        free_ast(node->u.locate.row);
        free_ast(node->u.locate.col);
        break;

    case ND_COLOR:
    case ND_BGCOLOR:
        free_ast(node->u.color.fg);
        free_ast(node->u.color.bg);
        break;

    default:
        break;
    }

    free(node);
    free_ast(nx); /* recurse down sibling chain */
}

/* ------------------------------------------------------------------ */
/* Public: print_ast (debug pretty-printer)                           */
/* ------------------------------------------------------------------ */

static void indent_print(int indent)
{
    int i;
    for (i = 0; i < indent; i++) putchar(' ');
}

static const char *kind_name(NodeKind k)
{
    switch (k) {
    case ND_ASSIGN:      return "ASSIGN";
    case ND_IF:          return "IF";
    case ND_FOR:         return "FOR";
    case ND_WHILE:       return "WHILE";
    case ND_DO:          return "DO";
    case ND_SELECT:      return "SELECT";
    case ND_GOSUB:       return "GOSUB";
    case ND_GOTO:        return "GOTO";
    case ND_RETURN:      return "RETURN";
    case ND_SUB_DEF:     return "SUB_DEF";
    case ND_FUNC_DEF:    return "FUNC_DEF";
    case ND_CALL:        return "CALL";
    case ND_DIM:         return "DIM";
    case ND_REDIM:       return "REDIM";
    case ND_TYPE_DEF:    return "TYPE_DEF";
    case ND_PRINT:       return "PRINT";
    case ND_LPRINT:      return "LPRINT";
    case ND_LPRINT_OPEN: return "LPRINT_OPEN";
    case ND_INPUT:       return "INPUT";
    case ND_LINE_INPUT:  return "LINE_INPUT";
    case ND_OPEN:        return "OPEN";
    case ND_CLOSE:       return "CLOSE";
    case ND_READ:        return "READ";
    case ND_WRITE:       return "WRITE";
    case ND_GET:         return "GET";
    case ND_PUT:         return "PUT";
    case ND_FIELD:       return "FIELD";
    case ND_LSET:        return "LSET";
    case ND_RSET:        return "RSET";
    case ND_TRY:         return "TRY";
    case ND_CATCH:       return "CATCH";
    case ND_ON_ERROR:    return "ON_ERROR";
    case ND_RESUME_NEXT: return "RESUME_NEXT";
    case ND_EXIT_SUB:    return "EXIT_SUB";
    case ND_EXIT_FOR:    return "EXIT_FOR";
    case ND_EXIT_DO:     return "EXIT_DO";
    case ND_EXIT_FUNC:   return "EXIT_FUNC";
    case ND_SHELL:       return "SHELL";
    case ND_CLS:         return "CLS";
    case ND_LOCATE:      return "LOCATE";
    case ND_COLOR:       return "COLOR";
    case ND_BGCOLOR:     return "BGCOLOR";
    case ND_END:         return "END";
    case ND_LABEL:       return "LABEL";
    case ND_BLOCK:       return "BLOCK";
    case ND_BINOP:       return "BINOP";
    case ND_UNOP:        return "UNOP";
    case ND_INT_LIT:     return "INT_LIT";
    case ND_LONG_LIT:    return "LONG_LIT";
    case ND_FLOAT_LIT:   return "FLOAT_LIT";
    case ND_DOUBLE_LIT:  return "DOUBLE_LIT";
    case ND_STRING_LIT:  return "STRING_LIT";
    case ND_VAR:         return "VAR";
    case ND_ARRAY_INDEX: return "ARRAY_INDEX";
    case ND_FIELD_ACCESS:return "FIELD_ACCESS";
    case ND_FUNC_CALL:   return "FUNC_CALL";
    case ND_CAST:        return "CAST";
    case ND_MID_ASSIGN:  return "MID_ASSIGN";
    default:             return "?";
    }
}

void print_ast(AstNode *n, int indent)
{
    if (!n) return;

    indent_print(indent);
    printf("[%s] line=%d col=%d", kind_name(n->kind), n->line, n->col);

    switch (n->kind) {
    case ND_INT_LIT:
        printf(" ival=%d\n", n->u.int_lit.ival);
        break;
    case ND_LONG_LIT:
        printf(" lval=%ld\n", n->u.long_lit.lval);
        break;
    case ND_FLOAT_LIT:
        printf(" fval=%g\n", (double)n->u.float_lit.fval);
        break;
    case ND_DOUBLE_LIT:
        printf(" dval=%g\n", n->u.double_lit.dval);
        break;
    case ND_STRING_LIT:
        printf(" sval=\"%s\"\n",
               n->u.string_lit.sval ? n->u.string_lit.sval : "");
        break;
    case ND_VAR:
        printf(" name=%s\n", n->u.var.name ? n->u.var.name : "");
        break;
    case ND_LABEL:
        printf(" name=%s\n", n->u.label.name ? n->u.label.name : "");
        break;
    case ND_GOTO:
    case ND_GOSUB:
        printf(" label=%s\n", n->u.jump.label ? n->u.jump.label : "");
        break;
    case ND_BINOP:
    case ND_UNOP:
        printf(" op=%d\n", n->u.binop.op);
        print_ast(n->u.binop.left,  indent + 2);
        print_ast(n->u.binop.right, indent + 2);
        break;
    case ND_ASSIGN:
    case ND_LSET:
    case ND_RSET:
        putchar('\n');
        print_ast(n->u.assign.lhs, indent + 2);
        print_ast(n->u.assign.rhs, indent + 2);
        break;
    case ND_BLOCK:
        putchar('\n');
        print_ast(n->u.block.stmts, indent + 2);
        break;
    case ND_IF:
        putchar('\n');
        print_ast(n->u.if_stmt.cond,         indent + 2);
        print_ast(n->u.if_stmt.then_block,   indent + 2);
        print_ast(n->u.if_stmt.elseif_chain, indent + 2);
        print_ast(n->u.if_stmt.else_block,   indent + 2);
        break;
    case ND_FOR:
        printf(" var=%s\n",
               n->u.for_stmt.var ? n->u.for_stmt.var : "");
        print_ast(n->u.for_stmt.init,  indent + 2);
        print_ast(n->u.for_stmt.limit, indent + 2);
        print_ast(n->u.for_stmt.step,  indent + 2);
        print_ast(n->u.for_stmt.body,  indent + 2);
        break;
    case ND_WHILE:
        putchar('\n');
        print_ast(n->u.while_stmt.cond, indent + 2);
        print_ast(n->u.while_stmt.body, indent + 2);
        break;
    case ND_DO:
        printf(" is_until=%d check_at_top=%d\n",
               n->u.do_stmt.is_until, n->u.do_stmt.check_at_top);
        print_ast(n->u.do_stmt.cond, indent + 2);
        print_ast(n->u.do_stmt.body, indent + 2);
        break;
    case ND_SELECT:
        putchar('\n');
        print_ast(n->u.select_stmt.expr,  indent + 2);
        print_ast(n->u.select_stmt.cases, indent + 2);
        break;
    case ND_SUB_DEF:
    case ND_FUNC_DEF:
        printf(" name=%s\n",
               n->u.sub_def.name ? n->u.sub_def.name : "");
        print_ast(n->u.sub_def.body, indent + 2);
        break;
    case ND_CALL:
        printf(" name=%s\n", n->u.call.name ? n->u.call.name : "");
        print_ast(n->u.call.args, indent + 2);
        break;
    case ND_FUNC_CALL:
        printf(" name=%s\n",
               n->u.func_call.name ? n->u.func_call.name : "");
        print_ast(n->u.func_call.args, indent + 2);
        break;
    case ND_DIM:
    case ND_REDIM:
        printf(" name=%s ndims=%d preserve=%d\n",
               n->u.dim.name ? n->u.dim.name : "",
               n->u.dim.ndims, n->u.dim.preserve);
        break;
    case ND_TYPE_DEF:
        printf(" name=%s\n",
               n->u.type_def.name ? n->u.type_def.name : "");
        break;
    case ND_PRINT:
        printf(" to_file=%d\n", n->u.print.to_file);
        print_ast(n->u.print.filenum, indent + 2);
        print_ast(n->u.print.items,   indent + 2);
        print_ast(n->u.print.fmt,     indent + 2);
        break;
    case ND_INPUT:
    case ND_LINE_INPUT:
        printf(" from_file=%d\n", n->u.input.from_file);
        print_ast(n->u.input.filenum, indent + 2);
        print_ast(n->u.input.prompt,  indent + 2);
        print_ast(n->u.input.vars,    indent + 2);
        break;
    case ND_OPEN:
        printf(" mode=%d\n", n->u.open.mode);
        print_ast(n->u.open.path,    indent + 2);
        print_ast(n->u.open.filenum, indent + 2);
        print_ast(n->u.open.reclen,  indent + 2);
        break;
    case ND_CLOSE:
        putchar('\n');
        print_ast(n->u.close_stmt.filenum, indent + 2);
        break;
    case ND_READ:
    case ND_WRITE:
    case ND_GET:
    case ND_PUT:
    case ND_FIELD:
        putchar('\n');
        print_ast(n->u.file_io.filenum, indent + 2);
        print_ast(n->u.file_io.recno,   indent + 2);
        print_ast(n->u.file_io.vars,    indent + 2);
        break;
    case ND_TRY:
        putchar('\n');
        print_ast(n->u.try_stmt.body,         indent + 2);
        print_ast(n->u.try_stmt.catch_var,    indent + 2);
        print_ast(n->u.try_stmt.catch_body,   indent + 2);
        print_ast(n->u.try_stmt.finally_body, indent + 2);
        break;
    case ND_ON_ERROR:
        printf(" label=%s goto_zero=%d\n",
               n->u.on_error.label ? n->u.on_error.label : "",
               n->u.on_error.goto_zero);
        break;
    case ND_ARRAY_INDEX:
        putchar('\n');
        print_ast(n->u.array_index.array,   indent + 2);
        print_ast(n->u.array_index.indices, indent + 2);
        break;
    case ND_FIELD_ACCESS:
        printf(" field=%s\n",
               n->u.field_access.field ? n->u.field_access.field : "");
        print_ast(n->u.field_access.record, indent + 2);
        break;
    case ND_CAST:
        printf(" to_type=%d\n", (int)n->u.cast.to_type);
        print_ast(n->u.cast.expr, indent + 2);
        break;
    case ND_MID_ASSIGN:
        putchar('\n');
        print_ast(n->u.mid_assign.target, indent + 2);
        print_ast(n->u.mid_assign.start,  indent + 2);
        print_ast(n->u.mid_assign.length, indent + 2);
        print_ast(n->u.mid_assign.value,  indent + 2);
        break;
    default:
        putchar('\n');
        break;
    }

    /* Print siblings at the same indent level */
    print_ast(n->next, indent);
}
