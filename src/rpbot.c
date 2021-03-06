#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <rp_os.h>
#include <rp_fifo.h>
#include <rp_slab.h>
#include <rp_palloc.h>
#include <rp_options.h>
#include <rpbot.h>
#include <rp_event.h>
#include <rp_irc.h>
#include <rp_config.h>

#define TIMEOUT 500

uintptr_t rp_current_msec;

struct rp_ctx {
	rp_pool_t        *pool;
	struct rp_config  cfg;
	rp_fifo_t        *read_buf;
	rp_fifo_t        *write_buf;
};

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

#define IRC_BUFFER_SZ 2048

static int
main_loop(struct rp_ctx *ctx)
{
	struct rp_irc_ctx   *irc_ctx;
	struct rp_event_ctx *ev_ctx;

	rp_event_init(ctx->pool, &ctx->cfg, ctx->read_buf, ctx->write_buf, &ev_ctx);
	rp_irc_init(ctx->pool, &ctx->cfg, ctx->write_buf, &irc_ctx);

	while (1) {
		struct rp_events evs;
		memset(&evs, 0, sizeof(evs));

		rp_updatetime();
		int r = rp_event_poll(ev_ctx, &evs, TIMEOUT);

		if (r < 0) {
			return -1;
		} else if (r > 0) {
			if (evs.connected) {
				fprintf(stderr, "connected to host\n");
				rp_irc_onconnect(irc_ctx);
			}

			if (evs.disconnected) {
				fprintf(stderr, "disconnected from host\n");
			}

			if (evs.sig_int) {
				fprintf(stderr, "SIGINT received, terminating...\n");
				return -1;
			}
		}

		while (rp_fifo_count(ctx->read_buf) > 0) {
			void *p;
			size_t len = rp_fifo_raw_r(ctx->read_buf, &p);

			if (rp_irc_parse(irc_ctx, p, &len)) {
				rp_irc_handle(irc_ctx);
			}

			rp_fifo_consume(ctx->read_buf, len);
		}
	}

	return 0;
}

static int
rp_init(struct rp_ctx *ctx, int argc, const char **argv)
{
	const char *config_path;

	// os initialization should always come first
	rp_os_init();

	if (rp_parse_opts(argc, argv, &config_path)) {
		return 1;
	}

	ctx->pool = rp_create_pool(RP_DEFAULT_POOL_SIZE);
	if (!ctx->pool) {
		return -1;
	}
	memset(&ctx->cfg, 0, sizeof(ctx->cfg));

	if (rp_config_load(ctx->pool, config_path, &ctx->cfg)) {
		return 1;
	}

	ctx->read_buf = rp_palloc(ctx->pool, sizeof(*ctx->read_buf) + IRC_BUFFER_SZ);
	if (!ctx->read_buf) {
		return -1;
	}

	ctx->write_buf = rp_palloc(ctx->pool, sizeof(*ctx->write_buf) + IRC_BUFFER_SZ);
	if (!ctx->write_buf) {
		return -1;
	}

	ctx->read_buf->capacity = IRC_BUFFER_SZ;
	ctx->write_buf->capacity = IRC_BUFFER_SZ;

	rp_fifo_init(ctx->read_buf);
	rp_fifo_init(ctx->write_buf);

	return 0;
}

int
main(int argc, const char **argv)
{
	int            ret;
	struct rp_ctx  ctx;

	if ((ret = rp_init(&ctx, argc, argv))) {
		if (ret < 0) {
			exit(ret);
		}

		exit(0);
	}

	main_loop(&ctx);

	rp_destroy_pool(ctx.pool);

	return 0;
}

