#include "config.h"

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/epoll.h>

#include "rpbot.h"
#include "event.h"

#define MAX_EVENTS 64

fifo_buffer_t rp_write_buf;
fifo_buffer_t rp_read_buf;

// the next time the client should try to connect to the server
static uintptr_t rp_next_connect_msec = 0;

static int sfd = -1; // the socket fd
static int efd = -1; // the epoll fd

static struct epoll_event ctl_event; // global struct for epoll_ctl
static struct epoll_event events[MAX_EVENTS];

// addrinfo list
static struct addrinfo *res0 = NULL;
static struct addrinfo *res = NULL;

// connection state
enum {
	RP_STATE_DISCONNECTED = 0,
	RP_STATE_CONNECTING,
	RP_STATE_CONNECTED,
} rp_state = RP_STATE_DISCONNECTED;

// context information about the event system, used to decide which
// events to listen for on the socket.
static struct {
	unsigned int read_full:1; // whether or not the read buf was full
	unsigned int write_full:1; // whether or not the write buf was full
} ctx;

// reset the addrinfo list
static void
rp_freeaddrinfo(void)
{
	if (res0) {
		freeaddrinfo(res0);
	}

	res0 = NULL;
	res = NULL;
}

int
rp_init(void)
{
	memset(&ctx, 0, sizeof(ctx));
	memset(&ctl_event, 0, sizeof(ctl_event));

	efd = epoll_create1(0);

	if (efd == -1) {
		perror("epoll_create1");
		abort();
	}

	fifo_init(&rp_read_buf);
	fifo_init(&rp_write_buf);

	return 0;
}

// attempt a non-blocking connection to the server. a few things happen
// here:
// 1) resolve the address.
//   a) if the address has yet to be resolved, get the addrinfo list.
//   b) if the address has been resolved, try the next one in the list.
//   c) if all the addresses have been tried, then give up.
// 2) create the socket, set it to non-blocking.
// 3) initiate the connection.
static int
rp_tryconnect(void)
{
	static struct addrinfo hints;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if (res == NULL) {
		// the address has not been resolved yet.
		rp_freeaddrinfo();

		if (getaddrinfo(IRC_HOSTNAME, IRC_PORT, &hints, &res0) != 0) {
			fprintf(stderr, "could not resolve hostname '%s'\n", IRC_HOSTNAME);
			return -1;
		}

		res = res0;
	} else {
		// address has already been resolved.
		res = res->ai_next;
	}

	// if the address is null here, then all have been tried
	if (!res) {
		return 0;
	}

	if ((sfd = socket(res->ai_family, res->ai_socktype,
	                  res->ai_protocol)) == -1) {
		fprintf(stderr, "failed to create socket\n");
		return -1;
	}

	int flags = 0;

	if (fcntl(sfd, F_GETFL, 0) == -1) {
		flags = 0;
	}

	if (fcntl(sfd, F_SETFL, flags | O_NONBLOCK) == -1) {
		fprintf(stderr, "failed to set O_NONBLOCK\n");
		close(sfd);
		return -1;
	}

	if (connect(sfd, res->ai_addr, res->ai_addrlen) == 0) {
		// if connect succeeded, something went terribly wrong, because
		// this is a non-blocking socket.
		close(sfd);
		return -1;
	}

	if (errno != EINPROGRESS) {
		fprintf(stderr, "error on connect() %d\n", errno);
		close(sfd);
		return -1;
	}

	rp_state = RP_STATE_CONNECTING;

	// listen for events on the socket
	ctl_event.data.fd = sfd;
	ctl_event.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLERR | EPOLLET;

	epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &ctl_event);

	return 0;
}

// disconnect and clean up the socket.
static int
rp_disconnect()
{
	epoll_ctl(efd, EPOLL_CTL_DEL, sfd, NULL);
	close(sfd);

	sfd = -1;
	rp_state = RP_STATE_DISCONNECTED;
	rp_next_connect_msec = rp_current_msec + IRC_RETRY_DELAY;
	rp_freeaddrinfo();

	return 0;
}

// read bytes from the socket into the read buffer.
// NOTE: there MUST be data available in the fifo read buffer before
//       this is called.
static void
do_read(void)
{
	while (1) {
		size_t max_read;
		ssize_t n_read;
		void *p;

		max_read = fifo_raw_w(&rp_read_buf, &p);
		n_read = read(sfd, p, max_read);

		if (n_read < 0) {
			if (errno != EAGAIN) {
				perror("read()");
				abort();
			}

			break;
		} else if (n_read == 0) {
			// TODO: handle this
			perror("read()");
			abort();
		}

		fifo_reserve(&rp_read_buf, n_read);

		if (fifo_bytes_free(rp_read_buf) == 0) {
			ctx.read_full = 1;
			break;
		}
	}
}

// write data to the socket from the read buffer.
// NOTE: there MUST be data in the fifo buffer before this is called.
static void
do_write(void)
{
	while (1) {
		size_t max_write;
		ssize_t n_written;
		void *p;

		max_write = fifo_raw_r(&rp_write_buf, &p);
		n_written = write(sfd, p, max_write);

		if (n_written < 0) {
			if (errno != EAGAIN) {
				perror("write()");
				abort();
			}

			ctx.write_full = 1;

			break;
		} else if (n_written == 0) {
			perror("write()");
			abort();
		}

		fifo_consume(&rp_write_buf, n_written);

		if (fifo_count(rp_write_buf) == 0) {
			break;
		}
	}
}

// handle a single epoll event.
static int
rp_handleevent(struct rp_events *evs, struct epoll_event *e)
{
	if ((e->events & EPOLLRDHUP) ||
	    (e->events & EPOLLERR) ||
	    (e->events & EPOLLHUP)) {
		rp_disconnect();
		evs->disconnected = 1;

		return 0;
	}

	if ((e->events & EPOLLIN) && (fifo_bytes_free(rp_read_buf) > 0)) {
		do_read();
	}

	if (e->events & EPOLLOUT) {
		if (rp_state == RP_STATE_CONNECTING) {
			// if connecting, then EPOLLOUT means the connection has
			// succeeded.
			ctl_event.data.fd = sfd;
			ctl_event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLET;

			epoll_ctl(efd, EPOLL_CTL_MOD, sfd, &ctl_event);

			rp_state = RP_STATE_CONNECTED;
			evs->connected = 1;
		} else if (fifo_count(rp_write_buf) > 0) {
			do_write();
		}
	}

	return 0;
}

int
rp_poll(struct rp_events *evs, int timeout)
{
	int n;

	if (rp_state == RP_STATE_CONNECTED) {
		char update_epoll = 0;

		ctl_event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLET;

		if (fifo_bytes_free(rp_read_buf) > 0) {
			if (ctx.read_full) {
				update_epoll = 1;
			} else {
				do_read();
			}
		}

		if (fifo_count(rp_write_buf) > 0) {
			if (ctx.write_full) {
				if (!(ctl_event.events & EPOLLOUT)) {
					update_epoll = 1;
					ctl_event.events |= EPOLLOUT;
				}
			} else {
				do_write();
			}
		} else {
			// write buffer is empty, but in a previous loop already
			// set EPOLLOUT, dont do it.
			if (ctl_event.events & EPOLLOUT) {
				update_epoll = 1;
				ctl_event.events &= ~EPOLLOUT;
			}
		}

		if (update_epoll) {
			epoll_ctl(efd, EPOLL_CTL_MOD, sfd, &ctl_event);
		}
	}

	n = epoll_wait(efd, events, MAX_EVENTS, timeout);

	if (n < 0) {
		perror("epoll_wait");
		abort();
	}

	if (n) {
		int i;

		for (i = 0; i < n; i++) {
			rp_handleevent(evs, &events[i]);
		}

	} else {
		evs->timeout = 1;
	}

	switch (rp_state) {
	case RP_STATE_DISCONNECTED:
		if (rp_current_msec >= rp_next_connect_msec) {
			if (rp_tryconnect()) {
				fprintf(stderr, "could not connect\n");
				rp_next_connect_msec = rp_current_msec + IRC_RETRY_DELAY;
			}
		}

		break;
	default:
		break;
	}

	return 0;
}

