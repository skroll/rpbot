#ifndef RP_OS_H
#define RP_OS_H

#include <stdint.h>
#include <stdlib.h>
#include <utlist.h>

extern uintptr_t rp_pagesize;
extern uintptr_t rp_pagesize_shift;

int rp_os_init(void);

#define rp_align(d, a) (((d) + (a - 1)) & ~(a - 1))
#define rp_align_ptr(p, a) \
	(u_char *)(((uintptr_t)(p) + ((uintptr_t)a - 1)) & ~((uintptr_t)a - 1))

#define rp_free free

void * rp_memalign(size_t alignment, size_t size);

#define RP_ALIGNMENT sizeof(unsigned long)

#endif // RP_OS_H

