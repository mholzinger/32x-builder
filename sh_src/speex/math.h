/* Freestanding shim (see string.h). filters.c includes <math.h>
 * unconditionally, but the FIXED_POINT build never calls libm. */
#ifndef SPX_SHIM_MATH_H
#define SPX_SHIM_MATH_H

#ifdef SPX_HOST_TEST
/* Host harness: defer to the real system header. */
#include_next <math.h>
#else

#endif /* SPX_HOST_TEST */

#endif
