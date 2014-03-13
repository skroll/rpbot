#include "config.h"

#include <string.h>
#include <stdlib.h>

#include "fifo.h"

void
fifo_init(fifo_buffer_t *buf)
{
	buf->count = 0;
	buf->head = &buf->buffer[0];
	buf->tail = &buf->buffer[0];
	buf->end = &buf->buffer[IRC_BUFFER_SZ];
}

size_t
fifo_put(fifo_buffer_t *buf, void *src, size_t n)
{
	char *p = src;

	if ((n = rp_min(n, fifo_bytes_free(*buf))) == 0) {
		return 0;
	}

	size_t ret = n;

	while (n > 0) {
		size_t s = rp_min(n, (size_t)(buf->end - buf->tail));
		memcpy(buf->tail, p, s);

		p += s;
		n -= s;

		fifo_reserve(buf, s);
	}

	return ret;
}

size_t
fifo_get(fifo_buffer_t *buf, void *dest, size_t n)
{
	char *p = dest;

	if ((n = rp_min(n, buf->count)) == 0) {
		return 0;
	}

	size_t ret = n;

	while (n > 0) {
		size_t s = rp_min(n, (size_t)(buf->end - buf->head));
		memcpy(p, buf->head, s);

		p += s;
		n -= s;

		fifo_consume(buf, s);
	}

	return ret;
}

