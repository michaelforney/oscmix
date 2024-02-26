.POSIX:

-include ../config.mk

WASM_CC?=clang --target=wasm32-unknown-wasi
WASM_CFLAGS?=-O2
WASM_LDFLAGS?=-mexec-model=reactor

CC=$(WASM_CC)
CFLAGS=$(WASM_CFLAGS)
LDFLAGS=$(WASM_LDFLAGS)

.PHONY: all
all: oscmix.wasm

OBJ=oscmix.o osc.o sysex.o util.o wasm.o device_ffucxii.o

oscmix.o: ../oscmix.c
	$(CC) $(CFLAGS) -c -o $@ ../oscmix.c

osc.o: ../osc.c
	$(CC) $(CFLAGS) -c -o $@ ../osc.c

sysex.o: ../sysex.c
	$(CC) $(CFLAGS) -c -o $@ ../sysex.c

util.o: ../util.c
	$(CC) $(CFLAGS) -c -o $@ ../util.c

device_ffucxii.o: ../device_ffucxii.c
	$(CC) $(CFLAGS) -c -o $@ ../device_ffucxii.c

oscmix.wasm: $(OBJ) oscmix.imports
	$(CC) $(LDFLAGS) -o $@ -Wl,--export=init,--export=handletimer,--export=handlesysex,--export=handleosc,--export=jsdata,--export=jsdatalen -Wl,--allow-undefined-file=oscmix.imports $(OBJ)

.PHONY: clean
clean:
	rm -f oscmix.wasm $(OBJ)
