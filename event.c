#include "config.h"

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <signal.h>

#include "rpbot.h"
#include "event.h"

#define MAX_EVENTS 64

fifo_buffer_t rp_write_buf;
fifo_buffer_t rp_read_buf;

static struct epoll_event ctl_event; // global struct for epoll_ctl
static struct epoll_event events[MAX_EVENTS];

// context information about the event system, used to decide which events
// to listen for on the socket.
static struct {
	// next time the client should try to connect
	uintptr_t next_conn_msec;

	// when the client should stop trying to connect
	uintptr_t conn_timeout_msec;

	// addrinfo list
	struct addrinfo *res0;
	struct addrinfo *res;

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
} ctx;

// reset the addrinfo list
static void
rp_freeaddrinfo(void)
{
	if (ctx.res0) {
		freeaddrinfo(ctx.res0);
	}

	ctx.res0 = NULL;
	ctx.res = NULL;
}

// find the next available RT signal
static int
find_rt_signal()
{
	struct sigaction act;
	int sig = SIGRTMIN;

	while (sig <= SIGRTMAX) {
		if (sigaction(sig, NULL, &act) != 0) {
			if (errno == EINTR) {
				continue;
			}
		} else {
			if (act.sa_flags & SA_SIGINFO) {
				if (act.sa_sigaction == NULL) {
					return sig;
				}
			} else {
				if (act.sa_handler == SIG_DFL) {
					return sig;
				}
			}
		}

		sig++;
	}

	return -1;
}

// initialize signal handling
static int
sig_init(void)
{
	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, ctx.addr_sig); // used for address resolution

	if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
		perror("sigprocmask()");
		abort();
	}

	ctx.sig_fd = signalfd(-1, &mask, SFD_NONBLOCK);

	if (ctx.sig_fd == -1) {
		perror("signalfd()");
		abort();
	}

	// add the signal fd to epoll
	struct epoll_event sig_ctl_event;
	memset(&sig_ctl_event, 0, sizeof(sig_ctl_event));

	sig_ctl_event.data.fd = ctx.sig_fd;
	sig_ctl_event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLET;

	epoll_ctl(ctx.epoll_fd, EPOLL_CTL_ADD, ctx.sig_fd, &sig_ctl_event);

	return 0;
}

int
rp_init(void)
{
	memset(&ctx, 0, sizeof(ctx));
	memset(&ctl_event, 0, sizeof(ctl_event));

	ctx.state = DISCONNECTED;
	ctx.sock_fd = -1;
	ctx.addr_sig = -1;
	ctx.sig_fd = -1;
	ctx.epoll_fd = epoll_create1(0);

	if (ctx.epoll_fd == -1) {
		perror("epoll_create1");
		abort();
	}

	ctx.addr_sig = find_rt_signal();

	if (ctx.addr_sig == -1) {
		perror("find_rt_signal()");
		abort();
	}

	fifo_init(&rp_read_buf);
	fifo_init(&rp_write_buf);

	if (sig_init()) {
		perror("sig_init()");
		abort();
	}

	return 0;
}

// start an asynchronous address resolution
static int
start_resolve(void)
{
	static struct gaicb host;
	static struct gaicb *host_p = &host; // no need for dynamic allocation
	static struct addrinfo hints;
	static struct sigevent sig;

	memset(&hints, 0, sizeof(hints));

	hints.ai_family = AF_UNSPEC; // ipv4 or v6
	hints.ai_socktype = SOCK_STREAM;

	host.ar_service = IRC_PORT;
	host.ar_name = IRC_HOSTNAME;
	host.ar_request = &hints;

	sig.sigev_notify = SIGEV_SIGNAL;
	sig.sigev_signo = ctx.addr_sig;
	sig.sigev_value.sival_ptr = host_p;

	getaddrinfo_a(GAI_NOWAIT, &host_p, 1, &sig);

	return 0;
}

// disconnect and clean up the socket.
static int
rp_disconnect()
{
	epoll_ctl(ctx.epoll_fd, EPOLL_CTL_DEL, ctx.sock_fd, NULL);
	close(ctx.sock_fd);

	ctx.sock_fd = -1;
	ctx.state = DISCONNECTED;
	ctx.next_conn_msec = rp_current_msec + IRC_RETRY_DELAY;
	rp_freeaddrinfo();

	return 0;
}

// attempt a non-blocking connection to the server.
static int
rp_tryconnect(void)
{
	if (ctx.res == NULL) {
		ctx.res = ctx.res0;
	} else {
		ctx.res = ctx.res->ai_next;
	}

	// if the address is null here, then all have been tried
	if (ctx.res == NULL) {
		rp_disconnect();

		return 0;
	}

	if ((ctx.sock_fd = socket(ctx.res->ai_family, ctx.res->ai_socktype,
	                          ctx.res->ai_protocol)) == -1) {
		fprintf(stderr, "failed to create socket\n");
		goto conn_fail;
	}

	int flags = 0;

	if (fcntl(ctx.sock_fd, F_GETFL, 0) == -1) {
		flags = 0;
	}

	if (fcntl(ctx.sock_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
		fprintf(stderr, "failed to set O_NONBLOCK\n");
		goto conn_fail;
	}

	if (connect(ctx.sock_fd, ctx.res->ai_addr, ctx.res->ai_addrlen) == 0) {
		// if connect succeeded, something went terribly wrong, because
		// this is a non-blocking socket.
		goto conn_fail;
	}

	if (errno != EINPROGRESS) {
		fprintf(stderr, "error on connect() %d\n", errno);
		goto conn_fail;
	}

	ctx.state = CONNECTING;
	ctx.conn_timeout_msec = rp_current_msec + IRC_CONNECT_TIMEOUT;

	// listen for events on the socket
	ctl_event.data.fd = ctx.sock_fd;
	ctl_event.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLERR | EPOLLET;

	epoll_ctl(ctx.epoll_fd, EPOLL_CTL_ADD, ctx.sock_fd, &ctl_event);

	return 0;

conn_fail:
	if (ctx.sock_fd != -1) {
		close(ctx.sock_fd);
	}

	ctx.next_conn_msec = rp_current_msec + IRC_RETRY_DELAY;
	return -1;
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
		n_read = read(ctx.sock_fd, p, max_read);

		if (n_read < 0) {
			if (errno != EAGAIN) {
				perror("read()");
				abort();
			}

			break;
		} else if (n_read == 0) {
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
		n_written = write(ctx.sock_fd, p, max_write);

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
handle_sock_event(struct rp_events *evs, struct epoll_event *e)
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
		if (ctx.state == CONNECTING) {
			// if connecting, then EPOLLOUT means the connection has
			// succeeded.
			ctl_event.data.fd = ctx.sock_fd;
			ctl_event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLET;

			epoll_ctl(ctx.epoll_fd, EPOLL_CTL_MOD, ctx.sock_fd, &ctl_event);

			printf("Connected\n");
			ctx.state = CONNECTED;
			evs->connected = 1;
		} else if (fifo_count(rp_write_buf) > 0) {
			do_write();
		}
	}

	return 0;
}

static int
handle_sig_event(struct rp_events *evs, struct epoll_event *e)
{
	if ((e->events & EPOLLRDHUP) ||
	    (e->events & EPOLLERR) ||
	    (e->events & EPOLLHUP)) {
		fprintf(stderr, "error handling signal\n");
		abort();
	}

	if (e->events & EPOLLIN) {
		struct signalfd_siginfo fdsi;
		ssize_t s;

		while ((s = read(ctx.sig_fd, &fdsi, sizeof(fdsi))) > 0) {
			if (s != sizeof(fdsi)) {
				perror("read(signalfd_signfo)");
				abort();
			}

			if (fdsi.ssi_signo == SIGINT) {
				evs->sig_int = 1;
			} else if (fdsi.ssi_signo == (uint32_t)ctx.addr_sig) {
				struct gaicb *host = (struct gaicb *)fdsi.ssi_ptr;
				// address was resolved
				ctx.res0 = host->ar_result;
				ctx.res = NULL;

				ctx.state = RESOLVED;
			}
		}

		if (s == -1) {
			if (errno != EAGAIN) {
				perror("read != EGAIN");
				abort();
			}
		} else if (s == 0) {
			perror("read(sig_fd)");
			abort();
		}
	}

	return 0;
}

int
rp_poll(struct rp_events *evs, int timeout)
{
	int n;

	if (ctx.state == CONNECTED) {
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
			epoll_ctl(ctx.epoll_fd, EPOLL_CTL_MOD, ctx.sock_fd, &ctl_event);
		}
	}

	n = epoll_wait(ctx.epoll_fd, events, MAX_EVENTS, timeout);

	if (n < 0) {
		perror("epoll_wait");
		abort();
	}

	if (n) {
		int i;

		for (i = 0; i < n; i++) {
			if (events[i].data.fd == ctx.sock_fd) {
				handle_sock_event(evs, &events[i]);
			} else if (events[i].data.fd == ctx.sig_fd) {
				handle_sig_event(evs, &events[i]);
			}
		}

	} else {
		evs->timeout = 1;
	}

	switch (ctx.state) {
	case DISCONNECTED:
		if (rp_current_msec >= ctx.next_conn_msec) {
			if (start_resolve()) {
				fprintf(stderr, "could not resolve\n");
				abort();
			}

			ctx.state = RESOLVING;
		}

		break;
	case CONNECTING:
		if (rp_current_msec >= ctx.conn_timeout_msec) {
			rp_tryconnect();
		}
		break;
	case RESOLVED:
		rp_tryconnect();
		break;
	default:
		break;
	}

	return 0;
}

