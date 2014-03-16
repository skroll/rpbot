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

// context information about the event system, used to decide which events
// to listen for on the socket.
struct rp_event_ctx {
	struct {
		// next time the client should try to connect
		uintptr_t next_conn_msec;

		// when the client should stop trying to connect
		uintptr_t conn_timeout_msec;

		// addrinfo list
		struct addrinfo *res0;
		struct addrinfo *res;

		struct gaicb host;
		struct gaicb *host_p;

		char *ar_service;
		char *ar_name;

		struct addrinfo hints;
		struct sigevent sig;

		// connection state
		enum {
			DISCONNECTED = 0,
			RESOLVING,
			RESOLVED,
			CONNECTING,
			CONNECTED,
		} state;

		int addr_sig; // address resolution signal
		int sock_fd; // socket fd
		int sig_fd; // signal event fd
		int epoll_fd; // epoll fd

		unsigned int read_full:1; // whether or not the read buf was full
		unsigned int write_full:1; // whether or not the write buf was full
	} p;

	struct rp_config *cfg;
	rp_fifo_t        *read_buf; // socket read buffer
	rp_fifo_t        *write_buf; // socket write buffer
};

// initialize the event system.
int rp_event_init(rp_pool_t *pool, struct rp_event_ctx *);

// poll for events on the socket, block until an event is received or
// timeout is reached.
int rp_event_poll(struct rp_event_ctx *, struct rp_events *, int timeout);

#endif // RP_EVENT_H

