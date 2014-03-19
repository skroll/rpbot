#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <signal.h>
#include <rpbot.h>
#include <rp_event.h>

// context information about the event system, used to decide which events
// to listen for on the socket.
struct rp_event_ctx {
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

	struct rp_config *cfg;
	rp_fifo_t        *read_buf; // socket read buffer
	rp_fifo_t        *write_buf; // socket write buffer
};

#define IRC_CONNECT_TIMEOUT (5 * 1000)
#define IRC_RETRY_DELAY (30 * 1000)
#define MAX_EVENTS 64

// reset the addrinfo list
static void
rp_freeaddrinfo(struct rp_event_ctx *ctx)
{
	if (ctx->res0) {
		freeaddrinfo(ctx->res0);
	}

	ctx->res0 = NULL;
	ctx->res = NULL;
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
sig_init(struct rp_event_ctx *ctx)
{
	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, ctx->addr_sig); // used for address resolution

	if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
		perror("sigprocmask()");
		abort();
	}

	ctx->sig_fd = signalfd(-1, &mask, SFD_NONBLOCK);

	if (ctx->sig_fd == -1) {
		perror("signalfd()");
		abort();
	}

	// add the signal fd to epoll
	struct epoll_event sig_ctl_event;
	memset(&sig_ctl_event, 0, sizeof(sig_ctl_event));
	sig_ctl_event.data.fd = ctx->sig_fd;
	sig_ctl_event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLET;

	epoll_ctl(ctx->epoll_fd, EPOLL_CTL_ADD, ctx->sig_fd, &sig_ctl_event);

	return 0;
}

int rp_event_init(rp_pool_t *pool, struct rp_config *cfg,
	rp_fifo_t *read_buf, rp_fifo_t *write_buf, struct rp_event_ctx **ctx)
{
	struct rp_event_ctx *c;
	c = rp_pcalloc(pool, sizeof(*c));

	c->state = DISCONNECTED;
	c->sock_fd = -1;
	c->addr_sig = -1;
	c->sig_fd = -1;
	c->epoll_fd = epoll_create1(0);

	if (c->epoll_fd == -1) {
		perror("epoll_create1");
		abort();
	}

	// TODO: find good values for this
	c->ar_name = rp_palloc(pool, 256);
	c->ar_service = rp_palloc(pool, 32);

	memset(&c->host, 0, sizeof(c->host));
	c->host.ar_name = c->ar_name;
	c->host.ar_service = c->ar_service;

	c->host_p = &c->host;
	c->host.ar_request = &c->hints;

	c->addr_sig = find_rt_signal();

	if (c->addr_sig == -1) {
		perror("find_rt_signal()");
		abort();
	}

	if (sig_init(c)) {
		perror("sig_init()");
		abort();
	}

	c->cfg = cfg;
	c->read_buf = read_buf;
	c->write_buf = write_buf;

	*ctx = c;

	return 0;
}

// start an asynchronous address resolution
static int
start_resolve(struct rp_event_ctx *ctx)
{
	memset(&ctx->hints, 0, sizeof(ctx->hints));

	ctx->hints.ai_family = AF_UNSPEC; // ipv4 or v6
	ctx->hints.ai_socktype = SOCK_STREAM;

	struct rp_config_server *server = ctx->cfg->servers;

	memcpy(ctx->ar_name, server->host.ptr, server->host.len);
	ctx->ar_name[server->host.len] = '\0';

	memcpy(ctx->ar_service, server->port.ptr, server->port.len);
	ctx->ar_service[server->port.len] = '\0';

	ctx->sig.sigev_notify = SIGEV_SIGNAL;
	ctx->sig.sigev_signo = ctx->addr_sig;
	ctx->sig.sigev_value.sival_ptr = ctx->host_p;

	getaddrinfo_a(GAI_NOWAIT, &ctx->host_p, 1, &ctx->sig);

	return 0;
}

// disconnect and clean up the socket.
static int
rp_disconnect(struct rp_event_ctx *ctx)
{
	epoll_ctl(ctx->epoll_fd, EPOLL_CTL_DEL, ctx->sock_fd, NULL);
	close(ctx->sock_fd);

	ctx->sock_fd = -1;
	ctx->state = DISCONNECTED;
	ctx->next_conn_msec = rp_current_msec + IRC_RETRY_DELAY;
	rp_freeaddrinfo(ctx);

	return 0;
}

// attempt a non-blocking connection to the server.
static int
rp_tryconnect(struct rp_event_ctx *ctx)
{
	if (ctx->res == NULL) {
		ctx->res = ctx->res0;
	} else {
		ctx->res = ctx->res->ai_next;
	}

	// if the address is null here, then all have been tried
	if (ctx->res == NULL) {
		rp_disconnect(ctx);

		return 0;
	}

	if ((ctx->sock_fd = socket(ctx->res->ai_family,
	                           ctx->res->ai_socktype,
	                           ctx->res->ai_protocol)) == -1) {
		fprintf(stderr, "failed to create socket\n");
		goto conn_fail;
	}

	int flags = 0;

	if (fcntl(ctx->sock_fd, F_GETFL, 0) == -1) {
		flags = 0;
	}

	if (fcntl(ctx->sock_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
		fprintf(stderr, "failed to set O_NONBLOCK\n");
		goto conn_fail;
	}

	if (connect(ctx->sock_fd, ctx->res->ai_addr, ctx->res->ai_addrlen) == 0) {
		// if connect succeeded, something went terribly wrong, because
		// this is a non-blocking socket.
		goto conn_fail;
	}

	if (errno != EINPROGRESS) {
		fprintf(stderr, "error on connect() %d\n", errno);
		goto conn_fail;
	}

	ctx->state = CONNECTING;
	ctx->conn_timeout_msec = rp_current_msec + IRC_CONNECT_TIMEOUT;

	// listen for events on the socket
	struct epoll_event ctl_event;
	memset(&ctl_event, 0, sizeof(ctl_event));
	ctl_event.data.fd = ctx->sock_fd;
	ctl_event.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLERR | EPOLLET;

	epoll_ctl(ctx->epoll_fd, EPOLL_CTL_ADD, ctx->sock_fd, &ctl_event);

	return 0;

conn_fail:
	if (ctx->sock_fd != -1) {
		close(ctx->sock_fd);
	}

	ctx->next_conn_msec = rp_current_msec + IRC_RETRY_DELAY;
	return -1;
}

// read bytes from the socket into the read buffer.
// NOTE: there MUST be data available in the fifo read buffer before
//       this is called.
static void
do_read(struct rp_event_ctx *ctx)
{
	while (1) {
		size_t max_read;
		ssize_t n_read;
		void *p;

		max_read = rp_fifo_raw_w(ctx->read_buf, &p);
		n_read = read(ctx->sock_fd, p, max_read);

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

		rp_fifo_reserve(ctx->read_buf, n_read);

		if (rp_fifo_bytes_free(ctx->read_buf) == 0) {
			ctx->read_full = 1;
			break;
		}
	}
}

// write data to the socket from the read buffer.
// NOTE: there MUST be data in the fifo buffer before this is called.
static void
do_write(struct rp_event_ctx *ctx)
{
	while (1) {
		size_t max_write;
		ssize_t n_written;
		void *p;

		max_write = rp_fifo_raw_r(ctx->write_buf, &p);
		n_written = write(ctx->sock_fd, p, max_write);

		if (n_written < 0) {
			if (errno != EAGAIN) {
				perror("write()");
				abort();
			}

			ctx->write_full = 1;

			break;
		} else if (n_written == 0) {
			perror("write()");
			abort();
		}

		rp_fifo_consume(ctx->write_buf, n_written);

		if (rp_fifo_count(ctx->write_buf) == 0) {
			break;
		}
	}
}

// handle a single epoll event.
static int
handle_sock_event(struct rp_event_ctx *ctx, struct rp_events *evs,
	struct epoll_event *e)
{
	if ((e->events & EPOLLRDHUP) ||
	    (e->events & EPOLLERR) ||
	    (e->events & EPOLLHUP)) {
		rp_disconnect(ctx);
		evs->disconnected = 1;

		return 0;
	}

	if ((e->events & EPOLLIN) && (rp_fifo_bytes_free(ctx->read_buf) > 0)) {
		do_read(ctx);
	}

	if (e->events & EPOLLOUT) {
		if (ctx->state == CONNECTING) {
			// if connecting, then EPOLLOUT means the connection has
			// succeeded.
			struct epoll_event ctl_event;
			memset(&ctl_event, 0, sizeof(ctl_event));
			ctl_event.data.fd = ctx->sock_fd;
			ctl_event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLET;

			epoll_ctl(ctx->epoll_fd, EPOLL_CTL_MOD, ctx->sock_fd, &ctl_event);

			ctx->state = CONNECTED;
			evs->connected = 1;
		} else if (rp_fifo_count(ctx->write_buf) > 0) {
			do_write(ctx);
		}
	}

	return 0;
}

static int
handle_sig_event(struct rp_event_ctx *ctx, struct rp_events *evs,
	struct epoll_event *e)
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

		while ((s = read(ctx->sig_fd, &fdsi, sizeof(fdsi))) > 0) {
			if (s != sizeof(fdsi)) {
				perror("read(signalfd_signfo)");
				abort();
			}

			if (fdsi.ssi_signo == SIGINT) {
				evs->sig_int = 1;
			} else if (fdsi.ssi_signo == (uint32_t)ctx->addr_sig) {
				struct gaicb *host = (struct gaicb *)fdsi.ssi_ptr;
				// address was resolved
				ctx->res0 = host->ar_result;
				ctx->res = NULL;

				ctx->state = RESOLVED;
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

static int
rp_tryflush(struct rp_event_ctx *ctx)
{
	char update_epoll = 0;

	struct epoll_event ctl_event;
	memset(&ctl_event, 0, sizeof(ctl_event));

	ctl_event.data.fd = ctx->sock_fd;
	ctl_event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLET;

	if (rp_fifo_bytes_free(ctx->read_buf) > 0) {
		if (ctx->read_full) {
			update_epoll = 1;
		} else {
			do_read(ctx);
		}
	}

	if (rp_fifo_count(ctx->write_buf) > 0) {
		if (ctx->write_full) {
			if (!(ctl_event.events & EPOLLOUT)) {
				update_epoll = 1;
				ctl_event.events |= EPOLLOUT;
			}
		} else {
			do_write(ctx);
		}
	} else {
		// write buffer is empty, but in a previous loop already
		// set EPOLLOUT, dont do it.
		if (ctl_event.events & EPOLLOUT) {
			update_epoll = 1;
			ctl_event.events &= ~EPOLLOUT;
		}
	}

	int ret = 0;

	if (update_epoll) {
		ret = epoll_ctl(ctx->epoll_fd, EPOLL_CTL_MOD, ctx->sock_fd,
		                &ctl_event);
	}

	return ret;
}

int
rp_event_poll(struct rp_event_ctx *ctx, struct rp_events *evs, int timeout)
{
	if (ctx->state == CONNECTED) {
		rp_tryflush(ctx);
	}

	int ret = 0;
	struct epoll_event events[MAX_EVENTS];

	int n = epoll_wait(ctx->epoll_fd, events, MAX_EVENTS, timeout);

	if (n < 0) {
		switch (errno) {
		case EINTR: // this can happen when gdb is attached
			return 0;
		default:
			perror("epoll_wait");
			return -1;
		}
	}

	if (n) {
		int i;

		for (i = 0; i < n; i++) {
			if (events[i].data.fd == ctx->sock_fd) {
				handle_sock_event(ctx, evs, &events[i]);
			} else if (events[i].data.fd == ctx->sig_fd) {
				handle_sig_event(ctx, evs, &events[i]);
			}
		}

		ret = 1;
	}
	// else: epoll timed out

	switch (ctx->state) {
	case DISCONNECTED:
		if (rp_current_msec >= ctx->next_conn_msec) {
			if (start_resolve(ctx)) {
				fprintf(stderr, "could not resolve\n");
				abort();
			}

			ctx->state = RESOLVING;
		}

		break;
	case CONNECTING:
		if (rp_current_msec >= ctx->conn_timeout_msec) {
			rp_tryconnect(ctx);
		}
		break;
	case RESOLVED:
		rp_tryconnect(ctx);
		break;
	default:
		break;
	}

	return ret;
}

