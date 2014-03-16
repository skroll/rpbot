#ifndef OS_H
#define OS_H

#include <stdint.h>

extern uintptr_t rp_pagesize;
extern uintptr_t rp_pagesize_shift;

int rp_os_init(void);

#endif // OS_H

