#include "config.h"
#include "slab.h"
#include "os.h"
#include <string.h>
#include <stdio.h>

#define RP_SLAB_PAGE_MASK 3
#define RP_SLAB_PAGE      0
#define RP_SLAB_BIG       1
#define RP_SLAB_EXACT     2
#define RP_SLAB_SMALL     3

#if (RPBOT_PTR_SIZE == 4)

#define RP_SLAB_PAGE_FREE  0
#define RP_SLAB_PAGE_BUSY  0xffffffff
#define RP_SLAB_PAGE_START 0x80000000

#define RP_SLAB_SHIFT_MASK 0x0000000f
#define RP_SLAB_MAP_MASK   0xffff0000
#define RP_SLAB_MAP_SHIFT  16

#define RP_SLAB_BUSY       0xffffffff

#else // (RPBOT_PTR_SIZE == 8)

#define RP_SLAB_PAGE_FREE  0
#define RP_SLAB_PAGE_BUSY  0xffffffffffffffff
#define RP_SLAB_PAGE_START 0x8000000000000000

#define RP_SLAB_SHIFT_MASK 0x000000000000000f
#define RP_SLAB_MAP_MASK   0xffffffff00000000
#define RP_SLAB_MAP_SHIFT  32

#define RP_SLAB_BUSY       0xffffffffffffffff

#endif // (RPBOT_PTR_SIZE == 8)

static struct rp_slab_page * rp_slab_alloc_pages(rp_slab_pool_t *pool,
	uintptr_t pages);

static void rp_slab_free_pages(rp_slab_pool_t *pool,
	struct rp_slab_page *page, uintptr_t pages);

static uint64_t rp_slab_max_size = 0;
static uint64_t rp_slab_exact_size;
static uint64_t rp_slab_exact_shift;

#define rp_align_ptr(p, a) \
	(u_char *)(((uintptr_t)(p) + ((uintptr_t)a - 1)) & ~((uintptr_t)a - 1))

void
rp_slab_init(rp_slab_pool_t *pool)
{
	u_char *p;
	size_t size;
	uintptr_t i, n, pages;
	intptr_t m;
	struct rp_slab_page *slots;

	if (rp_slab_max_size == 0) {
		rp_slab_max_size = rp_pagesize / 2;
		rp_slab_exact_size = rp_pagesize / (8 * sizeof(uintptr_t));
		for (n = rp_slab_exact_size; n >>= 1; rp_slab_exact_shift++) {
			// void
		}
	}

	pool->min_size = 1 << pool->min_shift;

	p = (u_char *)pool + sizeof(rp_slab_pool_t);
	size = pool->end - p;

	slots = (struct rp_slab_page *)p;
	n = rp_pagesize_shift - pool->min_shift;

	for (i = 0; i < n; i++) {
		slots[i].slab = 0;
		slots[i].next = &slots[i];
		slots[i].prev = 0;
	}

	p += n * sizeof(struct rp_slab_page);

	pages = (uintptr_t)(size / (rp_pagesize + sizeof(struct rp_slab_page)));
	memset(p, 0, pages * sizeof(struct rp_slab_page));

	pool->pages = (struct rp_slab_page *)p;

	pool->free.prev = 0;
	pool->free.next = (struct rp_slab_page *)p;

	pool->pages->slab = pages;
	pool->pages->next = &pool->free;
	pool->pages->prev = (uintptr_t)&pool->free;

	pool->start = (u_char *)rp_align_ptr((uintptr_t)p + pages * sizeof(struct rp_slab_page), rp_pagesize);

	m = pages - (pool->end - pool->start) / rp_pagesize;

	if (m > 0) {
		pages -= m;
		pool->pages->slab = pages;
	}
}

void *
rp_slab_alloc(rp_slab_pool_t *pool, size_t size)
{
	size_t s;
	uintptr_t p, n, m, mask, *bitmap;
	uintptr_t i, slot, shift, map;
	struct rp_slab_page *page, *prev, *slots;

	if (size >= rp_slab_max_size) {
		page = rp_slab_alloc_pages(pool, (size >> rp_pagesize_shift) + ((size % rp_pagesize) ? 1: 0));

		if (page) {
			p = (page - pool->pages) << rp_pagesize_shift;
			p += (uintptr_t)pool->start;
		} else {
			p = 0;
		}

		goto done;
	}

	if (size > pool->min_size) {
		shift = 1;
		for (s = size - 1; s >>= 1; shift++) { /* void */ }
		slot = shift - pool->min_shift;
	} else {
		size = pool->min_size;
		shift = pool->min_shift;
		slot = 0;
	}

	slots = (struct rp_slab_page *)((u_char *)pool + sizeof(rp_slab_pool_t));
	page = slots[slot].next;

	if (page->next != page) {
		if (shift < rp_slab_exact_shift) {
			do {
				p = (page - pool->pages) << rp_pagesize_shift;
				bitmap = (uintptr_t *)(pool->start + p);

				map = (1 << (rp_pagesize_shift - shift)) / (sizeof(uintptr_t) * 8);

				for (n = 0; n < map; n++) {
					if (bitmap[n] != RP_SLAB_BUSY) {
						for (m = 1, i = 0; m; m <<= 1, i++) {
							if ((bitmap[n] & m)) {
								continue;
							}

							bitmap[n] |= m;

							i = ((n * sizeof(uintptr_t) * 8) << shift) + (i << shift);

							if (bitmap[n] == RP_SLAB_BUSY) {
								for (n = n + 1; n < map; n++) {
									if (bitmap[n] != RP_SLAB_BUSY) {
										p = (uintptr_t)bitmap +i;
										goto done;
									}
								}

								prev = (struct rp_slab_page *)(page->prev & ~RP_SLAB_PAGE_MASK);
								prev->next = page->next;
								page->next->prev = page->prev;

								page->next = NULL;
								page->prev = RP_SLAB_SMALL;
							}

							p = (uintptr_t)bitmap + i;

							goto done;
						}
					}
				}

				page = page->next;
			} while (page);
		} else if (shift == rp_slab_exact_shift) {
			do {
				if (page->slab != RP_SLAB_BUSY) {
					for (m = 1, i = 0; m; m <<= 1, i++) {
						if ((page->slab & m)) {
							continue;
						}

						page->slab |= m;

						if (page->slab == RP_SLAB_BUSY) {
							prev = (struct rp_slab_page *)(page->prev & ~RP_SLAB_PAGE_MASK);
							prev->next = page->next;
							page->next->prev = page->prev;

							page->next = NULL;
							page->prev = RP_SLAB_EXACT;
						}

						p = (page - pool->pages) << rp_pagesize_shift;
						p += i << shift;
						p += (uintptr_t)pool->start;

						goto done;
					}
				}

				page = page->next;
			} while (page);
		} else { // shift > rp_slab_exact_shift
			n = rp_pagesize_shift - (page->slab & RP_SLAB_SHIFT_MASK);
			n = 1 << n;
			n = ((uintptr_t)1 << n) - 1;
			mask = n << RP_SLAB_MAP_SHIFT;

			do {
				if ((page->slab & RP_SLAB_MAP_MASK) != mask) {
					for (m = (uintptr_t)1 << RP_SLAB_MAP_SHIFT, i = 0; m & mask; m <<= 1, i++) {
						if ((page->slab & m)) {
							continue;
						}

						page->slab |= m;

						if ((page->slab & RP_SLAB_MAP_MASK) == mask) {
							prev = (struct rp_slab_page *)(page->prev & ~RP_SLAB_PAGE_MASK);
							prev->next = page->next;
							page->next->prev = page->prev;

							page->next = NULL;
							page->prev = RP_SLAB_BIG;
						}

						p = (page - pool->pages) << rp_pagesize_shift;
						p += i << shift;
						p += (uintptr_t)pool->start;

						goto done;
					}
				}

				page = page->next;
			} while (page);
		}
	}

	page = rp_slab_alloc_pages(pool, 1);

	if (page) {
		if (shift < rp_slab_exact_shift) {
			p = (page - pool->pages) << rp_pagesize_shift;
			bitmap = (uintptr_t *)(pool->start + p);

			s = 1 << shift;
			n = (1 << (rp_pagesize_shift - shift)) / 8 / s;

			if (n == 0) {
				n = 1;
			}

			bitmap[0] = (2 << n) - 1;

			map = (1 << (rp_pagesize_shift - shift)) / (sizeof(uintptr_t) * 8);

			for (i = 1; i < map; i++) {
				bitmap[i] = 0;
			}

			page->slab = shift;
			page->next = &slots[slot];
			page->prev = (uintptr_t)&slots[slot] | RP_SLAB_SMALL;

			slots[slot].next = page;

			p = ((page - pool->pages) << rp_pagesize_shift) + s * n;
			p += (uintptr_t)pool->start;

			goto done;
		} else if (shift == rp_slab_exact_shift) {
			page->slab = 1;
			page->next = &slots[slot];
			page->prev = (uintptr_t)&slots[slot] | RP_SLAB_EXACT;

			slots[slot].next = page;

			p = (page - pool->pages) << rp_pagesize_shift;
			p += (uintptr_t)pool->start;

			goto done;
		} else { // shift > rp_slab_exact_shift
			page->slab = ((uintptr_t)1 << RP_SLAB_MAP_SHIFT) | shift;
			page->next = &slots[slot];
			page->prev = (uintptr_t)&slots[slot] | RP_SLAB_BIG;

			slots[slot].next = page;

			p = (page - pool->pages) << rp_pagesize_shift;
			p += (uintptr_t)pool->start;

			goto done;
		}
	}

	p = 0;

done:
	return (void *)p;
}

void
rp_slab_free(rp_slab_pool_t *pool, void *p)
{
	size_t size;
	uintptr_t slab, m, *bitmap;
	uintptr_t n, type, slot, shift, map;
	struct rp_slab_page *slots, *page;

	if ((u_char *)p < pool->start || (u_char *)p > pool->end) {
		fprintf(stderr, "rp_slab_free(): outside of pool\n");
		goto fail;
	}

	n = ((u_char *)p - pool->start) >> rp_pagesize_shift;
	page = &pool->pages[n];
	slab = page->slab;
	type = page->prev & RP_SLAB_PAGE_MASK;

	switch (type) {
	case RP_SLAB_SMALL:
		shift = slab & RP_SLAB_SHIFT_MASK;
		size = 1 << shift;

		if ((uintptr_t)p & (size - 1)) {
			goto wrong_chunk;
		}

		n = ((uintptr_t)p & (rp_pagesize - 1)) >> shift;
		m = (uintptr_t)1 << (n & (sizeof(uintptr_t) * 8 - 1));
		n /= (sizeof(uintptr_t) * 8);
		bitmap = (uintptr_t *)((uintptr_t)p & ~((uintptr_t)rp_pagesize -1));

		if (bitmap[n] & m) {
			if (page->next == NULL) {
				slots = (struct rp_slab_page *)((u_char *)pool + sizeof(rp_slab_pool_t));
				slot = shift - pool->min_shift;

				page->next = slots[slot].next;
				slots[slot].next = page;

				page->prev = (uintptr_t)&slots[slot] | RP_SLAB_SMALL;
				page->next->prev = (uintptr_t)page | RP_SLAB_SMALL;
			}

			bitmap[n] &= ~m;

			n = (1 << (rp_pagesize_shift - shift)) / 8 / (1 << shift);

			if (n == 0) {
				n = 1;
			}

			if (bitmap[0] & ~(((uintptr_t)1 << n) - 1)) {
				goto done;
			}

			map = (1 << (rp_pagesize_shift - shift)) / (sizeof(uintptr_t) * 8);

			for (n = 1; n < map; n++) {
				if (bitmap[n]) {
					goto done;
				}
			}

			rp_slab_free_pages(pool, page, 1);

			goto done;
		}

		goto chunk_already_free;

	case RP_SLAB_EXACT:
		m = (uintptr_t)1 << (((uintptr_t)p & (rp_pagesize -1)) >> rp_slab_exact_shift);
		size = rp_slab_exact_size;

		if ((uintptr_t)p & (size - 1)) {
			goto wrong_chunk;
		}

		if (slab & m) {
			if (slab == RP_SLAB_BUSY) {
				slots = (struct rp_slab_page *)((u_char *)pool + sizeof(rp_slab_pool_t));
				slot = rp_slab_exact_shift - pool->min_shift;

				page->next = slots[slot].next;
				slots[slot].next = page;

				page->prev = (uintptr_t)&slots[slot] | RP_SLAB_EXACT;
				page->next->prev = (uintptr_t)page | RP_SLAB_EXACT;
			}

			page->slab &= ~m;

			if (page->slab) {
				goto done;
			}

			rp_slab_free_pages(pool, page, 1);

			goto done;
		}

		goto chunk_already_free;

	case RP_SLAB_BIG:
		shift = slab & RP_SLAB_SHIFT_MASK;
		size = 1 << shift;

		if ((uintptr_t)p & (size - 1)) {
			goto wrong_chunk;
		}

		m = (uintptr_t)1 << ((((uintptr_t)p & (rp_pagesize -1)) >> shift) + RP_SLAB_MAP_SHIFT);

		if (slab & m) {
			if (page->next == NULL) {
				slots = (struct rp_slab_page *)((u_char *)pool + sizeof(rp_slab_pool_t));
				slot = shift - pool->min_shift;

				page->next = slots[slot].next;
				slots[slot].next = page;

				page->prev = (uintptr_t)&slots[slot] | RP_SLAB_BIG;
				page->next->prev = (uintptr_t)page | RP_SLAB_BIG;
			}

			page->slab &= ~m;

			if (page->slab & RP_SLAB_MAP_MASK) {
				goto done;
			}

			rp_slab_free_pages(pool, page, 1);

			goto done;
		}

		goto chunk_already_free;

	case RP_SLAB_PAGE:
		if ((uintptr_t)p & (rp_pagesize -1)) {
			goto wrong_chunk;
		}

		if (slab == RP_SLAB_PAGE_FREE) {
			fprintf(stderr, "rp_slab_free(): page is already free\n");
			goto fail;
		}

		if (slab == RP_SLAB_PAGE_BUSY) {
			fprintf(stderr, "rp_slab_free(): pointer to wrong page\n");
			goto fail;
		}

		n = ((u_char *)p - pool->start) >> rp_pagesize_shift;
		size = slab & ~RP_SLAB_PAGE_START;

		rp_slab_free_pages(pool, &pool->pages[n], size);

		return;
	}


	// not reached
	return;

done:
	return;

wrong_chunk:
	fprintf(stderr, "rp_flab_free(): pointer to wrong chunk\n");
	goto fail;

chunk_already_free:
	fprintf(stderr, "rp_flab_free(): chunk already free\n");

fail:
	return;
}

static struct rp_slab_page *
rp_slab_alloc_pages(rp_slab_pool_t *pool, uintptr_t pages)
{
	struct rp_slab_page *page, *p;

	for (page = pool->free.next; page != &pool->free; page = page->next) {
		if (page->slab >= pages) {
			if (page->slab > pages) {
				page[pages].slab = page->slab - pages;
				page[pages].next = page->next;
				page[pages].prev = page->prev;

				p = (struct rp_slab_page *)page->prev;
				p->next = &page[pages];
				page->next->prev = (uintptr_t)&page[pages];
			} else {
				p = (struct rp_slab_page *)page->prev;
				p->next = page->next;
				page->next->prev = page->prev;
			}

			page->slab = pages | RP_SLAB_PAGE_START;
			page->next = NULL;
			page->prev = RP_SLAB_PAGE;

			if (--pages == 0) {
				return page;
			}

			for (p = page + 1; pages; pages--) {
				p->slab = RP_SLAB_PAGE_BUSY;
				p->next = NULL;
				p->prev = RP_SLAB_PAGE;
				p++;
			}

			return page;
		}
	}

	return NULL;
}

static void
rp_slab_free_pages(rp_slab_pool_t *pool,
	struct rp_slab_page *page, uintptr_t pages)
{
	struct rp_slab_page *prev;

	page->slab = pages--;

	if (pages) {
		memset(&page[1], 0, pages * sizeof(struct rp_slab_page));
	}

	if (page->next) {
		prev = (struct rp_slab_page *)(page->prev & ~RP_SLAB_PAGE_MASK);
		prev->next = page->next;
		prev->next->prev = page->prev;
	}

	page->prev = (uintptr_t)&pool->free;
	page->next = pool->free.next;

	page->next->prev = (uintptr_t)page;

	pool->free.next = page;
}

