#ifndef EVENT_H
#define EVENT_H

#include "fifo.h"

extern fifo_buffer_t rp_read_buf; // socket read buffer
extern fifo_buffer_t rp_write_buf; // socket write buffer

// events that have occurred during rp_poll.
struct rp_events {
	unsigned int connected:1; // the client connected
	unsigned int disconnected:1; // the client disconnected
	unsigned int timeout:1; // the client received no events
};

// initialize the event system.
int
rp_init(void);

// poll for events on the socket, block until an event is received or
// timeout is reached.
int
rp_poll(struct rp_events *, int timeout);

#endif // EVENT_H

