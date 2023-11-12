#include <string.h>
#include "sysex.h"
#include "intpack.h"

size_t
sysexenc(struct sysex *p, unsigned char *dst, int flags)
{
	size_t len;

	len = 2 + p->datalen;
	if (flags & SYSEX_MFRID)
		len += p->mfrid > 0x7f ? 3 : 1;
	if (flags & SYSEX_DEVID)
		++len;
	if (flags & SYSEX_SUBID)
		++len;
	if (!dst)
		return len;
	*dst++ = 0xf0;
	if (flags & SYSEX_MFRID) {
		if (p->mfrid > 0x7f) {
			*dst++ = 0x00;
			dst = putbe16(dst, p->mfrid);
		} else {
			*dst++ = p->mfrid;
		}
	}
	if (flags & SYSEX_DEVID)
		*dst++ = p->devid;
	if (flags & SYSEX_SUBID)
		*dst++ = p->subid;
	if (p->data)
		memcpy(dst, p->data, p->datalen);
	else
		p->data = dst;
	dst += p->datalen;
	*dst++ = 0xf7;
	return len;
}

int
sysexdec(struct sysex *p, const unsigned char *src, size_t len, int flags)
{
	if (len < 2 || src[0] != 0xf0 || src[len - 1] != 0xf7)
		return -1;
	++src;
	len -= 2;
	if (flags & SYSEX_MFRID) {
		if (len < 1)
			return -1;
		p->mfrid = *src++;
		--len;
		if (!p->mfrid) {
			if (len < 2)
				return -1;
			p->mfrid = getbe16(src);
			src += 2;
			len -= 2;
		}
	}
	if (flags & SYSEX_DEVID) {
		if (len < 1)
			return -1;
		p->devid = *src++;
		--len;
	}
	if (flags & SYSEX_SUBID) {
		if (len < 1)
			return -1;
		p->subid = *src++;
		--len;
	}
	p->data = (unsigned char *)src;
	p->datalen = len;
	return 0;
}

void
base128enc(unsigned char *dst, const unsigned char *src, size_t len)
{
	unsigned b;
	int i;

	b = 0;
	i = 0;
	while (len-- > 0) {
		b = *src++ << i | b;
		*dst++ = b & 0x7f;
		b >>= 7;
		if (++i == 7) {
			*dst++ = b;
			b = 0;
			i = 0;
		}
	}
	if (i > 0)
		*dst++ = b;
}

int
base128dec(unsigned char *dst, const unsigned char *src, size_t len)
{
	const unsigned char *end;
	unsigned b, c;
	int i;

	end = src + len;
	b = 0;
	i = 0;
	while (src != end) {
		c = *src++;
		if (c & ~0x7f)
			return -1;
		b |= c << i;
		if (i == 0) {
			i = 7;
		} else {
			*dst++ = b & 0xff;
			b >>= 8;
			--i;
		}
	}
	return 0;
}
