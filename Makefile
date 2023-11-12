.POSIX:

CFLAGS+=-std=c11

-include config.mk

all: oscmix regtool
	$(MAKE) -C gtk

oscmix: oscmix.o sysex.o osc.o
	$(CC) $(LDFLAGS) -o $@ oscmix.o sysex.o osc.o $(LDLIBS) -lm

regtool: regtool.o sysex.o
	$(CC) $(LDFLAGS) -o $@ regtool.o sysex.o $(LDLIBS) -lm

.PHONY: clean
clean:
	rm -f oscmix oscmix.o regtool regtool.o osc.o sysex.o
	$(MAKE) -C gtk clean
