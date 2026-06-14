#include "mars.h"
#include "raycast.h"
#include "shared.h"

/* Slave SH-2 entry point. The crt0 jumps here once the master clears
 * the slave's S_OK wait at COMM4 — see m_main.c for the release fix.
 *
 * d32xr/Doom-32X-style polling dispatcher: watch COMM4 for a non-zero
 * command code, execute it, write 0 back. The heartbeat counter
 * increments every iteration so the master's debug indicator can tell
 * "slave alive" from "slave hung". */
void s_main(void) {
    for (;;) {
        SLAVE_HEARTBEAT++;
        uint16_t cmd = MARS_SYS_COMM4;
        if (cmd == MARS_CMD_NONE) continue;
        switch (cmd) {
        case MARS_CMD_CEILING:
            raycast_draw_ceiling_grid();
            break;
        }
        MARS_SYS_COMM4 = MARS_CMD_NONE;   /* ACK */
    }
}
