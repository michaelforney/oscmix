#ifndef OSC_H
#define OSC_H

#include <stdint.h>

struct oscmsg {
	unsigned char *buf, *end;
	const char *type;
	const char *err;
};

int oscend(struct oscmsg *msg);
int_least32_t oscgetint(struct oscmsg *msg);
char *oscgetstr(struct oscmsg *msg);
float oscgetfloat(struct oscmsg *msg);

void oscputstr(struct oscmsg *msg, const char *str);
void oscputint(struct oscmsg *msg, int_least32_t val);
void oscputfloat(struct oscmsg *msg, float val);

#endif
