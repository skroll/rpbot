#ifndef FIFO_H
#define FIFO_H

#include "config.h"
#include <stdlib.h>
#include "rp_math.h"

struct fifo_buffer {
	size_t  count; // number of bytes in the buffer
	char   *head;
	char   *tail;
	char   *end;
	char    buffer[IRC_BUFFER_SZ];
};

typedef struct fifo_buffer  fifo_buffer_t;

// initialize a buffer
void fifo_init(fifo_buffer_t *buf);

// number of bytes free in the buffer
#define fifo_bytes_free(_buf) (IRC_BUFFER_SZ - (_buf).count)

// number of bytes in the buffer
#define fifo_count(_buf) ((_buf).count)

// consume n bytes, used in conjunction with fifo_raw_r for direct
// writing to socket buffers
inline void
fifo_consume(fifo_buffer_t *buf, size_t n)
{
	buf->head += n;
	buf->count -= n;

	if (buf->head == buf->end) {
		buf->head = &buf->buffer[0];
	}
}

// reserve n bytes, used in conjunction with fifo_raw_w for direct
// writing from socket buffers
inline void
fifo_reserve(fifo_buffer_t *buf, size_t n)
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
inline size_t
fifo_raw_r(fifo_buffer_t *buf, void **p)
{
	*p = buf->head;
	return rp_min(fifo_count(*buf), (size_t)(buf->end - buf->head));
}

// get a pointer to the internal buffer, returns the number of bytes
// that can be written in a single contiguous write. use fifo_reserve
// afterwards to indicate the number of bytes written.
inline size_t
fifo_raw_w(fifo_buffer_t *buf, void **p)
{
	*p = buf->tail;
	return rp_min(fifo_bytes_free(*buf), (size_t)(buf->end - buf->tail));
}

// put n bytes from src into the buffer. returns number of bytes
// actually written.
size_t fifo_put(fifo_buffer_t *buf, void *src, size_t n);

// retrieve at most n bytes from the buffer. returns number of bytes
// placed in dest.
size_t fifo_get(fifo_buffer_t *buf, void *dest, size_t n);

#endif // FIFO_H

