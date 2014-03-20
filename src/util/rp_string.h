#ifndef RP_STRING_H
#define RP_STRING_H

#include <stdlib.h>

typedef struct {
	size_t  len;
	char   *ptr;
} rp_str_t;

typedef struct rp_str_list {
	rp_str_t            str;
	struct rp_str_list *next;
} rp_str_list_t;

#define rp_string(str) { sizeof(str) - 1, str }

#endif // RP_STRING_H

