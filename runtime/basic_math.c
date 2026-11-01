/*
 * basic_math.c -- Math built-in wrappers.
 * C89 compatible.  Link with -lm.
 */

#include <math.h>
#include <stdlib.h>
#include <time.h>
#include "basic_runtime.h"

/* Last value returned by bs_rnd -- used when n == 0 */
static double s_last_rnd = 0.0;

double bs_sin(double x)  { return sin(x); }
double bs_cos(double x)  { return cos(x); }
double bs_tan(double x)  { return tan(x); }
double bs_atn(double x)  { return atan(x); }

double bs_sqr(double x)
{
    if (x < 0.0) bs_raise_error(5, 0); /* Illegal function call */
    return sqrt(x);
}

double bs_log(double x)
{
    if (x <= 0.0) bs_raise_error(5, 0); /* Illegal function call */
    return log(x);
}

double bs_exp(double x)   { return exp(x); }
double bs_abs_d(double x) { return x < 0.0 ? -x : x; }
int    bs_abs_i(int x)    { return x < 0  ? -x : x; }
long   bs_abs_l(long x)   { return x < 0L ? -x : x; }

/* INT -- floor toward -infinity */
double bs_int_fn(double x) { return floor(x); }

/* FIX -- truncate toward zero */
double bs_fix(double x)    { return x >= 0.0 ? floor(x) : ceil(x); }

/*
 * RND(n):
 *   n > 0  -- generate next random value in [0, 1)
 *   n == 0 -- return the last value generated
 *   n < 0  -- reseed with abs(n) and return a new value
 */
double bs_rnd(int n)
{
    double r;
    if (n == 0) {
        return s_last_rnd;
    }
    if (n < 0) {
        srand((unsigned int)(-n));
    }
    r = (double)rand() / ((double)RAND_MAX + 1.0);
    s_last_rnd = r;
    return r;
}

/* RANDOMIZE seed -- seed == -1 uses time(NULL) */
void bs_randomize(long seed)
{
    if (seed == -1L) {
        srand((unsigned int)time(NULL));
    } else {
        srand((unsigned int)seed);
    }
}

int    bs_sgn(double x)   { return x > 0.0 ? 1 : (x < 0.0 ? -1 : 0); }

/* CINT -- round to nearest int (round half away from zero) */
int    bs_cint(double x)  { return (int)(x >= 0.0 ? x + 0.5 : x - 0.5); }

/* CLNG -- round to nearest long */
long   bs_clng(double x)  { return (long)(x >= 0.0 ? x + 0.5 : x - 0.5); }

/* CSNG / CDBL -- type coercions */
float  bs_csng(double x)  { return (float)x; }
double bs_cdbl(float x)   { return (double)x; }
