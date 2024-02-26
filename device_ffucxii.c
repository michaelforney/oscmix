#include "device.h"

#define LEN(a) (sizeof (a) / sizeof *(a))

static const struct inputinfo inputs[] = {
	{"Mic/Line 1",  INPUT_GAIN | INPUT_48V},
	{"Mic/Line 2",  INPUT_GAIN | INPUT_48V},
	{"Inst/Line 3", INPUT_GAIN | INPUT_REFLEVEL},
	{"Inst/Line 4", INPUT_GAIN | INPUT_REFLEVEL},
	{"Analog 5",    INPUT_GAIN | INPUT_REFLEVEL},
	{"Analog 6",    INPUT_GAIN | INPUT_REFLEVEL},
	{"Analog 7",    INPUT_GAIN | INPUT_REFLEVEL},
	{"Analog 8",    INPUT_GAIN | INPUT_REFLEVEL},
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
	{"Analog 1", OUTPUT_REFLEVEL},
	{"Analog 2", OUTPUT_REFLEVEL},
	{"Analog 3", OUTPUT_REFLEVEL},
	{"Analog 4", OUTPUT_REFLEVEL},
	{"Analog 5", OUTPUT_REFLEVEL},
	{"Analog 6", OUTPUT_REFLEVEL},
	{"Phones 7", OUTPUT_REFLEVEL},
	{"Phones 8", OUTPUT_REFLEVEL},
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
	.flags = DEVICE_DUREC | DEVICE_ROOMEQ,
	.inputs = inputs,
	.inputslen = LEN(inputs),
	.outputs = outputs,
	.outputslen = LEN(outputs),
};
