#define _XOPEN_SOURCE 700
#include <assert.h>
#include <string.h>
#include "osc.h" 
#include "intpack.h"

int
oscend(struct oscmsg *msg)
{
	if (!msg->err) {
		if (*msg->type != '\0')
			msg->err = "extra arguments";
		else if (msg->buf != msg->end)
			msg->err = "extra argument data";
		else
			return 0;
	}
	return -1;
}

int_least32_t
oscgetint(struct oscmsg *msg)
{
	int_least32_t val;

	switch (msg->type ? *msg->type : 'i') {
	case 'i':
		if (msg->end - msg->buf < 4) {
			msg->err = "missing argument data";
			return 0;
		}
		val = getbe32(msg->buf), msg->buf += 4;
		break;
	case 'T':
		val = 1;
		break;
	case 'F':
		val = 0;
		break;
	case '\0':
		msg->err = "not enough arguments";
		return 0;
	default:
		msg->err = "incorrect argument type";
		return 0;
	}
	if (msg->type)
		++msg->type;
	return val;
}

char *
oscgetstr(struct oscmsg *msg)
{
	char *val;
	size_t len;

	switch (msg->type ? *msg->type : 's') {
	case 's':
		val = (char *)msg->buf;
		len = strnlen(val, msg->end - msg->buf);
		if (len == msg->end - msg->buf) {
			msg->err = "string is not nul-terminated";
			return NULL;
		}
		assert((msg->end - msg->buf) % 4 == 0);
		msg->buf += (len + 4) & -4;
		break;
	case 'N':
		val = "";
		break;
	case '\0':
		msg->err = "not enough arguments";
		return NULL;
	default:
		msg->err = "incorrect argument type";
		return NULL;
	}
	if (msg->type)
		++msg->type;
	return val;
}

float
oscgetfloat(struct oscmsg *msg)
{
	union {
		uint32_t i;
		float f;
	} val;

	switch (*msg->type) {
	case 'i':
		if (msg->end - msg->buf < 4) {
			msg->err = "missing argument data";
			return 0;
		}
		val.f = getbe32(msg->buf), msg->buf += 4;
		break;
	case 'f':
		if (msg->end - msg->buf < 4) {
			msg->err = "missing argument data";
			return 0;
		}
		val.i = getbe32(msg->buf), msg->buf += 4;
		break;
	case '\0':
		msg->err = "not enough arguments";
		return 0;
	default:
		msg->err = "incorrect argument type";
		return 0;
	}
	++msg->type;
	return val.f;
}

void
oscputstr(struct oscmsg *msg, const char *str)
{
	unsigned char *pos;
	int pad;

	if (msg->type) {
		assert(*msg->type == 's');
		++msg->type;
	}
	pos = msg->buf;
	pos = memccpy(pos, str, '\0', msg->end - pos);
	if (!pos) {
		msg->err = "string too large";
		return;
	}
	pad = 3 - ((pos - msg->buf) + 3) % 4;
	if (pad > msg->end - pos) {
		msg->err = "string too large";
		return;
	}
	memset(pos, 0, pad), pos += pad;
	msg->buf = pos;
}

void
oscputint(struct oscmsg *msg, int_least32_t val)
{
	unsigned char *pos;

	if (msg->type) {
		assert(*msg->type == 'i');
		++msg->type;
	}
	pos = msg->buf;
	if (msg->end - pos < 4) {
		msg->err = "buffer too small";
		return;
	}
	putbe32(pos, val);
	msg->buf = pos + 4;
}

void
oscputfloat(struct oscmsg *msg, float val)
{
	unsigned char *pos;
	union {
		float f32;
		uint32_t u32;
	} u;

	if (msg->type) {
		assert(*msg->type == 'f');
		++msg->type;
	}
	pos = msg->buf;
	if (msg->end - pos < 4) {
		msg->err = "buffer too small";
		return;
	}
	u.f32 = val;
	putbe32(pos, u.u32);
	msg->buf = pos + 4;
}

/* Match a segment of an OSC path, including glob style patterns.
 *
 * Some of the pattern types will match a variable length, changing the
 * available input for the rest of the match. In those cases we feed whatever
 * would be left of the segment for each valid option back into this function
 * to check the remaining pattern matches.
 */
static char const *
segmentmatch(char const * pat, char const * seg)
{
    while (*seg != '\0' || (*pat != '\0' && *pat != '/')) {
        if (*pat == '*') {
            do { // Try absorbing chars of the seg and see if the remaining pattern matches
                char const * const m = segmentmatch(pat + 1, seg);
                if (m)
                    return m;
            } while (*seg++ != '\0');
            return NULL;
        } else if (*pat == '[') {
            // char group, have to read over the whole thing, but if we find it then it's OK
            bool found = false;
            while (*(++pat) != ']') {
                if (*pat == '\0' || *pat == '/') {
                    return NULL;  // Malformed pattern
                } else if (*pat == *seg) {
                    found = true;
                }
            }
            if (!found) {
                return NULL;
            }
            seg++;  // absorb the 1 char that this matched
            pat++;  // move past ]
        } else if (*pat == '{') {
            // Try all options and see if remainder of pattern matches
            char const * group_end = pat;
            while (*(++group_end) != '}') {
                if (*group_end == '\0' || *group_end == '/')
                    return NULL;  // Malformed group
            }
            group_end++;
            char const * item_start = ++pat;
            int const slen = strlen(seg);
            while (pat < group_end) {
                if (*pat == ',' || *pat == '}') {
                    if (slen >= pat - item_start && !strncmp(seg, item_start, pat - item_start)) {
                        char const * const m = segmentmatch(group_end, &seg[pat - item_start]);
                        if (m)
                            return m;
                    } else {
                        item_start = pat + 1;
                    }
                }
                pat++;
            }
            return NULL;
        } else if (*pat == '?' || *seg == *pat) {
            seg++;
            pat++;
        } else {
            return NULL;
        }
    }
    return (*seg == '\0' && (*pat == '\0' || *pat == '/')) ? pat : NULL;
}

bool
oscmatch(const char *pat, const char *str, char **end)
{
	assert(*pat == '/');
    char const * const matched = segmentmatch(pat + 1, str);
    if (matched && end)
        *end = (char *) matched;
    return matched != NULL;
}
