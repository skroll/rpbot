#include <string.h>
#include <stdlib.h>
#include <rp_fifo.h>

void
rp_fifo_init(rp_fifo_t *buf)
{
	buf->count = 0;
	buf->head = &buf->buffer[0];
	buf->tail = &buf->buffer[0];
	buf->end = &buf->buffer[0] + buf->capacity;
}

size_t
rp_fifo_put(rp_fifo_t *buf, void *src, size_t n)
{
	char *p = src;

	if ((n = rp_min(n, rp_fifo_bytes_free(buf))) == 0) {
		return 0;
	}

	size_t ret = n;

	while (n > 0) {
		size_t s = rp_min(n, (size_t)(buf->end - buf->tail));
		memcpy(buf->tail, p, s);

		p += s;
		n -= s;

		rp_fifo_reserve(buf, s);
	}

	return ret;
}

size_t
rp_fifo_get(rp_fifo_t *buf, void *dest, size_t n)
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

		rp_fifo_consume(buf, s);
	}

	return ret;
}

