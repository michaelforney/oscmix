#ifndef OSCMIX_H
#define OSCMIX_H

void init(void);

void handlesysex(unsigned char *buf, size_t len, uint32_t *payload);
int handleosc(unsigned char *buf, size_t len);
void handletimer(bool levels);

extern void writemidi(const void *buf, size_t len);
extern void writeosc(const void *buf, size_t len);

#endif
