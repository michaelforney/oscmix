.POSIX:

CC=cc -std=c11
PREFIX=/usr/local
BINDIR=$(PREFIX)/bin
MANDIR=$(PREFIX)/share/man

-include config.mk

ALSA?=y
ALSA_CFLAGS?=$$(pkg-config --cflags alsa)
ALSA_LDFLAGS?=$$(pkg-config --libs-only-L --libs-only-other alsa)
ALSA_LDLIBS?=$$(pkg-config --libs-only-l alsa)

GTK?=y
WEB?=n

BIN=oscmix $(BIN-y)
BIN-$(ALSA)+=alsarawio alsaseqio
BIN-$(WEB)+=wsdgram

TARGET=$(BIN) $(TARGET-y)
TARGET-$(GTK)+=gtk
TARGET-$(WEB)+=web

all: $(TARGET)

.PHONY: gtk
gtk:
	$(MAKE) -C gtk

.PHONY: web
web:
	$(MAKE) -C web

OSCMIX_OBJ=\
	main.o\
	osc.o\
	oscmix.o\
	socket.o\
	sysex.o\
	util.o\
	\
	device_ffucxii.o

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
	$(CC) $(LDFLAGS) $(ALSA_LDFLAGS) -o $@ alsaseqio.o $(ALSA_LDLIBS) -l pthread

tools/regtool.o: tools/regtool.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(ALSA_CFLAGS) -c -o $@ tools/regtool.c

tools/regtool: tools/regtool.o
	$(CC) $(LDFLAGS) $(ALSA_LDFLAGS) -o $@ tools/regtool.o $(ALSA_LDLIBS)

.PHONY: install
install: $(BIN)
	mkdir -p $(DESTDIR)$(BINDIR)
	cp $(BIN) $(DESTDIR)$(BINDIR)/
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	cp doc/oscmix.1 $(DESTDIR)$(MANDIR)/man1/

.PHONY: clean
clean:
	rm -f oscmix $(OSCMIX_OBJ)\
		wsdgram $(WSDGRAM_OBJ)\
		alsarawio alsarawio.o\
		alsaseqio alsaseqio.o
	$(MAKE) -C gtk clean
	$(MAKE) -C web clean
