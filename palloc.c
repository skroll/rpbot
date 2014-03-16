#include "config.h"
#include "palloc.h"
#include <string.h>

static void * rp_palloc_block(rp_pool_t *pool, size_t size);
static void * rp_palloc_large(rp_pool_t *pool, size_t size);

rp_pool_t *
rp_create_pool(size_t size)
{
	rp_pool_t *p;

	p = rp_memalign(RP_POOL_ALIGNMENT, size);
	if (p == NULL) {
		return NULL;
	}

	p->d.last = (u_char *)p + sizeof(rp_pool_t);
	p->d.end = (u_char *)p + size;
	p->d.next = NULL;
	p->d.failed = 0;

	size = size - sizeof(rp_pool_t);
	p->max = (size < RP_MAX_ALLOC_FROM_POOL) ? size : RP_MAX_ALLOC_FROM_POOL;

	p->current = p;
	p->large = NULL;

	return p;
}

void
rp_destroy_pool(rp_pool_t *pool)
{
	rp_pool_t *p, *n;
	rp_pool_large_t *l;

	for (l = pool->large; l; l = l->next) {
		if (l->alloc) {
			rp_free(l->alloc);
		}
	}

	for (p = pool, n = (rp_pool_t *)pool->d.next; /* void */; p = n, n = (rp_pool_t *)n->d.next) {
		rp_free(p);

		if (n == NULL) {
			break;
		}
	}
}

void
rp_reset_pool(rp_pool_t *pool)
{
	rp_pool_t *p;
	rp_pool_large_t *l;

	for (l = pool->large; l; l = l->next) {
		if (l->alloc) {
			rp_free(l->alloc);
		}
	}

	for (p = pool; p; p = (rp_pool_t *)p->d.next) {
		p->d.last = (u_char *)p + sizeof(rp_pool_t);
		p->d.failed = 0;
	}

	pool->current = pool;
	pool->large = NULL;
}

void *
rp_palloc(rp_pool_t *pool, size_t size)
{
	u_char *m;
	rp_pool_t *p;

	if (size <= pool->max) {
		p = pool->current;

		do {
			m = rp_align_ptr(p->d.last, RP_ALIGNMENT);

			if ((size_t)(p->d.end - m) >= size) {
				p->d.last = m + size;
				return m;
			}

			p = (rp_pool_t *)p->d.next;
		} while (p);

		return rp_palloc_block(pool, size);
	}

	return rp_palloc_large(pool, size);
}

void *
rp_pnalloc(rp_pool_t *pool, size_t size)
{
	u_char *m;
	rp_pool_t *p;

	if (size <= pool->max) {
		p = pool->current;

		do {
			m = p->d.last;

			if ((size_t)(p->d.end - m) >= size) {
				p->d.last = m + size;
				return m;
			}

			p = (rp_pool_t *)p->d.next;
		} while (p);

		return rp_palloc_block(pool, size);
	}

	return rp_palloc_large(pool, size);
}

static void *
rp_palloc_block(rp_pool_t *pool, size_t size)
{
	u_char *m;
	size_t psize;
	rp_pool_t *p, *new, *current;

	psize = (size_t)(pool->d.end - (u_char *)pool);

	m = rp_memalign(RP_POOL_ALIGNMENT, psize);
	if (m == NULL) {
		return NULL;
	}

	new = (rp_pool_t *)m;

	new->d.end = m + psize;
	new->d.next = NULL;
	new->d.failed = 0;

	m += sizeof(rp_pool_data_t);
	m = rp_align_ptr(m, RP_ALIGNMENT);
	new->d.last = m + size;

	current = pool->current;

	for (p = current; p->d.next; p = (rp_pool_t *)p->d.next) {
		if (p->d.failed++ > 4) {
			current = (rp_pool_t *)p->d.next;
		}
	}

	p->d.next = (u_char *)new;

	pool->current = current ? current : new;

	return m;
}

static void *
rp_palloc_large(rp_pool_t *pool, size_t size)
{
	void *p;
	uintptr_t n;
	rp_pool_large_t *large;

	p = rp_alloc(size);
	if (p == NULL) {
		return NULL;
	}

	n = 0;

	for (large = pool->large; large; large = large->next) {
		if (large->alloc == NULL) {
			large->alloc = p;
			return p;
		}

		if (n++ > 3) {
			break;
		}
	}

	large = rp_palloc(pool, sizeof(rp_pool_large_t));
	if (large == NULL) {
		rp_free(p);
		return NULL;
	}

	large->alloc = p;
	large->next = pool->large;
	pool->large = large;

	return p;
}

void *
rp_pmemalign(rp_pool_t *pool, size_t size, size_t alignment)
{
	void *p;
	rp_pool_large_t *large;

	p = rp_memalign(alignment, size);
	if (p == NULL) {
		return NULL;
	}

	large = rp_palloc(pool, sizeof(rp_pool_large_t));
	if (large == NULL) {
		rp_free(p);
		return NULL;
	}

	large->alloc = p;
	large->next = pool->large;
	pool->large = large;

	return p;
}

int
rp_pfree(rp_pool_t *pool, void *p)
{
	rp_pool_large_t *l;

	for (l = pool->large; l; l = l->next) {
		if (p == l->alloc) {
			rp_free(l->alloc);
			l->alloc = NULL;

			return 0;
		}
	}

	return -1;
}

void *
rp_pcalloc(rp_pool_t *pool, size_t size)
{
	void *p;

	p = rp_palloc(pool, size);
	if (p) {
		memset(p, 0, size);
	}

	return p;
}

void *
rp_alloc(size_t size)
{
	void *p;

	p = malloc(size);

	return p;
}

void *
rp_calloc(size_t size)
{
	void *p;

	p = rp_alloc(size);

	if (p) {
		memset(p, 0, size);
	}

	return p;
}

