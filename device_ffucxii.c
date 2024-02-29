#include <assert.h>
#include "oscmix.h"
#include "device.h"

#define LEN(a) (sizeof (a) / sizeof *(a))

static const char *const reflevel_input[] = {"+13dBu", "+19dBu"};
static const char *const reflevel_output[] = {"+4dBu", "+13dBu", "+19dBu"};
static const char *const reflevel_phones[] = {"Low", "High"};

static const struct inputinfo inputs[] = {
	{"Mic/Line 1",  INPUT_HAS_GAIN | INPUT_HAS_48V | INPUT_HAS_AUTOSET,
		.gain={0, 750},
	},
	{"Mic/Line 2",  INPUT_HAS_GAIN | INPUT_HAS_48V | INPUT_HAS_AUTOSET,
		.gain={0, 750},
	},
	{"Inst/Line 3", INPUT_HAS_GAIN | INPUT_HAS_REFLEVEL | INPUT_HAS_HIZ | INPUT_HAS_AUTOSET,
		.gain={0, 240},
		.reflevel={reflevel_input, LEN(reflevel_input)}
	},
	{"Inst/Line 4", INPUT_HAS_GAIN | INPUT_HAS_REFLEVEL | INPUT_HAS_HIZ | INPUT_HAS_AUTOSET,
		.gain={0, 240},
		.reflevel={reflevel_input, LEN(reflevel_input)},
	},
	{"Analog 5",    INPUT_HAS_GAIN | INPUT_HAS_REFLEVEL,
		.reflevel={reflevel_input, LEN(reflevel_input)},
	},
	{"Analog 6",    INPUT_HAS_GAIN | INPUT_HAS_REFLEVEL,
		.reflevel={reflevel_input, LEN(reflevel_input)},
	},
	{"Analog 7",    INPUT_HAS_GAIN | INPUT_HAS_REFLEVEL,
		.reflevel={reflevel_input, LEN(reflevel_input)},
	},
	{"Analog 8",    INPUT_HAS_GAIN | INPUT_HAS_REFLEVEL,
		.reflevel={reflevel_input, LEN(reflevel_input)},
	},
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
	{"Analog 1", OUTPUT_REFLEVEL,
		.reflevel={reflevel_output, LEN(reflevel_output)},
	},
	{"Analog 2", OUTPUT_REFLEVEL,
		.reflevel={reflevel_output, LEN(reflevel_output)},
	},
	{"Analog 3", OUTPUT_REFLEVEL,
		.reflevel={reflevel_output, LEN(reflevel_output)},
	},
	{"Analog 4", OUTPUT_REFLEVEL,
		.reflevel={reflevel_output, LEN(reflevel_output)},
	},
	{"Analog 5", OUTPUT_REFLEVEL,
		.reflevel={reflevel_output, LEN(reflevel_output)},
	},
	{"Analog 6", OUTPUT_REFLEVEL,
		.reflevel={reflevel_output, LEN(reflevel_output)},
	},
	{"Phones 7", OUTPUT_REFLEVEL,
		.reflevel={reflevel_phones, LEN(reflevel_phones)},
	},
	{"Phones 8", OUTPUT_REFLEVEL,
		.reflevel={reflevel_phones, LEN(reflevel_phones)},
	},
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

#if 0
static unsigned long
regtoctl(int reg)
{
	unsigned long ctl;

	if (reg < 0x2000) {
		idx = reg >> 6;
		reg &= 0x3f;
		if (idx < 20) {
			static const unsigned char ctls[] = {
				INPUT_MUTE,
				INPUT_FXSEND,
				INPUT_STEREO,
				INPUT_RECORD,
				0,  /* unused */
				INPUT_PLAYCHAN,
				INPUT_MSPROC,
				INPUT_PHASE,
				INPUT_GAIN,
				INPUT_REFLEVEL_48V,
				INPUT_AUTOSET,
				INPUT_HIZ,
			};
			if (reg > sizeof ctls || !ctls[reg])
				return -1;
			ctl |= INPUT | ctls[reg];
		} else {
			static const unsigned char ctls[] = {
				OUTPUT_VOLUME,
				OUTPUT_BALANCE,
				OUTPUT_MUTE,
				OUTPUT_FXRETURN,
				OUTPUT_STEREO,
				OUTPUT_RECORD,
				0,  /* unused */
				OUTPUT_PLAYCHAN,
				OUTPUT_PHASE,
				OUTPUT_REFLEVEL,
				OUTPUT_CROSSFEED,
				OUTPUT_VOLUMECAL,
			};
			ctl |= OUTPUT;
			idx -= 20;
			if (idx >= 20)
				return -1;
		}
	}
}
#endif

static int
ctltoreg(unsigned long ctl)
{
	/*
	static const unsigned char inputregs[] = {
		[INPUT_MUTE]=0,
		[INPUT_FXSEND]=1,
		[INPUT_STEREO]=2,
		[INPUT_RECORD]=3,
		[INPUT_PLAYCHAN]=5,
		[INPUT_MSPROC]=6,
		[INPUT_PHASE]=7,
		[INPUT_GAIN]=8,
		[INPUT_48V]=9,
		[INPUT_REFLEVEL]=9,
		[INPUT_AUTOSET]=10,
	};
	static const unsigned char outputregs[] = {
		[OUTPUT_VOLUME]=0,
		[OUTPUT_BALANCE]=1,
		[OUTPUT_MUTE]=2,
		[OUTPUT_FXRETURN]=3,
		[OUTPUT_STEREO]=4,
		[OUTPUT_RECORD]=5,
		[OUTPUT_PLAYCHAN]=7,
		[OUTPUT_PHASE]=8,
		[OUTPUT_REFLEVEL]=9,
		[OUTPUT_CROSSFEED]=10,
		[OUTPUT_VOLUMECAL]=11,
	};
	*/
	int reg, idx;

	switch (ctl & 0xff000000) {
	case INPUT:
		idx = (ctl >> 16) & 0xff;
		assert(idx < 20);
		reg = idx << 6;
		switch (ctl & 0xff) {
		case INPUT_MUTE:     reg |= 0; break;
		case INPUT_FXSEND:   reg |= 1; break;
		case INPUT_STEREO:   reg |= 2; break;
		case INPUT_RECORD:   reg |= 3; break;
		case INPUT_PLAYCHAN: reg |= 5; break;
		case INPUT_MSPROC:   reg |= 6; break;
		case INPUT_PHASE:    reg |= 7; break;
		case INPUT_GAIN:     reg |= 8; break;
		case INPUT_48V:      reg |= 9; break;
		case INPUT_REFLEVEL: reg |= 9; break;
		case INPUT_AUTOSET:  reg |= 10; break;
		default: return -1;
		}
		return reg;
	case OUTPUT:
		assert(ctl >> 16 < 20);
		reg = ((ctl >> 16 & 0xff) + 20) << 6;
		switch (ctl & 0xff) {
		case OUTPUT_VOLUME:    reg |= 0; break;
		case OUTPUT_BALANCE:   reg |= 1; break;
		case OUTPUT_MUTE:      reg |= 2; break;
		case OUTPUT_FXRETURN:  reg |= 3; break;
		case OUTPUT_STEREO:    reg |= 4; break;
		case OUTPUT_RECORD:    reg |= 5; break;
		case OUTPUT_PLAYCHAN:  reg |= 7; break;
		case OUTPUT_PHASE:     reg |= 8; break;
		case OUTPUT_REFLEVEL:  reg |= 9; break;
		case OUTPUT_CROSSFEED: reg |= 10; break;
		case OUTPUT_VOLUMECAL: reg |= 11; break;
		default: return -1;
		}
		break;
	case PLAYBACK:
		break;
	}
	return reg;
}

const struct device ffucxii = {
	.id = "ffucxii",
	.name = "Fireface UCX II",
	.version = 30,
	.flags = DEVICE_HAS_DUREC | DEVICE_HAS_ROOMEQ,
	.inputs = inputs,
	.inputslen = LEN(inputs),
	.outputs = outputs,
	.outputslen = LEN(outputs),
	.ctltoreg = ctltoreg,
	//.tree = tree,
};
