#ifndef RP_EVENT_H
#define RP_EVENT_H

#include <stdint.h>
#include <sys/signalfd.h>
#include <netdb.h>
#include <rp_palloc.h>
#include <rp_fifo.h>
#include <rp_config.h>

// events that have occurred during rp_poll.
struct rp_events {
	unsigned int connected:1; // the client connected
	unsigned int disconnected:1; // the client disconnected

	unsigned int sig_int:1;
};

struct rp_event_ctx;

// initialize the event system.
int rp_event_init(rp_pool_t *pool, struct rp_config *cfg,
	rp_fifo_t *read_buf, rp_fifo_t *write_buf, struct rp_event_ctx **ctx);

// poll for events on the socket, block until an event is received or
// timeout is reached.
int rp_event_poll(struct rp_event_ctx *, struct rp_events *, int timeout);

#endif // RP_EVENT_H

