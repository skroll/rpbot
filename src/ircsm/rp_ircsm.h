#ifndef RP_IRC_SM_H
#define RP_IRC_SM_H

#include <stdlib.h>
#include <rp_string.h>

struct rp_irc_msg {
	rp_str_t  code;
	rp_str_t  params;
};

int rp_irc_sm_init(int *state);

int rp_irc_sm_parse(struct rp_irc_msg *msg, int *state, const char *src,
	size_t *len);

#endif // RP_IRC_SM_H

