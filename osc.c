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

#if 0
struct state {
	const char *c;
	unsigned char l, s1, s2;
};

bool
oscmatch(const char *pat, const char *str)
{
	int i, e;
	struct state *s, states[256];
	struct {
		int n;
		unsigned char s[256];
	} *l1, *l2, *t, list[2];

	assert(*pat == '/');
	i = 0;
	s = states;
	s->c = NULL;
	for (;;) {
		p = *++pat;
		switch (p) {
		case '\0':
		case '/':
			if (s->c)
				s = &states[i];
			goto run;
		case '*':
			if (s->c)
				s = &states[i];
			s->c = p;
			s->l = -1;
			s->s1 = i;
			s->s2 = ++i;
			s = &states[i];
			s->c = NULL;
			break;
		case '{':
			if (s->c)
				s = &states[i];
			e = ++i;
			for (;;) {
				s->c = ++pat;
				pat = strpbrk(opt, "},/");
				if (!pat || *pat == '/' || pat - s->c > UCHAR_MAX)
					return false;
				s->l = pat - s->c;
				s->s1 = e;
				if (*pat == '}') {
					s->s2 = -1;
					break;
				}
				s->s2 = ++i;
				s = &states[i];
			}
			s = &states[e];
			s->c = NULL;
			break;
		case '[':
			pat = strpbrk(pat + 1, "]/");
			if (!pat || *pat == '/')
				return false;
			/* fallthrough */
		case '?':
			if (s->c)
				s = &states[i];
			s->c = p;
			s->l = -1;
			s->s1 = ++i;
			s->s2 = -1;
			s = &states[i];
			s->c = NULL;
			break;
		default:
			if (!s->c) {
				s->c = p;
				s->l = 0;
				s->s1 = ++i;
				s->s2 = -1;
			}
			++s->l;
			break;
		}
	}
run:
	e = i;
	l1 = list;
	l1->n = 1;
	l1->s[0] = 0;
	while (*str) {
		for (i = 0; i < l1->n; ++i) {
			s = &states[l1->s[i]];
			if (s->l == -1) {
				if (s->c == '[') {
				}
				
			} else if (strncmp(str, s->c, s->l) != 0) {
				str += s->l;
				continue;
			}
		}
	}
}

bool
oscmatch(const char *pat, const char *str)
{
	int c, p;
	const char *s, *tail, *star;

	assert(*pat == '/');
	++pat;
	for (;;) {
		c = *str;
		switch (*pat) {
		case '/':
		case '\0':
			return c ? NULL : pat;
		case '?':
			if (!c)
				return NULL;
			break;
		case '*':
			if (!tail) {
				star = pat;
				for (;;) {
					tail = star + 1;
					star = strpbrk(tail, "*/");
					if (!star || *star == '/')
						break;
				}
			}
			break;
		case '[':
			for (;;) {
				p = *++pat;
				if (p == c)
					break;
				if (!p || p ==  '/' || p == ']')
					return NULL;
				if (pat[1] == '-' && pat[2] != ']') {
					for (p = pat[0] + 1; p < pat[2]; ++p) {
						if (p == c)
							goto matchbrack;
					}
				}
			}
		matchbrack:
			pat = strchr(pat, ']');
			if (!pat)
				return NULL;
			break;
		case '{':
			/* XXX: prefix of other choice? */
			s = str;
			for (;;) {
				p = *++pat;
				if (p == ',' || p == '}')
					break;
				if (!p || p == '/')
					return NULL;
				if (p != *s) {
					p = strchr(pat, ',');
					if (!p)
						return NULL;
					s = str;
				}
			}
			pat = strchr(pat, '}');
			if (!pat)
				return NULL;
			break;
		default:
			if (*pat != *str)
				return NULL;
		}
		++pat;
		++str;
	}
}
#endif
