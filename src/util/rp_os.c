#include <unistd.h>
#include <rp_os.h>

uintptr_t rp_pagesize;
uintptr_t rp_pagesize_shift;

int
rp_os_init(void)
{
	uintptr_t n;

	rp_pagesize = getpagesize();

	for (n = rp_pagesize; n >>= 1; rp_pagesize_shift++) { /* void */ }

	return 0;
}

void *
rp_memalign(size_t alignment, size_t size)
{
	void *p;
	int err;

	err = posix_memalign(&p, alignment, size);

	if (err) {
		p = NULL;
	}

	return p;
}

