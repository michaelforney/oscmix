.POSIX:

CC=cc -std=c11

-include config.mk

ALSA?=y
ALSA_CFLAGS?=$$(pkg-config --cflags alsa)
ALSA_LDFLAGS?=$$(pkg-config --libs-only-L --libs-only-other alsa)
ALSA_LDLIBS?=$$(pkg-config --libs-only-l alsa)

GTK?=y

TARGET=oscmix wsdgram $(TARGET-y)
TARGET-$(ALSA)+=alsarawio alsaseqio
TARGET-$(GTK)+=gtk

all: $(TARGET)

.PHONY: gtk
gtk:
	$(MAKE) -C gtk

OSCMIX_OBJ=\
	main.o\
	osc.o\
	oscmix.o\
	socket.o\
	sysex.o\
	util.o

WSDGRAM_OBJ=\
	wsdgram.o\
	base64.o\
	http.o\
	sha1.o\
	socket.o\
	util.o

oscmix: $(OSCMIX_OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OSCMIX_OBJ) -l pthread -l m

wsdgram: $(WSDGRAM_OBJ)
	$(CC) $(LDFLAGS) -o $@ $(WSDGRAM_OBJ) -l pthread

alsarawio: alsarawio.o
	$(CC) $(LDFLAGS) -o $@ alsarawio.o

alsaseqio.o: alsaseqio.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(ALSA_CFLAGS) -c -o $@ alsaseqio.c

alsaseqio: alsaseqio.o
	$(CC) $(LDFLAGS) $(ALSA_LDFLAGS) -o $@ alsaseqio.o $(ALSA_LDLIBS)

.PHONY: clean
clean:
	rm -f oscmix $(OSCMIX_OBJ)\
		wsdgram $(WSDGRAM_OBJ)\
		alsarawio alsarawio.o\
		alsaseqio alsaseqio.o
	$(MAKE) -C gtk clean
