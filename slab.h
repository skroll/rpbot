#ifndef SLAB_H
#define SLAB_H

#include "config.h"
#include <stdint.h>
#include <stdlib.h>

struct rp_slab_page {
	uintptr_t slab;
	struct rp_slab_page *next;
	uintptr_t prev;
};

typedef struct {
	size_t min_size;
	size_t min_shift;

	struct rp_slab_page *pages;
	struct rp_slab_page  free;

	u_char *start;
	u_char *end;
} rp_slab_pool_t;

void rp_slab_init(rp_slab_pool_t *pool);
void * rp_slab_alloc(rp_slab_pool_t *pool, size_t size);
void rp_slab_free(rp_slab_pool_t *pool, void *p);

#endif // SLAB_H

