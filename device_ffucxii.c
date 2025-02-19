#include <assert.h>
#include <stddef.h>
#include "device.h"
#include "intpack.h"

#define LEN(a) (sizeof (a) / sizeof *(a))

static const char *const reflevel_input[] = {"+13dBu", "+19dBu"};
static const char *const reflevel_output[] = {"+4dBu", "+13dBu", "+19dBu"};
static const char *const reflevel_phones[] = {"Low", "High"};

static const struct channelinfo inputs[] = {
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

static const struct channelinfo outputs[] = {
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

static enum control
regtoctl(int reg, struct param *p)
{
	int idx, flags;

	if (reg < 0)
		return -1;
	if (reg < 0x1000) {
		idx = reg >> 6;
		reg &= 0x3F;
		if (idx < LEN(inputs)) {
			p->in = idx;
			flags = inputs[idx].flags;
		} else {
			idx -= LEN(inputs);
			if (idx >= LEN(outputs))
				return -1;
			p->out = idx;
			flags = outputs[idx].flags;
			if (reg < 0x0C)
				reg |= 0x0500;
		}
	} else if ((unsigned)reg - 0x2000 < 0x500) {
		p->out = reg >> 6 & 0x3F;
		if (p->out >= LEN(outputs))
			return -1;
		p->in = reg & 0x3F;
		if (p->in >= LEN(inputs))
			return -1;
		return MIX;
	} else if ((unsigned)reg - 0x3380 < 20) {
		idx = (reg - 0x3380U) << 1;
		if (idx < LEN(inputs))
			p->in = idx;
		else
			p->out = idx - LEN(inputs);
		return AUTOLEVEL_METER;
	} else if (reg - 0x3180U < 20) {
		idx = (reg - 0x3180) << 1;
		if (idx < LEN(inputs))
			p->in = idx;
		else
			p->out = idx - LEN(inputs);
		return DYNAMICS_METER;
	} else if (reg - 0x35D0U < 0x280) {
		p->out = (reg - 0x35D0) >> 5;
		reg = 0x35D0 | (reg & 0x1F);
	}
	switch (reg) {
	case 0x0000: return INPUT_MUTE;
	case 0x0001: return INPUT_FXSEND;
	case 0x0002: return INPUT_STEREO;
	case 0x0003: return INPUT_RECORD;
	case 0x0004: return UNKNOWN;
	case 0x0005: return INPUT_PLAYCHAN;
	case 0x0006: return INPUT_MSPROC;
	case 0x0007: return INPUT_PHASE;
	case 0x0008: return INPUT_GAIN;
	case 0x0009: return flags & INPUT_HAS_48V ? INPUT_48V : INPUT_REFLEVEL;
	case 0x000A: return INPUT_AUTOSET;
	case 0x000B: return INPUT_HIZ;

	case 0x0500: return OUTPUT_VOLUME;
	case 0x0501: return OUTPUT_PAN;
	case 0x0502: return OUTPUT_MUTE;
	case 0x0503: return OUTPUT_FXRETURN;
	case 0x0504: return OUTPUT_STEREO;
	case 0x0505: return OUTPUT_RECORD;
	case 0x0506: return UNKNOWN;
	case 0x0507: return OUTPUT_PLAYCHAN;
	case 0x0508: return OUTPUT_PHASE;
	case 0x0509: return OUTPUT_REFLEVEL;
	case 0x050A: return OUTPUT_CROSSFEED;
	case 0x050B: return OUTPUT_VOLUMECAL;

	case 0x000C: return LOWCUT;
	case 0x000D: return LOWCUT_FREQ;
	case 0x000E: return LOWCUT_SLOPE;
	case 0x000F: return EQ;
	case 0x0010: return EQ_BAND1TYPE;
	case 0x0011: return EQ_BAND1GAIN;
	case 0x0012: return EQ_BAND1FREQ;
	case 0x0013: return EQ_BAND1Q;
	case 0x0014: return EQ_BAND2GAIN;
	case 0x0015: return EQ_BAND2FREQ;
	case 0x0016: return EQ_BAND2Q;
	case 0x0017: return EQ_BAND3TYPE;
	case 0x0018: return EQ_BAND3GAIN;
	case 0x0019: return EQ_BAND3FREQ;
	case 0x001A: return EQ_BAND3Q;
	case 0x001B: return DYNAMICS;
	case 0x001C: return DYNAMICS_GAIN;
	case 0x001D: return DYNAMICS_ATTACK;
	case 0x001E: return DYNAMICS_RELEASE;
	case 0x001F: return DYNAMICS_COMPTHRES;
	case 0x0020: return DYNAMICS_COMPRATIO;
	case 0x0021: return DYNAMICS_EXPTHRES;
	case 0x0022: return DYNAMICS_EXPRATIO;
	case 0x0023: return AUTOLEVEL;
	case 0x0024: return AUTOLEVEL_MAXGAIN;
	case 0x0025: return AUTOLEVEL_HEADROOM;
	case 0x0026: return AUTOLEVEL_RISETIME;

	case 0x2FC0: return REFRESH;

	case 0x3000: return REVERB;
	case 0x3001: return REVERB_TYPE;
	case 0x3002: return REVERB_PREDELAY;
	case 0x3003: return REVERB_LOWCUT;
	case 0x3004: return REVERB_ROOMSCALE;
	case 0x3005: return REVERB_ATTACK;
	case 0x3006: return REVERB_HOLD;
	case 0x3007: return REVERB_RELEASE;
	case 0x3008: return REVERB_HIGHCUT;
	case 0x3009: return REVERB_TIME;
	case 0x300A: return REVERB_HIGHDAMP;
	case 0x300B: return REVERB_SMOOTH;
	case 0x300C: return REVERB_VOLUME;
	case 0x300D: return REVERB_WIDTH;

	case 0x3014: return ECHO;
	case 0x3015: return ECHO_TYPE;
	case 0x3016: return ECHO_DELAY;
	case 0x3017: return ECHO_FEEDBACK;
	case 0x3018: return ECHO_HIGHCUT;
	case 0x3019: return ECHO_VOLUME;
	case 0x301A: return ECHO_WIDTH;

	case 0x3050: return CTLROOM_MAINOUT;
	case 0x3051: return CTLROOM_MAINMONO;
	case 0x3052: return UNKNOWN;  /* phones source? */
	case 0x3053: return CTLROOM_MUTEENABLE;
	case 0x3054: return CTLROOM_DIMREDUCTION;
	case 0x3055: return CTLROOM_DIM;
	case 0x3056: return CTLROOM_RECALLVOLUME;

	case 0x3064: return CLOCK_SOURCE;
	case 0x3065: return CLOCK_SAMPLERATE;
	case 0x3066: return CLOCK_WCKOUT;
	case 0x3067: return CLOCK_WCKSINGLE;
	case 0x3068: return CLOCK_WCKTERM;

	case 0x3078: return HARDWARE_OPTICALOUT;
	case 0x3079: return HARDWARE_SPDIFOUT;
	case 0x307A: return HARDWARE_CCMODE;
	case 0x307B: return HARDWARE_CCMIX;
	case 0x307C: return HARDWARE_STANDALONEMIDI;
	case 0x307D: return HARDWARE_STANDALONEARC;
	case 0x307E: return HARDWARE_LOCKKEYS;
	case 0x307F: return HARDWARE_REMAPKEYS;
	case 0x3080: return HARDWARE_DSPVERLOAD;
	case 0x3081: return HARDWARE_DSPAVAIL;
	case 0x3082: return HARDWARE_DSPSTATUS;
	case 0x3083: return HARDWARE_ARCDELTA;

	case 0x3580: return DUREC_STATUS;
	case 0x3581: return DUREC_TIME;
	case 0x3582: return UNKNOWN;
	case 0x3583: return DUREC_USBLOAD;
	case 0x3584: return DUREC_TOTALSPACE;
	case 0x3585: return DUREC_FREESPACE;
	case 0x3586: return DUREC_NUMFILES;
	case 0x3587: return DUREC_FILE;
	case 0x3588: return DUREC_NEXT;
	case 0x3589: return DUREC_RECORDTIME;
	case 0x358A: return DUREC_INDEX;
	case 0x358B: return DUREC_NAME0;
	case 0x358C: return DUREC_NAME1;
	case 0x358D: return DUREC_NAME2;
	case 0x358E: return DUREC_NAME3;
	case 0x358F: return DUREC_INFO;
	case 0x3590: return DUREC_LENGTH;

	case 0x35D0: return ROOMEQ_DELAY;
	case 0x35D1: return ROOMEQ;
	case 0x35D2: return ROOMEQ_BAND1TYPE;
	case 0x35D3: return ROOMEQ_BAND1GAIN;
	case 0x35D4: return ROOMEQ_BAND1FREQ;
	case 0x35D5: return ROOMEQ_BAND1Q;
	case 0x35D6: return ROOMEQ_BAND2GAIN;
	case 0x35D7: return ROOMEQ_BAND2FREQ;
	case 0x35D8: return ROOMEQ_BAND2Q;
	case 0x35D9: return ROOMEQ_BAND3GAIN;
	case 0x35DA: return ROOMEQ_BAND3FREQ;
	case 0x35DB: return ROOMEQ_BAND3Q;
	case 0x35DC: return ROOMEQ_BAND4GAIN;
	case 0x35DD: return ROOMEQ_BAND4FREQ;
	case 0x35DE: return ROOMEQ_BAND4Q;
	case 0x35DF: return ROOMEQ_BAND5GAIN;
	case 0x35E0: return ROOMEQ_BAND5FREQ;
	case 0x35E1: return ROOMEQ_BAND5Q;
	case 0x35E2: return ROOMEQ_BAND6GAIN;
	case 0x35E3: return ROOMEQ_BAND6FREQ;
	case 0x35E4: return ROOMEQ_BAND6Q;
	case 0x35E5: return ROOMEQ_BAND7GAIN;
	case 0x35E6: return ROOMEQ_BAND7FREQ;
	case 0x35E7: return ROOMEQ_BAND7Q;
	case 0x35E8: return ROOMEQ_BAND8TYPE;
	case 0x35E9: return ROOMEQ_BAND8GAIN;
	case 0x35EA: return ROOMEQ_BAND8FREQ;
	case 0x35EB: return ROOMEQ_BAND8Q;
	case 0x35EC: return ROOMEQ_BAND9TYPE;
	case 0x35ED: return ROOMEQ_BAND9GAIN;
	case 0x35EE: return ROOMEQ_BAND9FREQ;
	case 0x35EF: return ROOMEQ_BAND9Q;
	}
	return -1;
}

static int
ctltoreg(enum control ctl, const struct param *p)
{
	int reg, idx, flags;

	if ((unsigned)p->in < LEN(inputs)) {
		flags = inputs[p->in].flags;
		idx = p->in;
	} else if ((unsigned)p->out < LEN(outputs)) {
		flags = outputs[p->out].flags;
		idx = 20 + p->out;
	}
	switch (ctl) {
	case INPUT_MUTE:              reg = 0x00; goto channel;
	case INPUT_FXSEND:            reg = 0x01; goto channel;
	case INPUT_STEREO:            reg = 0x02; goto channel;
	case INPUT_RECORD:            reg = 0x03; goto channel;
	case INPUT_PLAYCHAN:          reg = 0x05; goto channel;
	case INPUT_MSPROC:            reg = 0x06; goto channel;
	case INPUT_PHASE:             reg = 0x07; goto channel;
	case INPUT_GAIN:              if (~flags & INPUT_HAS_GAIN) break;
	                              reg = 0x08; goto channel;
	case INPUT_REFLEVEL:          if (~flags & INPUT_HAS_REFLEVEL) break;
	                              reg = 0x09; goto channel;
	case INPUT_48V:               if (~flags & INPUT_HAS_48V) break;
	                              reg = 0x09; goto channel;
	case INPUT_AUTOSET:           if (~flags & INPUT_HAS_AUTOSET) break;
	                              reg = 0x0A; goto channel;
	case INPUT_HIZ:               if (~flags & INPUT_HAS_HIZ) break;
	                              reg = 0x0B; goto channel;
	case OUTPUT_VOLUME:           reg = 0x00; goto channel;
	case OUTPUT_PAN:              reg = 0x01; goto channel;
	case OUTPUT_MUTE:             reg = 0x02; goto channel;
	case OUTPUT_FXRETURN:         reg = 0x03; goto channel;
	case OUTPUT_STEREO:           reg = 0x04; goto channel;
	case OUTPUT_RECORD:           reg = 0x05; goto channel;
	case OUTPUT_PLAYCHAN:         reg = 0x07; goto channel;
	case OUTPUT_PHASE:            if (~flags & INPUT_HAS_REFLEVEL) break;
	                              reg = 0x08; goto channel;
	case OUTPUT_REFLEVEL:         reg = 0x09; goto channel;
	case OUTPUT_CROSSFEED:        reg = 0x0A; goto channel;
	case OUTPUT_VOLUMECAL:        reg = 0x0B; goto channel;
	case LOWCUT:                  reg = 0x0C; goto channel;
	case LOWCUT_FREQ:             reg = 0x0D; goto channel;
	case LOWCUT_SLOPE:            reg = 0x0E; goto channel;
	case EQ:                      reg = 0x0F; goto channel;
	case EQ_BAND1TYPE:            reg = 0x10; goto channel;
	case EQ_BAND1GAIN:            reg = 0x11; goto channel;
	case EQ_BAND1FREQ:            reg = 0x12; goto channel;
	case EQ_BAND1Q:               reg = 0x13; goto channel;
	case EQ_BAND2GAIN:            reg = 0x14; goto channel;
	case EQ_BAND2FREQ:            reg = 0x15; goto channel;
	case EQ_BAND2Q:               reg = 0x16; goto channel;
	case EQ_BAND3TYPE:            reg = 0x17; goto channel;
	case EQ_BAND3GAIN:            reg = 0x18; goto channel;
	case EQ_BAND3FREQ:            reg = 0x19; goto channel;
	case EQ_BAND3Q:               reg = 0x1A; goto channel;
	case DYNAMICS:                reg = 0x1B; goto channel;
	case DYNAMICS_GAIN:           reg = 0x1C; goto channel;
	case DYNAMICS_ATTACK:         reg = 0x1D; goto channel;
	case DYNAMICS_RELEASE:        reg = 0x1E; goto channel;
	case DYNAMICS_COMPTHRES:      reg = 0x1F; goto channel;
	case DYNAMICS_COMPRATIO:      reg = 0x20; goto channel;
	case DYNAMICS_EXPTHRES:       reg = 0x21; goto channel;
	case DYNAMICS_EXPRATIO:       reg = 0x22; goto channel;
	case AUTOLEVEL:               reg = 0x23; goto channel;
	case AUTOLEVEL_MAXGAIN:       reg = 0x24; goto channel;
	case AUTOLEVEL_HEADROOM:      reg = 0x25; goto channel;
	case AUTOLEVEL_RISETIME:      reg = 0x26; goto channel;
	channel:                      if (idx == -1) break;
	                              return idx << 6 | reg;
	case MIX:                     if ((unsigned)p->out >= LEN(outputs)) break;
		                      if ((unsigned)p->in >= LEN(inputs)) break;
		                      return 0x2000 | p->out << 6 | p->in;
	case MIX_LEVEL:               if ((unsigned)p->out >= LEN(outputs)) break;
		                      if ((unsigned)p->in >= LEN(inputs) + LEN(outputs)) break;
		                      return 0x4000 | p->out << 6 | p->in;
	case REVERB:                  return 0x3000;
	case REVERB_TYPE:             return 0x3001;
	case REVERB_PREDELAY:         return 0x3002;
	case REVERB_LOWCUT:           return 0x3003;
	case REVERB_ROOMSCALE:        return 0x3004;
	case REVERB_ATTACK:           return 0x3005;
	case REVERB_HOLD:             return 0x3006;
	case REVERB_RELEASE:          return 0x3007;
	case REVERB_HIGHCUT:          return 0x3008;
	case REVERB_TIME:             return 0x3009;
	case REVERB_HIGHDAMP:         return 0x300A;
	case REVERB_SMOOTH:           return 0x300B;
	case REVERB_VOLUME:           return 0x300C;
	case REVERB_WIDTH:            return 0x300D;
	case ECHO:                    return 0x3014;
	case ECHO_TYPE:               return 0x3015;
	case ECHO_DELAY:              return 0x3016;
	case ECHO_FEEDBACK:           return 0x3017;
	case ECHO_HIGHCUT:            return 0x3018;
	case ECHO_VOLUME:             return 0x3019;
	case ECHO_WIDTH:              return 0x301A;
	case CTLROOM_MAINOUT:         return 0x3050;
	case CTLROOM_MAINMONO:        return 0x3051;
	case CTLROOM_MUTEENABLE:      return 0x3053;
	case CTLROOM_DIMREDUCTION:    return 0x3054;
	case CTLROOM_DIM:             return 0x3055;
	case CTLROOM_RECALLVOLUME:    return 0x3056;
	case CLOCK_SOURCE:            return 0x3064;
	case CLOCK_SAMPLERATE:        return 0x3065;
	case CLOCK_WCKOUT:            return 0x3066;
	case CLOCK_WCKSINGLE:         return 0x3067;
	case CLOCK_WCKTERM:           return 0x3068;
	case HARDWARE_OPTICALOUT:     return 0x3078;
	case HARDWARE_SPDIFOUT:       return 0x3079;
	case HARDWARE_CCMODE:         return 0x307A;
	case HARDWARE_CCMIX:          return 0x307B;
	case HARDWARE_STANDALONEMIDI: return 0x307C;
	case HARDWARE_STANDALONEARC:  return 0x307D;
	case HARDWARE_LOCKKEYS:       return 0x307E;
	case HARDWARE_REMAPKEYS:      return 0x307F;
	case HARDWARE_DSPVERLOAD:     return 0x3080;
	case HARDWARE_DSPAVAIL:       return 0x3081;
	case HARDWARE_DSPSTATUS:      return 0x3082;
	case HARDWARE_ARCDELTA:       return 0x3083;
	case NAME:                    return 0x3200 + (idx << 3);
	case ROOMEQ_DELAY:            reg = 0x35D0; goto roomeq;
	case ROOMEQ:                  reg = 0x35D1; goto roomeq;
	case ROOMEQ_BAND1TYPE:        reg = 0x35D2; goto roomeq;
	case ROOMEQ_BAND1GAIN:        reg = 0x35D3; goto roomeq;
	case ROOMEQ_BAND1FREQ:        reg = 0x35D4; goto roomeq;
	case ROOMEQ_BAND1Q:           reg = 0x35D5; goto roomeq;
	case ROOMEQ_BAND2GAIN:        reg = 0x35D6; goto roomeq;
	case ROOMEQ_BAND2FREQ:        reg = 0x35D7; goto roomeq;
	case ROOMEQ_BAND2Q:           reg = 0x35D8; goto roomeq;
	case ROOMEQ_BAND3GAIN:        reg = 0x35D9; goto roomeq;
	case ROOMEQ_BAND3FREQ:        reg = 0x35DA; goto roomeq;
	case ROOMEQ_BAND3Q:           reg = 0x35DB; goto roomeq;
	case ROOMEQ_BAND4GAIN:        reg = 0x35DC; goto roomeq;
	case ROOMEQ_BAND4FREQ:        reg = 0x35DD; goto roomeq;
	case ROOMEQ_BAND4Q:           reg = 0x35DE; goto roomeq;
	case ROOMEQ_BAND5GAIN:        reg = 0x35DF; goto roomeq;
	case ROOMEQ_BAND5FREQ:        reg = 0x35E0; goto roomeq;
	case ROOMEQ_BAND5Q:           reg = 0x35E1; goto roomeq;
	case ROOMEQ_BAND6GAIN:        reg = 0x35E2; goto roomeq;
	case ROOMEQ_BAND6FREQ:        reg = 0x35E3; goto roomeq;
	case ROOMEQ_BAND6Q:           reg = 0x35E4; goto roomeq;
	case ROOMEQ_BAND7GAIN:        reg = 0x35E5; goto roomeq;
	case ROOMEQ_BAND7FREQ:        reg = 0x35E6; goto roomeq;
	case ROOMEQ_BAND7Q:           reg = 0x35E7; goto roomeq;
	case ROOMEQ_BAND8TYPE:        reg = 0x35E8; goto roomeq;
	case ROOMEQ_BAND8GAIN:        reg = 0x35E9; goto roomeq;
	case ROOMEQ_BAND8FREQ:        reg = 0x35EA; goto roomeq;
	case ROOMEQ_BAND8Q:           reg = 0x35EB; goto roomeq;
	case ROOMEQ_BAND9TYPE:        reg = 0x35EC; goto roomeq;
	case ROOMEQ_BAND9GAIN:        reg = 0x35ED; goto roomeq;
	case ROOMEQ_BAND9FREQ:        reg = 0x35EE; goto roomeq;
	case ROOMEQ_BAND9Q:           reg = 0x35EF; goto roomeq;
	roomeq:                       if (p->out == -1) break;
	                              return reg + (p->out << 5);
	case REFRESH:                 return 0x3E04;
	case DUREC_CONTROL:           return 0x3E9A;
	case DUREC_DELETE:            return 0x3E9B;
	case DUREC_FILE:              return 0x3E9C;
	case DUREC_SEEK:              return 0x3E9D;
	case DUREC_PLAYMODE:          return 0x3EA0;
	default:                      break;
	}
	return -1;
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
	.refresh = 0x67CD,
	.regtoctl = regtoctl,
	.ctltoreg = ctltoreg,
};
