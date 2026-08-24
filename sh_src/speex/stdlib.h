/* Freestanding shim (see string.h). os_support.h includes <stdlib.h>
 * unconditionally, but calloc/free are only referenced inside the
 * OVERRIDE_SPEEX_* guards that config.h closes — nothing to declare. */
#ifndef SPX_SHIM_STDLIB_H
#define SPX_SHIM_STDLIB_H

#ifdef SPX_HOST_TEST
/* Host harness: defer to the real system header. */
#include_next <stdlib.h>
#else

#endif /* SPX_HOST_TEST */

#endif
