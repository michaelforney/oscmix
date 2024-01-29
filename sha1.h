/*
 * Copyright (c) 2016 Thomas Pornin <pornin@bolet.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef SHA1_H__
#define SHA1_H__

#include <stddef.h>
#include <stdint.h>

/**
 * \brief SHA-1 output size (in bytes).
 */
#define sha1_SIZE   20

/**
 * \brief SHA-1 context.
 */
typedef struct {
	unsigned char buf[64];
	uint64_t count;
	uint32_t val[5];
} sha1_context;

/**
 * \brief SHA-1 context initialisation.
 *
 * This function initialises or resets a context for a new SHA-1
 * computation. It also sets the vtable pointer.
 *
 * \param ctx   pointer to the context structure.
 */
void sha1_init(sha1_context *ctx);

/**
 * \brief Inject some data bytes in a running SHA-1 computation.
 *
 * The provided context is updated with some data bytes. If the number
 * of bytes (`len`) is zero, then the data pointer (`data`) is ignored
 * and may be `NULL`, and this function does nothing.
 *
 * \param ctx    pointer to the context structure.
 * \param data   pointer to the injected data.
 * \param len    injected data length (in bytes).
 */
void sha1_update(sha1_context *ctx, const void *data, size_t len);

/**
 * \brief Compute SHA-1 output.
 *
 * The SHA-1 output for the concatenation of all bytes injected in the
 * provided context since the last initialisation or reset call, is
 * computed and written in the buffer pointed to by `out`. The context
 * itself is not modified, so extra bytes may be injected afterwards
 * to continue that computation.
 *
 * \param ctx   pointer to the context structure.
 * \param out   destination buffer for the hash output.
 */
void sha1_out(const sha1_context *ctx, void *out);

#endif
