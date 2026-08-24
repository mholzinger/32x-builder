/* Freestanding shim (see string.h). os_support.h includes <stdio.h>
 * unconditionally, but every stdio user (speex_warning & co.) is
 * overridden in config.h — nothing to declare. */
#ifndef SPX_SHIM_STDIO_H
#define SPX_SHIM_STDIO_H

#ifdef SPX_HOST_TEST
/* Host harness: defer to the real system header. */
#include_next <stdio.h>
#else

#endif /* SPX_HOST_TEST */

#endif
