.POSIX:

CFLAGS+=-std=c11

-include config.mk

all: oscmix regtool
	$(MAKE) -C gtk

oscmix: oscmix.o sysex.o osc.o
	$(CC) $(LDFLAGS) -o $@ oscmix.o sysex.o osc.o $(LDLIBS) -lm

.PHONY: clean
clean:
	rm -f oscmix oscmix.o osc.o sysex.o
	$(MAKE) -C gtk clean
