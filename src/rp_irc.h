#ifndef RP_IRC_H
#define RP_IRC_H

#include <stdlib.h>
#include <rp_config.h>
#include <rp_palloc.h>
#include <rp_os.h>
#include <rp_fifo.h>

struct rp_irc_ctx;

void rp_irc_init(rp_pool_t *pool, struct rp_config *cfg,
	rp_fifo_t *write_buf, struct rp_irc_ctx **ctx);

int rp_irc_parse(struct rp_irc_ctx *ctx, const char *src, size_t *len);
int rp_irc_handle(struct rp_irc_ctx *ctx);
int rp_irc_onconnect(struct rp_irc_ctx *ctx);

#endif // RP_IRC_H

