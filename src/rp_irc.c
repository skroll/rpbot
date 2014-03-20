#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <uthash.h>
#include <utlist.h>
#include <rp_irc.h>
#include <rp_palloc.h>
#include <rp_ircsm.h>

struct rp_irc_ctx {
	rp_pool_t              *pool;
	struct rp_config       *cfg;
	struct rp_irc_ev_hash  *hash;
	rp_fifo_t              *write_buf;
	struct rp_ircsm_msg     msg;
	int                     cs;
};

typedef void (* rp_ev_handler_t)(struct rp_irc_ctx *ctx);

struct rp_irc_ev {
	rp_ev_handler_t   handler;
	struct rp_irc_ev *next;
};

struct rp_irc_ev_hash {
	const char       *cmd;
	struct rp_irc_ev *ev;
	UT_hash_handle    hh;
};

static void
register_handler(struct rp_irc_ctx *ctx, rp_str_t *cmd,
	rp_ev_handler_t handler)
{
	struct rp_irc_ev_hash *ehash;

	HASH_FIND(hh, ctx->hash, cmd->ptr, cmd->len, ehash);

	if (!ehash) {
		ehash = (struct rp_irc_ev_hash *)rp_palloc(ctx->pool, sizeof(struct rp_irc_ev_hash));
		ehash->cmd = cmd->ptr;
		HASH_ADD_KEYPTR(hh, ctx->hash, ehash->cmd, cmd->len, ehash);
	}

	struct rp_irc_ev *e = rp_palloc(ctx->pool, sizeof(struct rp_irc_ev));
	e->handler = handler;
	e->next = NULL;

	LL_APPEND(ehash->ev, e);
}

static void
handle_ping(struct rp_irc_ctx *ctx)
{
	printf("PING?! PONG\n");

	rp_fifo_putstr(ctx->write_buf, "PONG ");
	rp_fifo_putstring(ctx->write_buf, &ctx->msg.params);
	rp_fifo_putstr(ctx->write_buf, "\r\n");
}

static void
handle_auth(struct rp_irc_ctx *ctx)
{
	printf("handling auth\n");
	rp_fifo_putstr(ctx->write_buf, "JOIN ");
	rp_fifo_putstring(ctx->write_buf, &ctx->cfg->channels->name);
	rp_fifo_putstr(ctx->write_buf, "\r\n");
}

static void
register_default_handlers(struct rp_irc_ctx *ctx)
{
	rp_str_t pingmsg = rp_string("PING");
	register_handler(ctx, &pingmsg, handle_ping);

	rp_str_t authmsg = rp_string("004");
	register_handler(ctx, &authmsg, handle_auth);
}

void
rp_irc_init(rp_pool_t *pool, struct rp_config *cfg, rp_fifo_t *write_buf,
	struct rp_irc_ctx **ctx)
{
	struct rp_irc_ctx *c = rp_palloc(pool, sizeof(*c));
	memset(c, 0, sizeof(*c));

	rp_ircsm_init(&c->cs);
	rp_ircsm_msg_init(pool, &c->msg);

	c->pool = pool;
	c->cfg = cfg;
	c->write_buf = write_buf;

	register_default_handlers(c);

	*ctx = c;
}

int
rp_irc_handle(struct rp_irc_ctx *ctx)
{
	struct rp_irc_ev_hash *ehash;
	HASH_FIND(hh, ctx->hash, ctx->msg.code.ptr, ctx->msg.code.len, ehash);

	if (!ehash) {
		return 0;
	}

	struct rp_irc_ev *e;

	LL_FOREACH(ehash->ev, e) {
		e->handler(ctx);
	}

	return 0;
}

int
rp_irc_parse(struct rp_irc_ctx *ctx, const char *src, size_t *len)
{
	return rp_ircsm_parse(&ctx->msg, &ctx->cs, src, len);
}

int
rp_irc_onconnect(struct rp_irc_ctx *ctx)
{
	rp_fifo_putstr(ctx->write_buf, "NICK ");
	rp_fifo_putstring(ctx->write_buf, &ctx->cfg->identity.nicks->str);
	rp_fifo_putstr(ctx->write_buf, "\r\nUSER ");
	rp_fifo_putstring(ctx->write_buf, &ctx->cfg->identity.login);
	rp_fifo_putstr(ctx->write_buf, " 8 * :");
	rp_fifo_putstring(ctx->write_buf, &ctx->cfg->identity.name);
	rp_fifo_putstr(ctx->write_buf, "\r\n");

	return 0;
}

