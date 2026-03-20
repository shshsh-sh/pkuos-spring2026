#ifndef FIXEDPOINT_H
#define FIXEDPOINT_H

#include <stdint.h>

#define F (1 << 14)  /*> 2^14 = 16384. Lowest 14 bits are seen as fractional bits. */

typedef int32_t fp64;  /*> 17.14 fixed-point number. */

#define INT_TO_FP(N)            ((N) * F)
#define FP_TO_INT_ZERO(N)       ((N) / F)
#define FP_TO_INT_NEAREST(N)    (((N) >= 0) ? ((N) + F / 2) / F : ((N) - F / 2) / F)
#define ADD_FP_INT(X, N)        ((X) + (N) * F)
#define SUB_FP_INT(X, N)        ((X) - (N) * F)
#define MUL_FP(X, Y)            (((int64_t)(X)) * (Y) / F)
#define DIV_FP(X, Y)            (((int64_t)(X)) * F / (Y))

#endif /**< threads/fixedpoint.h */