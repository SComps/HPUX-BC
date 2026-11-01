/*
 * basic_array.c -- Heap-allocated array descriptor implementation.
 * C89 compatible.
 * Sub-Task 10.
 */

#include <stdlib.h>
#include <string.h>
#include "basic_runtime.h"

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

static int total_elems(int ndims, const int *lbounds, const int *ubounds)
{
    int i, n = 1;
    for (i = 0; i < ndims; i++)
        n *= (ubounds[i] - lbounds[i] + 1);
    return n;
}

/*
 * Recursive helper for PRESERVE copy.
 * Iterates over the overlap region in all dimensions and memcpy's each leaf.
 */
static void copy_overlap(
    char       *new_data,  const int *new_lb, const int *new_ub,
    const char *old_data,  const int *old_lb, const int *old_ub,
    int ndims, int elem_size,
    const int *ov_lo, const int *ov_hi,
    int *idx, int dim)
{
    int i;
    if (dim == ndims) {
        /* Compute flat index in new array */
        int new_flat = 0, new_stride = 1;
        int old_flat = 0, old_stride = 1;
        for (i = ndims - 1; i >= 0; i--) {
            new_flat   += (idx[i] - new_lb[i]) * new_stride;
            new_stride *= (new_ub[i] - new_lb[i] + 1);
            old_flat   += (idx[i] - old_lb[i]) * old_stride;
            old_stride *= (old_ub[i] - old_lb[i] + 1);
        }
        memcpy(new_data + new_flat * elem_size,
               old_data + old_flat * elem_size,
               (size_t)elem_size);
        return;
    }
    for (i = ov_lo[dim]; i <= ov_hi[dim]; i++) {
        idx[dim] = i;
        copy_overlap(new_data, new_lb, new_ub,
                     old_data, old_lb, old_ub,
                     ndims, elem_size,
                     ov_lo, ov_hi, idx, dim + 1);
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void bsarray_alloc(BArray *a, int elem_size, int ndims,
                   const int *lbounds, const int *ubounds)
{
    int n;
    if (!a) return;
    a->ndims     = ndims;
    a->elem_size = elem_size;

    /* Allocate lbound and ubound as a single contiguous block */
    a->lbound = (int *)malloc((size_t)(ndims * 2) * sizeof(int));
    if (!a->lbound) bs_raise_error(7, 0); /* out of memory */
    a->ubound = a->lbound + ndims;

    memcpy(a->lbound, lbounds, (size_t)ndims * sizeof(int));
    memcpy(a->ubound, ubounds, (size_t)ndims * sizeof(int));

    n = total_elems(ndims, lbounds, ubounds);
    a->data = calloc((size_t)n, (size_t)elem_size);
    if (!a->data) {
        free(a->lbound);
        a->lbound = NULL;
        a->ubound = NULL;
        bs_raise_error(7, 0); /* out of memory */
    }
}

void bsarray_free(BArray *a)
{
    if (!a) return;
    free(a->data);
    free(a->lbound); /* also frees ubound (allocated together) */
    a->data   = NULL;
    a->lbound = NULL;
    a->ubound = NULL;
    a->ndims     = 0;
    a->elem_size = 0;
}

void bsarray_check_bounds(const BArray *a, int dim, int idx)
{
    if (!a || !a->lbound || !a->ubound)
        bs_raise_error(9, 0); /* 9 = subscript out of range */
    if (dim < 0 || dim >= a->ndims)
        bs_raise_error(9, 0);
    if (idx < a->lbound[dim] || idx > a->ubound[dim])
        bs_raise_error(9, 0);
}

void *bsarray_index(const BArray *a, const int *indices)
{
    int i, flat_idx, stride;
    if (!a || !a->data) {
        bs_raise_error(9, 0);
        return NULL;
    }
    flat_idx = 0;
    stride   = 1;
    for (i = a->ndims - 1; i >= 0; i--) {
        bsarray_check_bounds(a, i, indices[i]);
        flat_idx += (indices[i] - a->lbound[i]) * stride;
        stride   *= (a->ubound[i] - a->lbound[i] + 1);
    }
    return (void *)((char *)a->data + flat_idx * a->elem_size);
}

void bsarray_realloc(BArray *a, int ndims, const int *lbounds,
                     const int *ubounds, int preserve)
{
    int   elem_size;
    int   old_ndims;
    int  *old_lb;
    int  *old_ub;
    char *old_data;
    char *new_data;
    int   new_n;

    elem_size  = a->elem_size;
    old_ndims  = a->ndims;
    old_lb     = a->lbound;
    old_ub     = a->ubound; /* points into same block as old_lb */
    old_data   = (char *)a->data;

    /* Detach pointers so bsarray_free doesn't touch them */
    a->data   = NULL;
    a->lbound = NULL;
    a->ubound = NULL;

    new_n    = total_elems(ndims, lbounds, ubounds);
    new_data = (char *)calloc((size_t)new_n, (size_t)elem_size);
    if (!new_data) {
        /* Restore old state before raising */
        a->data      = old_data;
        a->lbound    = old_lb;
        a->ubound    = old_ub;
        a->ndims     = old_ndims;
        a->elem_size = elem_size;
        bs_raise_error(7, 0); /* out of memory */
    }

    {
        int ov_lo[8], ov_hi[8], idx[8];
        int has_overlap;
        int i;
        has_overlap = preserve && old_data && (old_ndims == ndims) && (ndims <= 8);
        if (has_overlap) {
            for (i = 0; i < ndims; i++) {
                ov_lo[i] = old_lb[i] > lbounds[i] ? old_lb[i] : lbounds[i];
                ov_hi[i] = old_ub[i] < ubounds[i] ? old_ub[i] : ubounds[i];
                if (ov_hi[i] < ov_lo[i]) { has_overlap = 0; break; }
            }
        }
        if (has_overlap) {
            copy_overlap(new_data,  lbounds, ubounds,
                         old_data,  old_lb,  old_ub,
                         ndims, elem_size,
                         ov_lo, ov_hi, idx, 0);
        }
    }

    free(old_data);
    free(old_lb); /* frees old_ub too (same block) */

    /* Install new state */
    a->elem_size = elem_size;
    a->data      = NULL; /* set by bsarray_alloc */
    a->lbound    = NULL;
    a->ubound    = NULL;

    a->ndims     = ndims;
    a->lbound    = (int *)malloc((size_t)(ndims * 2) * sizeof(int));
    if (!a->lbound) {
        free(new_data);
        a->elem_size = 0;
        a->ndims     = 0;
        bs_raise_error(7, 0);
    }
    a->ubound = a->lbound + ndims;
    memcpy(a->lbound, lbounds, (size_t)ndims * sizeof(int));
    memcpy(a->ubound, ubounds, (size_t)ndims * sizeof(int));
    a->data = new_data;
}
