#ifndef SYSEX_H
#define SYSEX_H 1

#include <stdint.h>

struct sysex {
	uint_least32_t mfrid;
	unsigned char devid;
	unsigned char subid;
	unsigned char *data;
	size_t datalen;
};

enum {
	SYSEX_MFRID = 1,
	SYSEX_DEVID = 2,
	SYSEX_SUBID = 4,
};

size_t sysexenc(struct sysex *p, unsigned char *dst, int flags);
int sysexdec(struct sysex *p, const unsigned char *src, size_t len, int flags);

void base128enc(unsigned char *dst, const unsigned char *src, size_t len);
int base128dec(unsigned char *dst, const unsigned char *src, size_t len);

static inline uint_least32_t
getle32_7bit(const void *p)
{
	const unsigned char *b = p;
	uint_least32_t v;

	v = b[0] & 0x7ful;
	v |= (b[1] & 0x7ful) << 7;
	v |= (b[2] & 0x7ful) << 14;
	v |= (b[3] & 0x7ful) << 21;
	v |= (b[4] & 0xful) << 28;
	return v;
}

static inline void *
putle32_7bit(void *p, uint_least32_t v)
{
	unsigned char *b = p;

	b[0] = v & 0x7f;
	b[1] = v >> 7 & 0x7f;
	b[2] = v >> 14 & 0x7f;
	b[3] = v >> 21 & 0x7f;
	b[4] = v >> 28 & 0x3f;
	return b + 5;
}

#endif
