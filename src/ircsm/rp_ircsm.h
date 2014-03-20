#ifndef RP_IRC_SM_H
#define RP_IRC_SM_H

#include <stdlib.h>
#include <rp_string.h>
#include <rp_palloc.h>

struct rp_ircsm_msg {
	// the prefix is a shared buffer between the hostmask and the
	// servername, the strings actually just point to locations
	// inside of the prefix buffer.
	rp_str_t  prefix;

	rp_str_t  servername;
	struct {
		rp_str_t nick;
		rp_str_t user;
		rp_str_t host;
	} hostmask;

	rp_str_t  code;
	rp_str_t  params;

	unsigned int is_hostmask:1;
	unsigned int is_servername:1;
};

int rp_ircsm_init(int *state);

int rp_ircsm_parse(struct rp_ircsm_msg *msg, int *state, const char *src,
	size_t *len);

int rp_ircsm_msg_init(rp_pool_t *pool, struct rp_ircsm_msg *msg);

#endif // RP_IRC_SM_H

