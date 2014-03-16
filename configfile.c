#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <yajl/yajl_parse.h>

#include "configfile.h"

static int
rpcfg_string(void *ctx, const unsigned char *s, size_t len)
{
	return 1;
}

static int
rpcfg_start_map(void *ctx)
{
	return 1;
}

static int
rpcfg_end_map(void *ctx)
{
	return 1;
}

static int
rpcfg_map_key(void *ctx, const unsigned char *s, size_t len)
{
	return 1;
}

static yajl_callbacks callbacks = {
	NULL, // null
	NULL, // bool
	NULL,
	NULL,
	NULL, // number
	rpcfg_string, // string
	rpcfg_start_map, // start map
	rpcfg_map_key, // map key
	rpcfg_end_map, // end map
	NULL, // start array
	NULL // end array
};

static void *
rpcfg_alloc(void *ctx, size_t sz)
{
	printf("alloc %lu\n", sz);
	return malloc(sz);
}

static void
rpcfg_free(void *ctx, void *ptr)
{
	printf("free\n");
	free(ptr);
}

static void *
rpcfg_realloc(void *ctx, void *ptr, size_t sz)
{
	printf("realloc %lu\n", sz);
	return realloc(ptr, sz);
}

static yajl_alloc_funcs alloc_funcs = {
	rpcfg_alloc,
	rpcfg_realloc,
	rpcfg_free,
	NULL
};

int rp_load_config(const char *path)
{
	char buf[2048];
	yajl_handle hand = yajl_alloc(&callbacks, &alloc_funcs, NULL);
	yajl_config(hand, yajl_allow_comments, 1);

	FILE *f = fopen(path, "r");

	if (!f) {
		fprintf(stderr, "%s not found\n", path);
		return -1;
	}

	for (;;) {
		size_t rd = fread((void *)buf, 1, sizeof(buf) - 1, f);

		if (rd == 0) {
			if (!feof(f)) {
				fprintf(stderr, "error on file read\n");
			}
			break;
		}

		printf("read %lu\n", rd);

		int stat = yajl_parse(hand, (const unsigned char *)buf, rd);

		if (stat != yajl_status_ok) {
			unsigned char *str = yajl_get_error(hand, 1, (const unsigned char *)buf, rd);
			fprintf(stderr, "%s", (const char *)str);
			yajl_free_error(hand, str);
			break;
		}
	}

	fclose(f);

	yajl_free(hand);

	return 0;
}

