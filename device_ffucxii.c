#include "device.h"

#define LEN(a) (sizeof (a) / sizeof *(a))

static const struct inputinfo inputs[] = {
	{"Mic/Line 1",  INPUT_HAS_GAIN | INPUT_HAS_48V | INPUT_HAS_AUTOSET},
	{"Mic/Line 2",  INPUT_HAS_GAIN | INPUT_HAS_48V | INPUT_HAS_AUTOSET},
	{"Inst/Line 3", INPUT_HAS_GAIN | INPUT_HAS_REFLEVEL | INPUT_HAS_HIZ | INPUT_HAS_AUTOSET},
	{"Inst/Line 4", INPUT_HAS_GAIN | INPUT_HAS_REFLEVEL | INPUT_HAS_HIZ | INPUT_HAS_AUTOSET},
	{"Analog 5",    INPUT_HAS_GAIN | INPUT_HAS_REFLEVEL},
	{"Analog 6",    INPUT_HAS_GAIN | INPUT_HAS_REFLEVEL},
	{"Analog 7",    INPUT_HAS_GAIN | INPUT_HAS_REFLEVEL},
	{"Analog 8",    INPUT_HAS_GAIN | INPUT_HAS_REFLEVEL},
	{"SPDIF L"},
	{"SPDIF R"},
	{"AES L"},
	{"AES R"},
	{"ADAT 1"},
	{"ADAT 2"},
	{"ADAT 3"},
	{"ADAT 4"},
	{"ADAT 5"},
	{"ADAT 6"},
	{"ADAT 7"},
	{"ADAT 8"},
};
_Static_assert(LEN(inputs) == 20, "bad inputs");

static const struct outputinfo outputs[] = {
	{"Analog 1", OUTPUT_HAS_REFLEVEL},
	{"Analog 2", OUTPUT_HAS_REFLEVEL},
	{"Analog 3", OUTPUT_HAS_REFLEVEL},
	{"Analog 4", OUTPUT_HAS_REFLEVEL},
	{"Analog 5", OUTPUT_HAS_REFLEVEL},
	{"Analog 6", OUTPUT_HAS_REFLEVEL},
	{"Phones 7", OUTPUT_HAS_REFLEVEL},
	{"Phones 8", OUTPUT_HAS_REFLEVEL},
	{"SPDIF L"},
	{"SPDIF R"},
	{"AES L"},
	{"AES R"},
	{"ADAT 1"},
	{"ADAT 2"},
	{"ADAT 3"},
	{"ADAT 4"},
	{"ADAT 5"},
	{"ADAT 6"},
	{"ADAT 7"},
	{"ADAT 8"},
};
_Static_assert(LEN(outputs) == 20, "bad outputs");

const struct device ffucxii = {
	.id = "ffucxii",
	.name = "Fireface UCX II",
	.version = 30,
	.flags = DEVICE_HAS_DUREC | DEVICE_HAS_ROOMEQ,
	.inputs = inputs,
	.inputslen = LEN(inputs),
	.outputs = outputs,
	.outputslen = LEN(outputs),
};
