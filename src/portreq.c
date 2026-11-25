#include "portreq.h"

int mfb_port_req(const input_event_t *ev, int32_t live)
{
    if (ev->type != INPUT_EV_CHAR || ev->source != INPUT_SRC_SCRIPT || ev->code != 0 ||
        ev->value <= 0) {
        return MFB_PORT_NONE;
    }
    return live != 0 && ev->value == live ? MFB_PORT_LIVE : MFB_PORT_STALE;
}
