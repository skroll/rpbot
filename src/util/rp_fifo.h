#ifndef RP_FIFO_H
#define RP_FIFO_H

#include <stdlib.h>
#include <string.h>
#include <rp_math.h>
#include <rp_os.h>

struct rp_fifo {
	size_t    capacity;
	size_t    count; // number of bytes in the buffer
	u_char   *head;
	u_char   *tail;
	u_char   *end;
	u_char    buffer[];
};

typedef struct rp_fifo  rp_fifo_t;

// initialize a buffer
void rp_fifo_init(rp_fifo_t *buf);

// number of bytes free in the buffer
#define rp_fifo_bytes_free(_buf) ((_buf)->capacity - (_buf)->count)

// number of bytes in the buffer
#define rp_fifo_count(_buf) ((_buf)->count)

// consume n bytes, used in conjunction with fifo_raw_r for direct
// writing to socket buffers
static inline void
rp_fifo_consume(rp_fifo_t *buf, size_t n)
{
	buf->head += n;
	buf->count -= n;

	if (buf->head == buf->end) {
		buf->head = &buf->buffer[0];
	}
}

// reserve n bytes, used in conjunction with fifo_raw_w for direct
// writing from socket buffers
static inline void
rp_fifo_reserve(rp_fifo_t *buf, size_t n)
{
	buf->tail += n;
	buf->count += n;

	if (buf->tail == buf->end) {
		buf->tail = &buf->buffer[0];
	}
}

// get a pointer to the internal buffer, returns the number of bytes
// that can be read in a single contiguous read. use fifo_consume
// afterwards to indicate the number of bytes read.
static inline size_t
rp_fifo_raw_r(rp_fifo_t *buf, void **p)
{
	*p = buf->head;
	return rp_min(rp_fifo_count(buf), (size_t)(buf->end - buf->head));
}

// get a pointer to the internal buffer, returns the number of bytes
// that can be written in a single contiguous write. use fifo_reserve
// afterwards to indicate the number of bytes written.
static inline size_t
rp_fifo_raw_w(rp_fifo_t *buf, void **p)
{
	*p = buf->tail;
	return rp_min(rp_fifo_bytes_free(buf), (size_t)(buf->end - buf->tail));
}

// put n bytes from src into the buffer. returns number of bytes
// actually written.
size_t rp_fifo_put(rp_fifo_t *buf, void *src, size_t n);

// retrieve at most n bytes from the buffer. returns number of bytes
// placed in dest.
size_t rp_fifo_get(rp_fifo_t *buf, void *dest, size_t n);

static inline size_t
rp_fifo_putstr(rp_fifo_t *buf, const char *str)
{
	return rp_fifo_put(buf, (void *)str, strlen(str));
}

static inline size_t
rp_fifo_putstring(rp_fifo_t *buf, rp_str_t *str)
{
	return rp_fifo_put(buf, (void *)str->ptr, str->len);
}

#endif // RP_FIFO_H

