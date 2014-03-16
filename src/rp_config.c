#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <yajl/yajl_parse.h>
#include <rp_config.h>

struct rp_json_ctx {
	rp_pool_t        *pool;
	rp_pool_t        *tmp_pool;
	struct rp_config *cfg;

	struct rp_config_server *server;
	struct rp_config_channel *channel;

	enum {
		START = 0,
		ROOT,
		ROOT_CONFIG,
		ROOT_CONFIG_IDENTITY,
		ROOT_CONFIG_IDENTITY_NICKS,
		ROOT_CONFIG_IDENTITY_NICKS_ITEMS,
		ROOT_CONFIG_IDENTITY_NAME,
		ROOT_CONFIG_IDENTITY_LOGIN,
		ROOT_CONFIG_SERVERS,
		ROOT_CONFIG_SERVERS_ITEMS,
		ROOT_CONFIG_SERVERS_ITEMS_HOST,
		ROOT_CONFIG_SERVERS_ITEMS_PORT,
		ROOT_CONFIG_CHANNELS,
		ROOT_CONFIG_CHANNELS_ITEMS,
	} state;
};

static void
rpcfg_mkstr(rp_pool_t *pool, rp_str_t *str, const char *s, size_t len)
{
	str->ptr = rp_palloc(pool, len);
	str->len = len;
	memcpy(str->ptr, s, len);
}

static int
rpcfg_string(void *data, const unsigned char *s, size_t len)
{
	struct rp_json_ctx *ctx = (struct rp_json_ctx *)data;

	switch (ctx->state) {
	case ROOT_CONFIG_IDENTITY_NICKS_ITEMS:
	{
		rp_str_list_t *l = rp_palloc(ctx->pool, sizeof(*l));
		rpcfg_mkstr(ctx->pool, &l->str, (const char *)s, len);
		LL_APPEND(ctx->cfg->identity.nicks, l);
		return 1;
	}
	case ROOT_CONFIG_IDENTITY_NAME:
		rpcfg_mkstr(ctx->pool, &ctx->cfg->identity.name, (const char *)s, len);
		ctx->state = ROOT_CONFIG_IDENTITY;
		return 1;
	case ROOT_CONFIG_IDENTITY_LOGIN:
		rpcfg_mkstr(ctx->pool, &ctx->cfg->identity.login, (const char *)s, len);
		ctx->state = ROOT_CONFIG_IDENTITY;
		return 1;
		break;
	case ROOT_CONFIG_SERVERS_ITEMS_HOST:
		rpcfg_mkstr(ctx->pool, &ctx->server->host, (const char *)s, len);
		ctx->state = ROOT_CONFIG_SERVERS_ITEMS;
		return 1;
	case ROOT_CONFIG_SERVERS_ITEMS_PORT:
		rpcfg_mkstr(ctx->pool, &ctx->server->port, (const char *)s, len);
		ctx->state = ROOT_CONFIG_SERVERS_ITEMS;
		return 1;
	case ROOT_CONFIG_CHANNELS_ITEMS:
		ctx->channel = rp_pcalloc(ctx->pool, sizeof(*ctx->channel));
		ctx->channel->key.len = 0;
		rpcfg_mkstr(ctx->pool, &ctx->channel->name, (const char *)s, len);
		LL_APPEND(ctx->cfg->channels, ctx->channel);
		return 1;
	default:
		return 0;
	}

	return 1;
}

static int
rpcfg_start_map(void *data)
{
	struct rp_json_ctx *ctx = (struct rp_json_ctx *)data;

	switch (ctx->state) {
	case START:
		ctx->state = ROOT;
		break;
	case ROOT_CONFIG:
	case ROOT_CONFIG_IDENTITY:
		return 1;
	case ROOT_CONFIG_SERVERS_ITEMS:
		ctx->server = rp_pcalloc(ctx->pool, sizeof(*ctx->server));
		return 1;
	default:
		return 0;
		break;
	}

	return 1;
}

static int
rpcfg_end_map(void *data)
{
	struct rp_json_ctx *ctx = (struct rp_json_ctx *)data;

	switch (ctx->state) {
	case ROOT_CONFIG:
		ctx->state = ROOT;
		return 1;
	case ROOT_CONFIG_IDENTITY:
		ctx->state = ROOT_CONFIG;
		return 1;
	case ROOT_CONFIG_SERVERS_ITEMS:
		LL_APPEND(ctx->cfg->servers, ctx->server);
		return 1;
	case ROOT:
		ctx->state = START;
		return 1;
	default:
		return 0;
	}

	return 1;
}

static int
rpcfg_map_key(void *data, const unsigned char *s, size_t len)
{
	struct rp_json_ctx *ctx = (struct rp_json_ctx *)data;

	switch (ctx->state) {
	case ROOT:
		if (strncmp((const char *)s, "config", len) == 0) {
			ctx->state = ROOT_CONFIG;
			return 1;
		} else {
			return 0;
		}
		break;
	case ROOT_CONFIG:
		if (strncmp((const char *)s, "identity", len) == 0) {
			ctx->state = ROOT_CONFIG_IDENTITY;
			return 1;
		} else if (strncmp((const char *)s, "servers", len) == 0) {
			ctx->state = ROOT_CONFIG_SERVERS;
			return 1;
		} else if (strncmp((const char *)s, "channels", len) == 0) {
			ctx->state = ROOT_CONFIG_CHANNELS;
			return 1;
		} else {
			return 0;
		}

		break;
	case ROOT_CONFIG_IDENTITY:
		if (strncmp((const char *)s, "nicks", len) == 0) {
			ctx->state = ROOT_CONFIG_IDENTITY_NICKS;
			return 1;
		} else if (strncmp((const char *)s, "name", len) == 0) {
			ctx->state = ROOT_CONFIG_IDENTITY_NAME;
			return 1;
		} else if (strncmp((const char *)s, "login", len) == 0) {
			ctx->state = ROOT_CONFIG_IDENTITY_LOGIN;
			return 1;
		}
		break;
	case ROOT_CONFIG_SERVERS_ITEMS:
		if (strncmp((const char *)s, "host", len) == 0) {
			ctx->state = ROOT_CONFIG_SERVERS_ITEMS_HOST;
			return 1;
		} else if (strncmp((const char *)s, "port", len) == 0) {
			ctx->state = ROOT_CONFIG_SERVERS_ITEMS_PORT;
			return 1;
		} else {
			return 0;
		}
	default:
		return 0;
	}

	(void)s;
	(void)len;

	return 1;
}

static int
rpcfg_start_array(void *data)
{
	struct rp_json_ctx *ctx = (struct rp_json_ctx *)data;

	switch (ctx->state) {
	case ROOT_CONFIG_IDENTITY_NICKS:
		ctx->state = ROOT_CONFIG_IDENTITY_NICKS_ITEMS;
		return 1;
	case ROOT_CONFIG_SERVERS:
		ctx->state = ROOT_CONFIG_SERVERS_ITEMS;
		return 1;
	case ROOT_CONFIG_CHANNELS:
		ctx->state = ROOT_CONFIG_CHANNELS_ITEMS;
		return 1;
	default:
		return 0;
	}
}

static int
rpcfg_end_array(void *data)
{
	struct rp_json_ctx *ctx = (struct rp_json_ctx *)data;

	switch (ctx->state) {
	case ROOT_CONFIG_IDENTITY_NICKS_ITEMS:
		ctx->state = ROOT_CONFIG_IDENTITY;
		return 1;
	case ROOT_CONFIG_SERVERS_ITEMS:
		ctx->state = ROOT_CONFIG;
		return 1;
	case ROOT_CONFIG_CHANNELS_ITEMS:
		ctx->state = ROOT_CONFIG;
		return 1;
	default:
		return 0;
	}

	return 1;
}

static yajl_callbacks callbacks = {
	NULL,              // null
	NULL,              // bool
	NULL,
	NULL,
	NULL,              // number
	rpcfg_string,      // string
	rpcfg_start_map,   // start map
	rpcfg_map_key,     // map key
	rpcfg_end_map,     // end map
	rpcfg_start_array, // start array
	rpcfg_end_array    // end array
};

static void *
rpcfg_alloc(void *data, size_t sz)
{
	struct rp_json_ctx *ctx = (struct rp_json_ctx *)data;

	return rp_palloc(ctx->tmp_pool, sz);
}

static void
rpcfg_free(void *data, void *ptr)
{
	(void)data;
	(void)ptr;
}

static void *
rpcfg_realloc(void *data, void *ptr, size_t sz)
{
	struct rp_json_ctx *ctx = (struct rp_json_ctx *)data;

	void *p = rp_palloc(ctx->tmp_pool, sz);

	if (ptr != NULL) {
		memcpy(p, ptr, sz);
	}

	return p;
}

#define READ_BUF_SZ 2048

int rp_config_load(rp_pool_t *pool, const char *path, struct rp_config *cfg)
{
	struct rp_json_ctx  ctx;
	rp_pool_t          *tmp_pool;

	tmp_pool = rp_create_pool(RP_DEFAULT_POOL_SIZE);

	ctx.pool = pool;
	ctx.tmp_pool = tmp_pool;
	ctx.cfg = cfg;
	ctx.state = START;
	ctx.server = NULL;

	yajl_alloc_funcs alloc_funcs = {
		rpcfg_alloc,
		rpcfg_realloc,
		rpcfg_free,
		&ctx,
	};

	yajl_handle hand = yajl_alloc(&callbacks, &alloc_funcs, &ctx);
	yajl_config(hand, yajl_allow_comments, 1);

	FILE *f = fopen(path, "r");

	if (!f) {
		fprintf(stderr, "%s not found\n", path);
		return -1;
	}

	unsigned char *buf = rp_palloc(tmp_pool, READ_BUF_SZ);

	for (;;) {
		size_t rd = fread((void *)buf, 1, READ_BUF_SZ, f);

		if (rd == 0) {
			if (!feof(f)) {
				fprintf(stderr, "error on file read\n");
			}
			break;
		}

		int stat = yajl_parse(hand, buf, rd);

		if (stat != yajl_status_ok) {
			unsigned char *str = yajl_get_error(hand, 1, buf, rd);
			fprintf(stderr, "%s", (const char *)str);
			yajl_free_error(hand, str);

			return -1;
		}
	}

	fclose(f);

	yajl_free(hand);
	rp_destroy_pool(ctx.tmp_pool);

	// TODO: Verify the configuration is valid

	return 0;
}

