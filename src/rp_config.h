#ifndef RP_CONFIG_H
#define RP_CONFIG_H

#include <rp_string.h>
#include <rp_palloc.h>

struct rp_config_server {
	rp_str_t                 host;
	rp_str_t                 port;
	struct rp_config_server *next;
};

struct rp_config_channel {
	rp_str_t                  name;
	rp_str_t                  key;
	struct rp_config_channel *next;
};

struct rp_config {
	struct {
		rp_str_list_t *nicks;
		rp_str_t       name;
		rp_str_t       login;
	} identity;

	struct rp_config_server *servers;
	struct rp_config_channel *channels;
};

int rp_config_load(rp_pool_t *pool, const char *path, struct rp_config *cfg);

#endif // RP_CONFIG_H

