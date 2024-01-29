.POSIX:

CFLAGS+=-std=c11

-include config.mk

ALSA?=y
ALSA_CFLAGS?=$$(pkg-config --cflags alsa)
ALSA_LDFLAGS?=$$(pkg-config --libs-only-L --libs-only-other alsa)
ALSA_LDLIBS?=$$(pkg-config --libs-only-l alsa)

GTK?=y

TARGET=oscmix $(TARGET-y)
TARGET-$(ALSA)+=alsarawio alsaseqio
TARGET-$(GTK)+=gtk

all: $(TARGET)

.PHONY: gtk
gtk:
	$(MAKE) -C gtk

oscmix: oscmix.o sysex.o osc.o
	$(CC) $(LDFLAGS) -o $@ oscmix.o sysex.o osc.o -l pthread -l m

alsarawio: alsarawio.o
	$(CC) $(LDFLAGS) -o $@ alsarawio.o

alsaseqio.o: alsaseqio.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(ALSA_CFLAGS) -c -o $@ alsaseqio.c

alsaseqio: alsaseqio.o
	$(CC) $(LDFLAGS) $(ALSA_LDFLAGS) -o $@ alsaseqio.o $(ALSA_LDLIBS)

.PHONY: clean
clean:
	rm -f oscmix oscmix.o osc.o sysex.o alsarawio alsarawio.o alsaseqio alsaseqio.o
	$(MAKE) -C gtk clean
