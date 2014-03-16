#ifndef RP_IRC_H
#define RP_IRC_H

#include <stdlib.h>

struct irc_str {
	int len;
	char buf[1];
};

#define IRC_STR(_name, _len) \
	struct { \
		int len; \
		char buf[_len]; \
	} _name;

struct rp_irc_msg {
	IRC_STR(code,   16)
	IRC_STR(params, 2048)
};

struct rp_irc_ctx {
	int               cs;
	struct rp_irc_msg msg;
};

void rp_irc_init(struct rp_irc_ctx *);

int rp_irc_parse(struct rp_irc_ctx *ctx, const char *src, size_t *len);

#endif // RP_IRC_H

