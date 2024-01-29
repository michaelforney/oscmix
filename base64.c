#include "base64.h"

void
base64_encode(char *dst, const unsigned char *src, size_t len)
{
	static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	if (len > 0) {
		for (;;) {
			unsigned long x = (src[0] & 0xfful) << 16;
			dst[3] = len < 3 ? '=' : b64[(x |= src[2] & 0xfful) & 0x3f];
			dst[2] = len < 2 ? '=' : b64[(x |= (src[1] & 0xfful) << 8) >> 6 & 0x3f];
			dst[1] = b64[x >> 12 & 0x3f];
			dst[0] = b64[x >> 18];
			dst += 4;
			if (len < 3)
				break;
			src += 3, len -= 3;
		}
	}
	*dst = '\0';
}
