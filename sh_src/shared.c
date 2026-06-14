#include "shared.h"

/* Pointer to the dual-CPU shared state in uncached SDRAM. */
volatile shared_state_t * const shared = (volatile shared_state_t *)SHARED_BASE;
