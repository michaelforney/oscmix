#ifndef OSCMIX_H
#define OSCMIX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int init(const char *port);

void handlesysex(const unsigned char *buf, size_t len, uint32_t *payload);
int handleosc(const unsigned char *buf, size_t len);
void handletimer(bool levels);

extern void writemidi(const void *buf, size_t len);
extern void writeosc(const void *buf, size_t len);

#endif
