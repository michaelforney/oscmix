.POSIX:

CFLAGS+=-ggdb

-include ../config.mk

OBJ=\
	test_osc.o\
	../osc.o

all: oscmix-test

oscmix-test: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS) -l m

.PHONY: clean
clean:
	rm -f oscmix-test $(OBJ)
