.POSIX:

CFLAGS+=-std=c11

-include ../config.mk

CFLAGS+=$(GTK_CFLAGS)
LDFLAGS+=$(GTK_LDFLAGS)
LDLIBS+=$(GTK_LDLIBS)
GLIB_COMPILE_RESOURCES?=glib-compile-resources
GLIB_COMPILE_SCHEMAS?=glib-compile-schemas

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

$(OBJ): channel.h scaleentry.h mixer.h

resources.c: resources.xml $(RES)
	$(GLIB_COMPILE_RESOURCES) --generate-source $<

gschemas.compiled: oscmix.gschema.xml
	$(GLIB_COMPILE_SCHEMAS) .

oscmix-gtk: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

.PHONY: clean
clean:
	rm -f oscmix-gtk resources.c gschemas.compiled $(OBJ)
