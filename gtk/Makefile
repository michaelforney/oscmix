.POSIX:

CFLAGS+=-Wno-overlength-strings

-include ../config.mk

GTK_CFLAGS?=$$(pkg-config --cflags --cflags gtk+-3.0)
GTK_LDFLAGS?=$$(pkg-config --libs-only-L --libs-only-other gtk+-3.0)
GTK_LDLIBS?=$$(pkg-config --libs-only-l gtk+-3.0)
GLIB_COMPILE_RESOURCES?=glib-compile-resources
GLIB_COMPILE_SCHEMAS?=glib-compile-schemas

CFLAGS+=$(GTK_CFLAGS)
LDFLAGS+=$(GTK_LDFLAGS)
LDLIBS+=$(GTK_LDLIBS)

OBJ=\
	main.o\
	mixer.o\
	channel.o\
	eqplot.o\
	scaleentry.o\
	resources.o\
	../osc.o

RES=\
	oscmix.ui\
	channel.ui\
	oscmix.css\
	eq-peak.svg\
	eq-lowshelf.svg\
	eq-highshelf.svg\
	eq-lowpass.svg

all: oscmix-gtk gschemas.compiled

$(OBJ): channel.h scaleentry.h mixer.h eqplot.h

resources.c: resources.xml $(RES)
	$(GLIB_COMPILE_RESOURCES) --generate-source resources.xml

gschemas.compiled: oscmix.gschema.xml
	$(GLIB_COMPILE_SCHEMAS) .

oscmix-gtk: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS) -l m

.PHONY: clean
clean:
	rm -f oscmix-gtk resources.c gschemas.compiled $(OBJ)
