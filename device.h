#ifndef DEVICE_H
#define DEVICE_H

enum inputflags {
	INPUT_GAIN = 1 << 0,
	INPUT_REFLEVEL = 1 << 1,
	INPUT_48V = 1 << 2,
	INPUT_HIZ = 1 << 3,
};

struct inputinfo {
	char name[12];
	int flags;
};

enum outputflags {
	OUTPUT_REFLEVEL = 1 << 0,
};

struct outputinfo {
	const char *name;
	int flags;
};

enum deviceflags {
	DEVICE_DUREC = 1 << 0,
	DEVICE_ROOMEQ = 1 << 1,
};

struct device {
	const char *id;
	const char *name;
	int version;
	int flags;

	const struct inputinfo *inputs;
	int inputslen;
	const struct outputinfo *outputs;
	int outputslen;
};

#endif
