/* SPDX-License-Identifier: Unlicense */
#ifndef INTPACK_H
#define INTPACK_H

#include <stdint.h>

static inline void *
putle16(void *p, uint_least16_t v)
{
	unsigned char *b = p;

	b[0] = v & 0xff;
	b[1] = v >> 8 & 0xff;
	return b + 2;
}

static inline void *
putbe16(void *p, uint_least16_t v)
{
	unsigned char *b = p;

	b[0] = v >> 8 & 0xff;
	b[1] = v & 0xff;
	return b + 2;
}

static inline void *
putle24(void *p, uint_least32_t v)
{
	unsigned char *b = p;

	b[0] = v & 0xff;
	b[1] = v >> 8 & 0xff;
	b[2] = v >> 16 & 0xff;
	return b + 3;
}

static inline void *
putbe24(void *p, uint_least32_t v)
{
	unsigned char *b = p;

	b[0] = v >> 16 & 0xff;
	b[1] = v >> 8 & 0xff;
	b[2] = v & 0xff;
	return b + 4;
}

static inline void *
putle32(void *p, uint_least32_t v)
{
	unsigned char *b = p;

	b[0] = v & 0xff;
	b[1] = v >> 8 & 0xff;
	b[2] = v >> 16 & 0xff;
	b[3] = v >> 24 & 0xff;
	return b + 4;
}

static inline void *
putbe32(void *p, uint_least32_t v)
{
	unsigned char *b = p;

	b[0] = v >> 24 & 0xff;
	b[1] = v >> 16 & 0xff;
	b[2] = v >> 8 & 0xff;
	b[3] = v & 0xff;
	return b + 4;
}

static inline void *
putle64(void *p, uint_least64_t v)
{
	unsigned char *b = p;

	b[0] = v & 0xff;
	b[1] = v >> 8 & 0xff;
	b[2] = v >> 16 & 0xff;
	b[3] = v >> 24 & 0xff;
	b[4] = v >> 32 & 0xff;
	b[5] = v >> 40 & 0xff;
	b[6] = v >> 48 & 0xff;
	b[7] = v >> 56 & 0xff;
	return b + 8;
}

static inline void *
putbe64(void *p, uint_least64_t v)
{
	unsigned char *b = p;

	b[0] = v >> 56 & 0xff;
	b[1] = v >> 48 & 0xff;
	b[2] = v >> 40 & 0xff;
	b[3] = v >> 32 & 0xff;
	b[4] = v >> 24 & 0xff;
	b[5] = v >> 16 & 0xff;
	b[6] = v >> 8 & 0xff;
	b[7] = v & 0xff;
	return b + 8;
}

static inline uint_least16_t
getle16(const void *p)
{
	const unsigned char *b = p;
	uint_least16_t v;

	v = b[0] & 0xffu;
	v |= (b[1] & 0xffu) << 8;
	return v;
}

static inline uint_least16_t
getbe16(const void *p)
{
	const unsigned char *b = p;
	uint_least16_t v;

	v = (b[0] & 0xffu) << 8;
	v |= b[1] & 0xffu;
	return v;
}

static inline uint_least32_t
getle24(const void *p)
{
	const unsigned char *b = p;
	uint_least32_t v;

	v = b[0] & 0xfful;
	v |= (b[1] & 0xfful) << 8;
	v |= (b[2] & 0xfful) << 16;
	return v;
}

static inline uint_least32_t
getbe24(const void *p)
{
	const unsigned char *b = p;
	uint_least32_t v;

	v = (b[0] & 0xfful) << 16;
	v |= (b[1] & 0xfful) << 8;
	v |= b[2] & 0xfful;
	return v;
}

static inline uint_least32_t
getle32(const void *p)
{
	const unsigned char *b = p;
	uint_least32_t v;

	v = b[0] & 0xfful;
	v |= (b[1] & 0xfful) << 8;
	v |= (b[2] & 0xfful) << 16;
	v |= (b[3] & 0xfful) << 24;
	return v;
}

static inline uint_least32_t
getbe32(const void *p)
{
	const unsigned char *b = p;
	uint_least32_t v;

	v = (b[0] & 0xfful) << 24;
	v |= (b[1] & 0xfful) << 16;
	v |= (b[2] & 0xfful) << 8;
	v |= b[3] & 0xfful;
	return v;
}

static inline uint_least64_t
getle64(const void *p)
{
	const unsigned char *b = p;
	uint_least64_t v;

	v = b[0] & 0xffull;
	v |= (b[1] & 0xffull) << 8;
	v |= (b[2] & 0xffull) << 16;
	v |= (b[3] & 0xffull) << 24;
	v |= (b[4] & 0xffull) << 32;
	v |= (b[5] & 0xffull) << 40;
	v |= (b[6] & 0xffull) << 48;
	v |= (b[7] & 0xffull) << 56;
	return v;
}

static inline uint_least64_t
getbe64(const void *p)
{
	const unsigned char *b = p;
	uint_least64_t v;

	v = (b[0] & 0xffull) << 56;
	v |= (b[1] & 0xffull) << 48;
	v |= (b[2] & 0xffull) << 40;
	v |= (b[3] & 0xffull) << 32;
	v |= (b[4] & 0xffull) << 24;
	v |= (b[5] & 0xffull) << 16;
	v |= (b[6] & 0xffull) << 8;
	v |= b[7] & 0xffull;
	return v;
}

#endif
