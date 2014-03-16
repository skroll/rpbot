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

#define IRC_CONNECT_TIMEOUT (5 * 1000)
#define IRC_RETRY_DELAY (30 * 1000)
#define MAX_EVENTS 64

static struct epoll_event ctl_event; // global struct for epoll_ctl
static struct epoll_event events[MAX_EVENTS];

// reset the addrinfo list
static void
rp_freeaddrinfo(struct rp_event_ctx *ctx)
{
	if (ctx->p.res0) {
		freeaddrinfo(ctx->p.res0);
	}

	ctx->p.res0 = NULL;
	ctx->p.res = NULL;
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
	sigaddset(&mask, ctx->p.addr_sig); // used for address resolution

	if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
		perror("sigprocmask()");
		abort();
	}

	ctx->p.sig_fd = signalfd(-1, &mask, SFD_NONBLOCK);

	if (ctx->p.sig_fd == -1) {
		perror("signalfd()");
		abort();
	}

	// add the signal fd to epoll
	struct epoll_event sig_ctl_event;
	memset(&sig_ctl_event, 0, sizeof(sig_ctl_event));

	sig_ctl_event.data.fd = ctx->p.sig_fd;
	sig_ctl_event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLET;

	epoll_ctl(ctx->p.epoll_fd, EPOLL_CTL_ADD, ctx->p.sig_fd, &sig_ctl_event);

	return 0;
}

int
rp_event_init(rp_pool_t *pool, struct rp_event_ctx *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
	memset(&ctl_event, 0, sizeof(ctl_event));

	ctx->p.state = DISCONNECTED;
	ctx->p.sock_fd = -1;
	ctx->p.addr_sig = -1;
	ctx->p.sig_fd = -1;
	ctx->p.epoll_fd = epoll_create1(0);

	if (ctx->p.epoll_fd == -1) {
		perror("epoll_create1");
		abort();
	}

	// TODO: find good values for this
	ctx->p.ar_name = rp_palloc(pool, 256);
	ctx->p.ar_service = rp_palloc(pool, 32);

	memset(&ctx->p.host, 0, sizeof(ctx->p.host));
	ctx->p.host.ar_name = ctx->p.ar_name;
	ctx->p.host.ar_service = ctx->p.ar_service;

	ctx->p.host_p = &ctx->p.host;
	ctx->p.host.ar_request = &ctx->p.hints;

	ctx->p.addr_sig = find_rt_signal();

	if (ctx->p.addr_sig == -1) {
		perror("find_rt_signal()");
		abort();
	}

	if (sig_init(ctx)) {
		perror("sig_init()");
		abort();
	}

	return 0;
}

// start an asynchronous address resolution
static int
start_resolve(struct rp_event_ctx *ctx)
{
	memset(&ctx->p.hints, 0, sizeof(ctx->p.hints));

	ctx->p.hints.ai_family = AF_UNSPEC; // ipv4 or v6
	ctx->p.hints.ai_socktype = SOCK_STREAM;

	struct rp_config_server *server = ctx->cfg->servers;

	memcpy(ctx->p.ar_name, server->host.ptr, server->host.len);
	ctx->p.ar_name[server->host.len] = '\0';

	memcpy(ctx->p.ar_service, server->port.ptr, server->port.len);
	ctx->p.ar_service[server->port.len] = '\0';

	ctx->p.sig.sigev_notify = SIGEV_SIGNAL;
	ctx->p.sig.sigev_signo = ctx->p.addr_sig;
	ctx->p.sig.sigev_value.sival_ptr = ctx->p.host_p;

	getaddrinfo_a(GAI_NOWAIT, &ctx->p.host_p, 1, &ctx->p.sig);

	return 0;
}

// disconnect and clean up the socket.
static int
rp_disconnect(struct rp_event_ctx *ctx)
{
	epoll_ctl(ctx->p.epoll_fd, EPOLL_CTL_DEL, ctx->p.sock_fd, NULL);
	close(ctx->p.sock_fd);

	ctx->p.sock_fd = -1;
	ctx->p.state = DISCONNECTED;
	ctx->p.next_conn_msec = rp_current_msec + IRC_RETRY_DELAY;
	rp_freeaddrinfo(ctx);

	return 0;
}

// attempt a non-blocking connection to the server.
static int
rp_tryconnect(struct rp_event_ctx *ctx)
{
	if (ctx->p.res == NULL) {
		ctx->p.res = ctx->p.res0;
	} else {
		ctx->p.res = ctx->p.res->ai_next;
	}

	// if the address is null here, then all have been tried
	if (ctx->p.res == NULL) {
		rp_disconnect(ctx);

		return 0;
	}

	if ((ctx->p.sock_fd = socket(ctx->p.res->ai_family,
	                             ctx->p.res->ai_socktype,
	                             ctx->p.res->ai_protocol)) == -1) {
		fprintf(stderr, "failed to create socket\n");
		goto conn_fail;
	}

	int flags = 0;

	if (fcntl(ctx->p.sock_fd, F_GETFL, 0) == -1) {
		flags = 0;
	}

	if (fcntl(ctx->p.sock_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
		fprintf(stderr, "failed to set O_NONBLOCK\n");
		goto conn_fail;
	}

	if (connect(ctx->p.sock_fd, ctx->p.res->ai_addr, ctx->p.res->ai_addrlen) == 0) {
		// if connect succeeded, something went terribly wrong, because
		// this is a non-blocking socket.
		goto conn_fail;
	}

	if (errno != EINPROGRESS) {
		fprintf(stderr, "error on connect() %d\n", errno);
		goto conn_fail;
	}

	ctx->p.state = CONNECTING;
	ctx->p.conn_timeout_msec = rp_current_msec + IRC_CONNECT_TIMEOUT;

	// listen for events on the socket
	ctl_event.data.fd = ctx->p.sock_fd;
	ctl_event.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLERR | EPOLLET;

	epoll_ctl(ctx->p.epoll_fd, EPOLL_CTL_ADD, ctx->p.sock_fd, &ctl_event);

	return 0;

conn_fail:
	if (ctx->p.sock_fd != -1) {
		close(ctx->p.sock_fd);
	}

	ctx->p.next_conn_msec = rp_current_msec + IRC_RETRY_DELAY;
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
		n_read = read(ctx->p.sock_fd, p, max_read);

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
			ctx->p.read_full = 1;
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
		n_written = write(ctx->p.sock_fd, p, max_write);

		if (n_written < 0) {
			if (errno != EAGAIN) {
				perror("write()");
				abort();
			}

			ctx->p.write_full = 1;

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
		if (ctx->p.state == CONNECTING) {
			// if connecting, then EPOLLOUT means the connection has
			// succeeded.
			ctl_event.data.fd = ctx->p.sock_fd;
			ctl_event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLET;

			epoll_ctl(ctx->p.epoll_fd, EPOLL_CTL_MOD, ctx->p.sock_fd, &ctl_event);

			printf("Connected\n");
			ctx->p.state = CONNECTED;
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

		while ((s = read(ctx->p.sig_fd, &fdsi, sizeof(fdsi))) > 0) {
			if (s != sizeof(fdsi)) {
				perror("read(signalfd_signfo)");
				abort();
			}

			if (fdsi.ssi_signo == SIGINT) {
				evs->sig_int = 1;
			} else if (fdsi.ssi_signo == (uint32_t)ctx->p.addr_sig) {
				struct gaicb *host = (struct gaicb *)fdsi.ssi_ptr;
				// address was resolved
				ctx->p.res0 = host->ar_result;
				ctx->p.res = NULL;

				ctx->p.state = RESOLVED;
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
rp_event_poll(struct rp_event_ctx *ctx, struct rp_events *evs, int timeout)
{
	if (ctx->p.state == CONNECTED) {
		char update_epoll = 0;

		ctl_event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLET;

		if (rp_fifo_bytes_free(ctx->read_buf) > 0) {
			if (ctx->p.read_full) {
				update_epoll = 1;
			} else {
				do_read(ctx);
			}
		}

		if (rp_fifo_count(ctx->write_buf) > 0) {
			if (ctx->p.write_full) {
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

		if (update_epoll) {
			epoll_ctl(ctx->p.epoll_fd, EPOLL_CTL_MOD, ctx->p.sock_fd,
			          &ctl_event);
		}
	}

	int ret = 0;
	int n = epoll_wait(ctx->p.epoll_fd, events, MAX_EVENTS, timeout);

	if (n < 0) {
		perror("epoll_wait");
		return -1;
	}

	if (n) {
		int i;

		for (i = 0; i < n; i++) {
			if (events[i].data.fd == ctx->p.sock_fd) {
				handle_sock_event(ctx, evs, &events[i]);
			} else if (events[i].data.fd == ctx->p.sig_fd) {
				handle_sig_event(ctx, evs, &events[i]);
			}
		}

		ret = 1;
	}
	// else: epoll timed out

	switch (ctx->p.state) {
	case DISCONNECTED:
		if (rp_current_msec >= ctx->p.next_conn_msec) {
			if (start_resolve(ctx)) {
				fprintf(stderr, "could not resolve\n");
				abort();
			}

			ctx->p.state = RESOLVING;
		}

		break;
	case CONNECTING:
		if (rp_current_msec >= ctx->p.conn_timeout_msec) {
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

