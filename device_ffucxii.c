#include <assert.h>
#include <stdio.h>
#include "oscmix.h"
#include "device.h"
#include "intpack.h"

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
	{"Analog 1", OUTPUT_HAS_REFLEVEL, .reflevel={reflevel_output, LEN(reflevel_output)}},
	{"Analog 2", OUTPUT_HAS_REFLEVEL, .reflevel={reflevel_output, LEN(reflevel_output)}},
	{"Analog 3", OUTPUT_HAS_REFLEVEL, .reflevel={reflevel_output, LEN(reflevel_output)}},
	{"Analog 4", OUTPUT_HAS_REFLEVEL, .reflevel={reflevel_output, LEN(reflevel_output)}},
	{"Analog 5", OUTPUT_HAS_REFLEVEL, .reflevel={reflevel_output, LEN(reflevel_output)}},
	{"Analog 6", OUTPUT_HAS_REFLEVEL, .reflevel={reflevel_output, LEN(reflevel_output)}},
	{"Phones 7", OUTPUT_HAS_REFLEVEL, .reflevel={reflevel_phones, LEN(reflevel_phones)}},
	{"Phones 8", OUTPUT_HAS_REFLEVEL, .reflevel={reflevel_phones, LEN(reflevel_phones)}},
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

static const unsigned char inputregs[] = {
	[INPUT_MUTE]=0,
	[INPUT_FXSEND]=1,
	[INPUT_STEREO]=2,
	[INPUT_RECORD]=3,
	[INPUT_PLAYCHAN]=5,
	[INPUT_MSPROC]=6,
	[INPUT_PHASE]=7,
	[INPUT_GAIN]=8,
	[INPUT_REFLEVEL_48V]=9,
	[INPUT_AUTOSET]=10,
};
static const unsigned char inputctls[] = {
	[0]=INPUT_MUTE,
	[1]=INPUT_FXSEND,
	[2]=INPUT_STEREO,
	[3]=INPUT_RECORD,
	[5]=INPUT_PLAYCHAN,
	[6]=INPUT_MSPROC,
	[7]=INPUT_PHASE,
	[8]=INPUT_GAIN,
	[9]=INPUT_REFLEVEL_48V,
	[10]=INPUT_AUTOSET,
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
static const unsigned char outputctls[] = {
	[0]=OUTPUT_VOLUME,
	[1]=OUTPUT_BALANCE,
	[2]=OUTPUT_MUTE,
	[3]=OUTPUT_FXRETURN,
	[4]=OUTPUT_STEREO,
	[5]=OUTPUT_RECORD,
	[7]=OUTPUT_PLAYCHAN,
	[8]=OUTPUT_PHASE,
	[9]=OUTPUT_REFLEVEL,
	[10]=OUTPUT_CROSSFEED,
	[11]=OUTPUT_VOLUMECAL,
};
static const unsigned char roomeqregs[] = {
	[ROOMEQ_DELAY]=0,
	[ROOMEQ_ENABLED]=1,
	[ROOMEQ_BAND1TYPE]=2,
	[ROOMEQ_BAND1GAIN]=3,
	[ROOMEQ_BAND1FREQ]=4,
	[ROOMEQ_BAND1Q]=5,
	[ROOMEQ_BAND2GAIN]=6,
	[ROOMEQ_BAND2FREQ]=7,
	[ROOMEQ_BAND2Q]=8,
	[ROOMEQ_BAND3GAIN]=9,
	[ROOMEQ_BAND3FREQ]=10,
	[ROOMEQ_BAND3Q]=11,
	[ROOMEQ_BAND4GAIN]=12,
	[ROOMEQ_BAND4FREQ]=13,
	[ROOMEQ_BAND4Q]=14,
	[ROOMEQ_BAND5GAIN]=15,
	[ROOMEQ_BAND5FREQ]=16,
	[ROOMEQ_BAND5Q]=17,
	[ROOMEQ_BAND6GAIN]=18,
	[ROOMEQ_BAND6FREQ]=19,
	[ROOMEQ_BAND6Q]=20,
	[ROOMEQ_BAND7GAIN]=21,
	[ROOMEQ_BAND7FREQ]=22,
	[ROOMEQ_BAND7Q]=23,
	[ROOMEQ_BAND8TYPE]=24,
	[ROOMEQ_BAND8GAIN]=25,
	[ROOMEQ_BAND8FREQ]=26,
	[ROOMEQ_BAND8Q]=27,
	[ROOMEQ_BAND9TYPE]=28,
	[ROOMEQ_BAND9GAIN]=29,
	[ROOMEQ_BAND9FREQ]=30,
	[ROOMEQ_BAND9Q]=31,
};

/*
struct segment {
	short base;
	unsigned char step;
	const unsigned char *regs;
	size_t regslen;
};

static const struct segment segments[] = {
	[INPUT]   ={0x0000, 0x40, inputregs,   LEN(inputregs)},
	[OUTPUT]  ={0x0500, 0x40, outputregs,  LEN(outputregs)},
	[REVERB]  ={0x3000, 0x00, reverbregs,  LEN(reverbregs)},
	[ECHO]    ={0x3014, 0x00, echoregs,    LEN(echoregs)},
	[CTLROOM] ={0x3050, 0x00, ctlroomregs, LEN(ctlroomregs)},
	[CLOCK]   ={0x3060, 0x00, clockregs,   LEN(clockregs)},
	[HARDWARE]={0x3070, 0x00, hwregs,      LEN(hwregs)},
	[ROOMEQ]  ={0x35d0, 0x20, roomeqregs,  LEN(roomeqregs)},
};
*/

static unsigned long
regtoctl(int reg)
{
	int idx;

	if (reg < 0)
		return -1;
	if (reg < 0x1000) {
		unsigned char ctl[4];

		static const unsigned char fxctls[][3] = {
			{INPUT_LOWCUT, OUTPUT_LOWCUT, -1},
			{INPUT_LOWCUT, OUTPUT_LOWCUT, LOWCUT_FREQ},
			{INPUT_LOWCUT, OUTPUT_LOWCUT, LOWCUT_SLOPE},
			{INPUT_EQ, OUTPUT_EQ, -1},
			{INPUT_EQ, OUTPUT_EQ, EQ_BAND1TYPE},
			{INPUT_EQ, OUTPUT_EQ, EQ_BAND1GAIN},
			{INPUT_EQ, OUTPUT_EQ, EQ_BAND1FREQ},
			{INPUT_EQ, OUTPUT_EQ, EQ_BAND1Q},
			{INPUT_EQ, OUTPUT_EQ, EQ_BAND2GAIN},
			{INPUT_EQ, OUTPUT_EQ, EQ_BAND2FREQ},
			{INPUT_EQ, OUTPUT_EQ, EQ_BAND2Q},
			{INPUT_EQ, OUTPUT_EQ, EQ_BAND3TYPE},
			{INPUT_EQ, OUTPUT_EQ, EQ_BAND3GAIN},
			{INPUT_EQ, OUTPUT_EQ, EQ_BAND3FREQ},
			{INPUT_EQ, OUTPUT_EQ, EQ_BAND3Q},
			{INPUT_DYNAMICS, OUTPUT_DYNAMICS, -1},
			{INPUT_DYNAMICS, OUTPUT_DYNAMICS, DYNAMICS_GAIN},
			{INPUT_DYNAMICS, OUTPUT_DYNAMICS, DYNAMICS_ATTACK},
			{INPUT_DYNAMICS, OUTPUT_DYNAMICS, DYNAMICS_RELEASE},
			{INPUT_DYNAMICS, OUTPUT_DYNAMICS, DYNAMICS_COMPTHRES},
			{INPUT_DYNAMICS, OUTPUT_DYNAMICS, DYNAMICS_COMPRATIO},
			{INPUT_DYNAMICS, OUTPUT_DYNAMICS, DYNAMICS_EXPTHRES},
			{INPUT_DYNAMICS, OUTPUT_DYNAMICS, DYNAMICS_EXPRATIO},
			{INPUT_AUTOLEVEL, OUTPUT_AUTOLEVEL, -1},
			{INPUT_AUTOLEVEL, OUTPUT_AUTOLEVEL, AUTOLEVEL_MAXGAIN},
			{INPUT_AUTOLEVEL, OUTPUT_AUTOLEVEL, AUTOLEVEL_HEADROOM},
			{INPUT_AUTOLEVEL, OUTPUT_AUTOLEVEL, AUTOLEVEL_RISETIME},
		};
		idx = reg >> 6;
		reg &= 0x3f;
		if (idx < 20) {
			ctl[0] = INPUT;
			ctl[1] = idx;
			if (reg < sizeof inputctls) {
				ctl[2] = inputctls[reg];
				ctl[3] = -1;
			} else {
				reg -= 12;
				if ((unsigned)reg >= sizeof fxctls)
					return -1;
				ctl[2] = fxctls[reg][0];
				ctl[3] = fxctls[reg][2];
			}
		} else if (idx < 40) {
			idx -= 20;
			ctl[0] = OUTPUT;
			ctl[1] = idx;
			if (reg < sizeof outputctls) {
				ctl[2] = outputctls[reg];
				ctl[3] = -1;
			} else {
				reg -= 12;
				if ((unsigned)reg >= sizeof fxctls)
					return -1;
				ctl[2] = fxctls[reg][1];
				ctl[3] = fxctls[reg][2];
			}
		} else {
			return -1;
		}
		return getle32(ctl);
	} else if ((unsigned)reg - 0x3000 < 0x1000) {
		switch (reg) {
		case 0x3000: return CTL(REVERB, -1, -1 -1);
		case 0x3001: return CTL(REVERB, REVERB_TYPE, -1, -1);
		case 0x3002: return CTL(REVERB, REVERB_PREDELAY, -1, -1);
		case 0x3003: return CTL(REVERB, REVERB_LOWCUT, -1, -1);
		case 0x3004: return CTL(REVERB, REVERB_ROOMSCALE, -1, -1);
		case 0x3005: return CTL(REVERB, REVERB_ATTACK, -1, -1);
		case 0x3006: return CTL(REVERB, REVERB_HOLD, -1, -1);
		case 0x3007: return CTL(REVERB, REVERB_RELEASE, -1, -1);
		case 0x3008: return CTL(REVERB, REVERB_HIGHCUT, -1, -1);
		case 0x3009: return CTL(REVERB, REVERB_TIME, -1, -1);
		case 0x300A: return CTL(REVERB, REVERB_HIGHDAMP, -1, -1);
		case 0x300B: return CTL(REVERB, REVERB_SMOOTH, -1, -1);
		case 0x300C: return CTL(REVERB, REVERB_VOLUME, -1, -1);
		case 0x300D: return CTL(REVERB, REVERB_WIDTH, -1, -1);

		case 0x3014: return CTL(ECHO, -1, -1, -1);
		case 0x3015: return CTL(ECHO, ECHO_TYPE, -1, -1);
		case 0x3016: return CTL(ECHO, ECHO_DELAY, -1, -1);
		case 0x3017: return CTL(ECHO, ECHO_FEEDBACK, -1, -1);
		case 0x3018: return CTL(ECHO, ECHO_HIGHCUT, -1, -1);
		case 0x3019: return CTL(ECHO, ECHO_VOLUME, -1, -1);
		case 0x301A: return CTL(ECHO, ECHO_WIDTH, -1, -1);

		case 0x3050: return CTL(CTLROOM, CTLROOM_MAINOUT, -1, -1);
		case 0x3051: return CTL(CTLROOM, CTLROOM_MAINMONO, -1, -1);
		case 0x3053: return CTL(CTLROOM, CTLROOM_MUTEENABLE, -1, -1);
		case 0x3054: return CTL(CTLROOM, CTLROOM_DIMREDUCTION, -1, -1);
		case 0x3055: return CTL(CTLROOM, CTLROOM_DIM, -1, -1);
		case 0x3056: return CTL(CTLROOM, CTLROOM_RECALLVOLUME, -1, -1);

		case 0x3064: return CTL(CLOCK, CLOCK_SOURCE, -1, -1);
		case 0x3065: return CTL(CLOCK, CLOCK_SAMPLERATE, -1, -1);
		case 0x3066: return CTL(CLOCK, CLOCK_WCKOUT, -1, -1);
		case 0x3067: return CTL(CLOCK, CLOCK_WCKSINGLE, -1, -1);
		case 0x3068: return CTL(CLOCK, CLOCK_WCKTERM, -1, -1);

		case 0x3078: return CTL(HARDWARE, HARDWARE_OPTICALOUT, -1, -1);
		case 0x3079: return CTL(HARDWARE, HARDWARE_SPDIFOUT, -1, -1);
		case 0x307A: return CTL(HARDWARE, HARDWARE_CCMODE, -1, -1);
		case 0x307B: return CTL(HARDWARE, HARDWARE_CCMIX, -1, -1);
		case 0x307C: return CTL(HARDWARE, HARDWARE_STANDALONEMIDI, -1, -1);
		case 0x307D: return CTL(HARDWARE, HARDWARE_STANDALONEARC, -1, -1);
		case 0x307E: return CTL(HARDWARE, HARDWARE_LOCKKEYS, -1, -1);
		case 0x307F: return CTL(HARDWARE, HARDWARE_REMAPKEYS, -1, -1);
		case 0x3080: return CTL(HARDWARE, HARDWARE_DSPVERLOAD, -1, -1);
		case 0x3081: return CTL(HARDWARE, HARDWARE_DSPAVAIL, -1, -1);
		case 0x3082: return CTL(HARDWARE, HARDWARE_DSPSTATUS, -1, -1);
		case 0x3083: return CTL(HARDWARE, HARDWARE_ARCDELTA, -1, -1);

		case 0x3580: return CTL(DUREC, DUREC_STATUS);
		case 0x3581: return CTL(DUREC, DUREC_TIME);
		case 0x3583: return CTL(DUREC, DUREC_USBLOAD);
		case 0x3584: return CTL(DUREC, DUREC_TOTALSPACE);
		case 0x3585: return CTL(DUREC, DUREC_FREESPACE);
		case 0x3586: return CTL(DUREC, DUREC_NUMFILES);
		case 0x3587: return CTL(DUREC, DUREC_FILE);
		case 0x3588: return CTL(DUREC, DUREC_NEXT);
		case 0x3589: return CTL(DUREC, DUREC_RECORDTIME);

		default: return -1;
		}
	}
	return -1;
}


static int
ctltoreg(unsigned long ctl)
{
	int reg, idx;

	switch (ctl & 0xff) {
	case INPUT:  idx = (ctl >> 8) & 0xff,        ctl &= 0xffff00ff; break;
	case OUTPUT: idx = 20 + ((ctl >> 8) & 0xff); ctl &= 0xffff00ff; break;
	default:     idx = 0; break;
	}
	switch (ctl) {
	case CTL(INPUT,  0, INPUT_MUTE,         -1           ):
	case CTL(OUTPUT, 0, OUTPUT_VOLUME,      -1           ): reg = 0;  break;
	case CTL(INPUT,  0, INPUT_FXSEND,       -1           ):
	case CTL(OUTPUT, 0, OUTPUT_BALANCE,     -1           ): reg = 1;  break;
	case CTL(INPUT,  0, INPUT_STEREO,       -1           ):
	case CTL(OUTPUT, 0, OUTPUT_MUTE,        -1           ): reg = 2;  break;
	case CTL(INPUT,  0, INPUT_RECORD,       -1           ):
	case CTL(OUTPUT, 0, OUTPUT_FXRETURN,    -1           ): reg = 3;  break;
	case CTL(OUTPUT, 0, OUTPUT_STEREO,      -1           ): reg = 4;  break;
	case CTL(INPUT,  0, INPUT_PLAYCHAN,     -1           ):
	case CTL(OUTPUT, 0, OUTPUT_RECORD,      -1           ): reg = 5;  break;
	case CTL(INPUT,  0, INPUT_MSPROC,       -1           ): reg = 6;  break;
	case CTL(INPUT,  0, INPUT_PHASE,        -1           ):
	case CTL(OUTPUT, 0, OUTPUT_PLAYCHAN,    -1           ): reg = 7;  break;
	case CTL(INPUT,  0, INPUT_GAIN,         -1           ):
	case CTL(OUTPUT, 0, OUTPUT_PHASE,       -1           ): reg = 8;  break;
	case CTL(INPUT,  0, INPUT_REFLEVEL_48V, -1           ):
	case CTL(OUTPUT, 0, OUTPUT_REFLEVEL,    -1           ): reg = 9;  break;
	case CTL(INPUT,  0, INPUT_AUTOSET,      -1           ):
	case CTL(OUTPUT, 0, OUTPUT_CROSSFEED,   -1           ): reg = 10; break;
	case CTL(OUTPUT, 0, OUTPUT_VOLUMECAL,   -1           ): reg = 11; break;
	case CTL(INPUT,  0, INPUT_LOWCUT,       -1           ):
	case CTL(OUTPUT, 0, OUTPUT_LOWCUT,      -1           ): reg = 12; break;
	case CTL(INPUT,  0, INPUT_LOWCUT,       LOWCUT_FREQ  ):
	case CTL(OUTPUT, 0, OUTPUT_LOWCUT,      LOWCUT_FREQ  ): reg = 13; break;
	case CTL(INPUT,  0, INPUT_LOWCUT,       LOWCUT_SLOPE ):
	case CTL(OUTPUT, 0, OUTPUT_LOWCUT,      LOWCUT_SLOPE ): reg = 14; break;
	case CTL(INPUT,  0, INPUT_EQ,           -1           ):
	case CTL(OUTPUT, 0, OUTPUT_EQ,          -1           ): reg = 15; break;
	case CTL(INPUT,  0, INPUT_EQ,           EQ_BAND1TYPE ):
	case CTL(OUTPUT, 0, OUTPUT_EQ,          EQ_BAND1TYPE ): reg = 16; break;
	case CTL(INPUT,  0, INPUT_EQ,           EQ_BAND1GAIN ):
	case CTL(OUTPUT, 0, OUTPUT_EQ,          EQ_BAND1GAIN ): reg = 17; break;
	case CTL(INPUT,  0, INPUT_EQ,           EQ_BAND1FREQ ):
	case CTL(OUTPUT, 0, OUTPUT_EQ,          EQ_BAND1FREQ ): reg = 18; break;
	case CTL(INPUT,  0, INPUT_EQ,           EQ_BAND1Q    ):
	case CTL(OUTPUT, 0, OUTPUT_EQ,          EQ_BAND1Q    ): reg = 19; break;
	case CTL(INPUT,  0, INPUT_EQ,           EQ_BAND2GAIN ):
	case CTL(OUTPUT, 0, OUTPUT_EQ,          EQ_BAND2GAIN ): reg = 20; break;
	case CTL(INPUT,  0, INPUT_EQ,           EQ_BAND2FREQ ):
	case CTL(OUTPUT, 0, OUTPUT_EQ,          EQ_BAND2FREQ ): reg = 21; break;
	case CTL(INPUT,  0, INPUT_EQ,           EQ_BAND2Q    ):
	case CTL(OUTPUT, 0, OUTPUT_EQ,          EQ_BAND2Q    ): reg = 22; break;
	case CTL(INPUT,  0, INPUT_EQ,           EQ_BAND3TYPE ):
	case CTL(OUTPUT, 0, OUTPUT_EQ,          EQ_BAND3TYPE ): reg = 23; break;
	case CTL(INPUT,  0, INPUT_EQ,           EQ_BAND3GAIN ):
	case CTL(OUTPUT, 0, OUTPUT_EQ,          EQ_BAND3GAIN ): reg = 24; break;
	case CTL(INPUT,  0, INPUT_EQ,           EQ_BAND3FREQ ):
	case CTL(OUTPUT, 0, OUTPUT_EQ,          EQ_BAND3FREQ ): reg = 25; break;
	case CTL(INPUT,  0, INPUT_EQ,           EQ_BAND3Q    ):
	case CTL(OUTPUT, 0, OUTPUT_EQ,          EQ_BAND3Q    ): reg = 26; break;
	case CTL(INPUT,  0, INPUT_DYNAMICS,     -1           ):
	case CTL(OUTPUT, 0, OUTPUT_DYNAMICS,    -1           ): reg = 27; break;
	case CTL(INPUT,  0, INPUT_DYNAMICS,     DYNAMICS_GAIN     ):
	case CTL(OUTPUT, 0, OUTPUT_DYNAMICS,    DYNAMICS_GAIN     ): reg = 28; break;
	case CTL(INPUT,  0, INPUT_DYNAMICS,     DYNAMICS_ATTACK   ):
	case CTL(OUTPUT, 0, OUTPUT_DYNAMICS,    DYNAMICS_ATTACK   ): reg = 29; break;
	case CTL(INPUT,  0, INPUT_DYNAMICS,     DYNAMICS_RELEASE  ):
	case CTL(OUTPUT, 0, OUTPUT_DYNAMICS,    DYNAMICS_RELEASE  ): reg = 30; break;
	case CTL(INPUT,  0, INPUT_DYNAMICS,     DYNAMICS_COMPTHRES):
	case CTL(OUTPUT, 0, OUTPUT_DYNAMICS,    DYNAMICS_COMPTHRES): reg = 31; break;
	case CTL(INPUT,  0, INPUT_DYNAMICS,     DYNAMICS_COMPRATIO):
	case CTL(OUTPUT, 0, OUTPUT_DYNAMICS,    DYNAMICS_COMPRATIO): reg = 32; break;
	case CTL(INPUT,  0, INPUT_DYNAMICS,     DYNAMICS_EXPTHRES ):
	case CTL(OUTPUT, 0, OUTPUT_DYNAMICS,    DYNAMICS_EXPTHRES ): reg = 33; break;
	case CTL(INPUT,  0, INPUT_DYNAMICS,     DYNAMICS_EXPRATIO ):
	case CTL(OUTPUT, 0, OUTPUT_DYNAMICS,    DYNAMICS_EXPRATIO ): reg = 34; break;
	case CTL(INPUT,  0, INPUT_AUTOLEVEL,    -1           ):
	case CTL(OUTPUT, 0, OUTPUT_AUTOLEVEL,   -1           ): reg = 35; break;
	case CTL(INPUT,  0, INPUT_AUTOLEVEL,    AUTOLEVEL_MAXGAIN ):
	case CTL(OUTPUT, 0, OUTPUT_AUTOLEVEL,   AUTOLEVEL_MAXGAIN ): reg = 36; break;
	case CTL(INPUT,  0, INPUT_AUTOLEVEL,    AUTOLEVEL_HEADROOM):
	case CTL(OUTPUT, 0, OUTPUT_AUTOLEVEL,   AUTOLEVEL_HEADROOM): reg = 37; break;
	case CTL(INPUT,  0, INPUT_AUTOLEVEL,    AUTOLEVEL_RISETIME):
	case CTL(OUTPUT, 0, OUTPUT_AUTOLEVEL,   AUTOLEVEL_RISETIME): reg = 38; break;

	case CTL(DUREC,  DUREC_STOP):
	case CTL(DUREC,  DUREC_PLAY):
	case CTL(DUREC,  DUREC_RECORD): return 0x3E9A;
	case CTL(DUREC,  DUREC_DELETE): return 0x3E9B;
	}
	reg |= idx << 6;
	return reg;
}

#if 0
static int
ctltoreg(int segidx, int idx, int ctl)
{
	const struct segment *seg;

	fprintf(stderr, "ctltoreg %d %d %d\n", segidx, idx, ctl);
	if (segidx > LEN(segments))
		return -1;
	seg = &segments[segidx];
	if (!seg->regs)
		return -1;
	return seg->base + seg->step * idx + seg->regs[ctl];
	switch (ctl & 0xff000000) {
	case INPUT:
		assert(idx < 20);
		assert(ctl & 0xff < LEN(inputregs));
		return idx << 6 | inputregs[ctl & 0xff];
	case OUTPUT:
		assert(ctl >> 16 < 20);
		assert(ctl & 0xff < LEN(outputregs));
		reg = ((ctl >> 16 & 0xff) + 20) << 6 | outputregs[ctl & 0xff];
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
	case REVERB | REVERB_ENABLED:   reg = 0x3000; break;
	case REVERB | REVERB_TYPE:      reg = 0x3001; break;
	case REVERB | REVERB_PREDELAY:  reg = 0x3002; break;
	case REVERB | REVERB_LOWCUT:    reg = 0x3003; break;
	case REVERB | REVERB_ROOMSCALE: reg = 0x3004; break;
	case REVERB | REVERB_ATTACK:    reg = 0x3005; break;
	case REVERB | REVERB_HOLD:      reg = 0x3006; break;
	case REVERB | REVERB_RELEASE:   reg = 0x3007; break;
	case REVERB | REVERB_HIGHCUT:   reg = 0x3008; break;
	case REVERB | REVERB_TIME:      reg = 0x3009; break;
	case REVERB | REVERB_HIGHDAMP:  reg = 0x300A; break;
	case REVERB | REVERB_SMOOTH:    reg = 0x300B; break;
	case REVERB | REVERB_VOLUME:    reg = 0x300C; break;
	case REVERB | REVERB_WIDTH:     reg = 0x300D; break;
	case ECHO | ECHO_ENABLED:       reg = 0x3014; break;
	case ECHO | ECHO_TYPE:          reg = 0x3016; break;
	case ECHO | ECHO_DELAY:         reg = 0x3017; break;
	case ECHO | ECHO_FEEDBACK:      reg = 0x3018; break;
	case ECHO | ECHO_HIGHCUT:       reg = 0x3019; break;
	case ECHO | ECHO_VOLUME:        reg = 0x3020; break;
	case ECHO | ECHO_WIDTH:         reg = 0x3021; break;
	}
	return reg;
}
#endif

const struct device ffucxii = {
	.id = "ffucxii",
	.name = "Fireface UCX II",
	.version = 30,
	.flags = DEVICE_HAS_DUREC | DEVICE_HAS_ROOMEQ,
	.inputs = inputs,
	.inputslen = LEN(inputs),
	.outputs = outputs,
	.outputslen = LEN(outputs),
	.regtoctl = regtoctl,
	.ctltoreg = ctltoreg,
	//.tree = tree,
};