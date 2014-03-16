#ifndef RP_PALLOC_H
#define RP_PALLOC_H

#include <sys/types.h>
#include <stdint.h>
#include <rp_os.h>

#define RP_MAX_ALLOC_FROM_POOL (rp_pagesize - 1)
#define RP_DEFAULT_POOL_SIZE (16 * 1024)
#define RP_POOL_ALIGNMENT 16
#define RP_MIN_POOL_SIZE rp_align((sizeof(rp_pool_t) + 2 * sizeof(rp_pool_large_t)), RP_POOL_ALIGNMENT)

typedef struct rp_pool_large rp_pool_large_t;

struct rp_pool_large {
	rp_pool_large_t *next;
	void *alloc;
};

typedef struct {
	u_char *last;
	u_char *end;
	u_char *next;
	uintptr_t failed;
} rp_pool_data_t;

typedef struct rp_pool rp_pool_t;

struct rp_pool {
	rp_pool_data_t d;
	size_t max;
	rp_pool_t *current;
	rp_pool_large_t *large;
};

void *rp_alloc(size_t size);
void *rp_calloc(size_t size);

rp_pool_t *rp_create_pool(size_t size);
void rp_destroy_pool(rp_pool_t *pool);
void rp_reset_pool(rp_pool_t *pool);

void * rp_palloc(rp_pool_t *pool, size_t size);
void * rp_pnalloc(rp_pool_t *pool, size_t size);
void * rp_pcalloc(rp_pool_t *pool, size_t size);
void *rp_pmemalign(rp_pool_t *pool, size_t size, size_t alignment);
int rp_pfree(rp_pool_t *pool, void *p);

#endif // RP_PALLOC_H

