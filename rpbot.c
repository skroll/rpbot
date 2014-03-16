#include "config.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "rpbot.h"
#include "os.h"
#include "fifo.h"
#include "event.h"
#include "irc.h"
#include "slab.h"

#define TIMEOUT 500

uintptr_t rp_current_msec;

// update rp_current_msec with the current time.
static void
rp_updatetime(void)
{
	struct timeval tv;
	time_t sec;
	uintptr_t msec;

	gettimeofday(&tv, NULL);

	sec = tv.tv_sec;
	msec = tv.tv_usec / 1000;

	rp_current_msec = (uintptr_t)sec * 1000 + msec;
}

static void
handle_irc_msg(struct irc_state *istate)
{
	if (istate->message_code.len == 3) {
		if (strncmp(istate->message_code.buf, "004", 3) == 0) {
			const char *jstr = "JOIN " IRC_CHAN "\r\n";
			fifo_put(&rp_write_buf, (void *)jstr, strlen(jstr));
		}
	} else if (istate->message_code.len == 4) {
		if (strncmp(istate->message_code.buf, "PING", 4) == 0) {
			fifo_put(&rp_write_buf, "PONG", 4);
			fifo_put(&rp_write_buf, istate->params.buf, istate->params.len);
			fifo_put(&rp_write_buf, "\r\n", 2);
		}
	}
}

int
main(int argc, const char **argv)
{
	struct irc_state istate;

	(void)argc;
	(void)argv;

	rp_os_init();
	rp_init();
	irc_init(&istate);

	while (1) {
		struct rp_events evs;
		memset(&evs, 0, sizeof(evs));

		rp_updatetime();
		rp_poll(&evs, TIMEOUT);

		if (evs.connected) {
			fprintf(stderr, "connected to host\n");
			const char *connstr = "NICK " IRC_NICK "\r\nUSER " IRC_LOGIN " 8 * : " IRC_NAME "\r\n";
			fifo_put(&rp_write_buf, (void *)connstr, strlen(connstr));
		}

		if (evs.disconnected) {
			fprintf(stderr, "disconnected from host\n");
		}

		if (evs.timeout) {
		}

		if (evs.sig_int) {
			fprintf(stderr, "SIGINT received, terminating...\n");
			abort();
		}

		while (fifo_count(rp_read_buf) > 0) {
			void *p;
			size_t len = fifo_raw_r(&rp_read_buf, &p);

			if (irc_parse(&istate, p, &len)) {
				handle_irc_msg(&istate);
			}

			fifo_consume(&rp_read_buf, len);
		}
	}

	return 0;
}

