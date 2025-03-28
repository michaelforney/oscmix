#include <stddef.h>
#include "device.h"

#define LEN(a) (sizeof (a) / sizeof *(a))

static const struct channelinfo inputs[] = {
	{"Analog 1",    INPUT_HAS_GAIN | INPUT_HAS_REFLEVEL},
	{"Analog 2",    INPUT_HAS_GAIN | INPUT_HAS_REFLEVEL},
	{"Analog 3",    INPUT_HAS_GAIN | INPUT_HAS_REFLEVEL},
	{"Analog 4",    INPUT_HAS_GAIN | INPUT_HAS_REFLEVEL},
	{"Analog 5",    INPUT_HAS_GAIN | INPUT_HAS_REFLEVEL},
	{"Analog 6",    INPUT_HAS_GAIN | INPUT_HAS_REFLEVEL},
	{"Analog 7",    INPUT_HAS_GAIN | INPUT_HAS_REFLEVEL},
	{"Analog 8",    INPUT_HAS_GAIN | INPUT_HAS_REFLEVEL},
	{"Mic/Inst 9",  INPUT_HAS_48V | INPUT_HAS_HIZ},
	{"Mic/Inst 10", INPUT_HAS_48V | INPUT_HAS_HIZ},
	{"Mic/Inst 11", INPUT_HAS_48V | INPUT_HAS_HIZ},
	{"Mic/Inst 12", INPUT_HAS_48V | INPUT_HAS_HIZ},
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
	{"ADAT 9"},
	{"ADAT 10"},
	{"ADAT 11"},
	{"ADAT 12"},
	{"ADAT 13"},
	{"ADAT 14"},
	{"ADAT 15"},
	{"ADAT 16"},
};
_Static_assert(LEN(inputs) == 30, "bad inputs");

static const struct channelinfo outputs[] = {
	{"Analog 1",  OUTPUT_HAS_REFLEVEL},
	{"Analog 2",  OUTPUT_HAS_REFLEVEL},
	{"Analog 3",  OUTPUT_HAS_REFLEVEL},
	{"Analog 4",  OUTPUT_HAS_REFLEVEL},
	{"Analog 5",  OUTPUT_HAS_REFLEVEL},
	{"Analog 6",  OUTPUT_HAS_REFLEVEL},
	{"Analog 7",  OUTPUT_HAS_REFLEVEL},
	{"Analog 8",  OUTPUT_HAS_REFLEVEL},
	{"Phones 9",  OUTPUT_HAS_REFLEVEL},
	{"Phones 10", OUTPUT_HAS_REFLEVEL},
	{"Phones 11", OUTPUT_HAS_REFLEVEL},
	{"Phones 12", OUTPUT_HAS_REFLEVEL},
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
	{"ADAT 9"},
	{"ADAT 10"},
	{"ADAT 11"},
	{"ADAT 12"},
	{"ADAT 13"},
	{"ADAT 14"},
	{"ADAT 15"},
	{"ADAT 16"},
};
_Static_assert(LEN(outputs) == 30, "bad outputs");

const struct device ff802 = {
	.id = "ff802",
	.name = "Fireface 802",
	.version = 30,
	.flags = 0,
	.inputs = inputs,
	.inputslen = LEN(inputs),
	.outputs = outputs,
	.outputslen = LEN(outputs),
};
