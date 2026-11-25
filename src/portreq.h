/*
 * A maintenance port request that travels through the input queue. The
 * port's handler pushes a marked event behind the keys it already
 * received and keeps the event's number as the live request. The main
 * loop takes the event in its turn and asks mfb_port_req() what it is.
 * Pure code: the boot agent, the diagnostics image and the host tests
 * link it.
 */
#ifndef MFB_PORTREQ_H
#define MFB_PORTREQ_H

#include <stdint.h>

#include <robco/input.h>

/* What an event means for the port's request: mfb_port_req(). */
#define MFB_PORT_NONE   0   /* not a port request: a key for the menu */
#define MFB_PORT_LIVE   1   /* the live request: act on it now */
#define MFB_PORT_STALE  2   /* a request dropped earlier: take no action */

/*
 * The marked event: INPUT_EV_CHAR from INPUT_SRC_SCRIPT with code 0 and
 * the request number, above 0, in value. No key has that form: the
 * `char` command refuses NUL and pushes value 0. live: the number of the request
 * still waiting, or 0 when none waits. The request is live when the
 * numbers match. Any other marked event is stale: an earlier action
 * overtook it and dropped it.
 */
int mfb_port_req(const input_event_t *ev, int32_t live);

#endif
