#ifndef DEVICE_H
#define DEVICE_H

enum inputflags {
	INPUT_HAS_GAIN = 1 << 0,
	INPUT_HAS_AUTOSET = 1 << 1,
	INPUT_HAS_REFLEVEL = 1 << 2,
	INPUT_HAS_48V = 1 << 3,
	INPUT_HAS_HIZ = 1 << 4,
};

struct inputinfo {
	char name[12];
	int flags;
};

enum outputflags {
	OUTPUT_HAS_REFLEVEL = 1 << 0,
};

struct outputinfo {
	const char *name;
	int flags;
};

enum deviceflags {
	DEVICE_HAS_DUREC = 1 << 0,
	DEVICE_HAS_ROOMEQ = 1 << 1,
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
