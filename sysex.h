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

#endif
