#include "shared.h"

/* Lives in .bss → SDRAM. Both CPUs link to the same address; access
 * MUST go through the SLAVE_* macros in shared.h to bypass per-CPU
 * caches. */
shared_t shared = { 0 };
