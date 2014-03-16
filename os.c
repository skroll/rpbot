#include "config.h"
#include "os.h"
#include <unistd.h>

uintptr_t rp_pagesize;
uintptr_t rp_pagesize_shift;

int rp_os_init(void)
{
	uintptr_t n;

	rp_pagesize = getpagesize();

	for (n = rp_pagesize; n >>= 1; rp_pagesize_shift++) { /* void */ }

	return 0;
}

