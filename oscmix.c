#define _XOPEN_SOURCE 700  /* for memccpy */
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>  /* for strcasecmp */
#include "device.h"
#include "intpack.h"
#include "oscmix.h"
#include "osc.h"
#include "sysex.h"
#include "util.h"

#define LEN(a) (sizeof (a) / sizeof *(a))
#define PI 3.14159265358979323846

struct oscnode {
	const char *name;
	int reg;
	int (*set)(const struct oscnode *path[], int reg, struct oscmsg *msg);
	int (*new)(const struct oscnode *path[], const char *addr, int reg, int val);
	union {
		struct {
			const char *const *const names;
			size_t nameslen;
		};
		struct {
			short min;
			short max;
			float scale;
		};
	};
	const struct oscnode *child;
};

struct input {
	bool stereo;
	bool mute;
	float width;
};

struct output {
	bool stereo;
	float *mix;
};

struct durecfile {
	short reg[6];
	char name[9];
	unsigned long samplerate;
	unsigned channels;
	unsigned length;
};

int dflag;
static const struct device *device;
static struct input *inputs;
static struct output *outputs;
static struct {
	int status;
	int position;
	int time;
	int usberrors;
	int usbload;
	float totalspace;
	float freespace;
	struct durecfile *files;
	size_t fileslen;
	int file;
	int recordtime;
	int index;
	int next;
	int playmode;
} durec = {.index = -1};
static struct {
	int vers;
	int load;
} dsp;
static bool refreshing;

static void oscsend(const char *addr, const char *type, ...);
static void oscflush(void);
static void oscsendenum(const char *addr, int val, const char *const names[], size_t nameslen);

static void
dump(const char *name, const void *ptr, size_t len)
{
	size_t i;
	const unsigned char *buf;

	buf = ptr;
	if (name)
		fputs(name, stdout);
	if (len > 0) {
		if (name)
			putchar('\t');
		printf("%.2x", buf[0]);
	}
	for (i = 1; i < len; ++i)
		printf(" %.2x", buf[i]);
	putchar('\n');
}

static void
writesysex(int subid, const unsigned char *buf, size_t len, unsigned char *sysexbuf)
{
	struct sysex sysex;
	size_t sysexlen;

	sysex.mfrid = 0x200d;
	sysex.devid = 0x10;
	sysex.data = NULL;
	sysex.datalen = len * 5 / 4;
	sysex.subid = subid;
	sysexlen = sysexenc(&sysex, sysexbuf, SYSEX_MFRID | SYSEX_DEVID | SYSEX_SUBID);
	base128enc(sysex.data, buf, len);
	writemidi(sysexbuf, sysexlen);
}

static int
setreg(unsigned reg, unsigned val)
{
	unsigned long regval;
	unsigned char buf[4], sysexbuf[7 + 5];
	unsigned par;

	val &= 0xffff;
	if (dflag && reg != 0x3f00)
		fprintf(stderr, "setreg %#.4x %#.4x\n", reg, val);
	regval = (reg & 0x7fff) << 16 | val;
	par = regval >> 16 ^ regval;
	par ^= par >> 8;
	par ^= par >> 4;
	par ^= par >> 2;
	par ^= par >> 1;
	regval |= (~par & 1) << 31;
	putle32(buf, regval);

	writesysex(0, buf, sizeof buf, sysexbuf);
	return 0;
}

static int
setint(const struct oscnode *path[], int reg, struct oscmsg *msg)
{
	int_least32_t val;

	val = oscgetint(msg);
	if (oscend(msg) != 0)
		return -1;
	setreg(reg, val);
	return 0;
}

static int
newint(const struct oscnode *path[], const char *addr, int reg, int val)
{
	oscsend(addr, ",i", val);
	return 0;
}

static int
setfixed(const struct oscnode *path[], int reg, struct oscmsg *msg)
{
	const struct oscnode *node;
	float val;

	node = *path;
	val = oscgetfloat(msg);
	if (oscend(msg) != 0)
		return -1;
	setreg(reg, (int)(val / node->scale));
	return 0;
}

static int
newfixed(const struct oscnode *path[], const char *addr, int reg, int val)
{
	const struct oscnode *node;

	node = *path;
	oscsend(addr, ",f", val * node->scale);
	return 0;
}

static int
setenum(const struct oscnode *path[], int reg, struct oscmsg *msg)
{
	const struct oscnode *node;
	const char *str;
	int val;

	node = *path;
	switch (*msg->type) {
	case 's':
		str = oscgetstr(msg);
		if (str) {
			for (val = 0; val < node->nameslen; ++val) {
				if (strcasecmp(str, node->names[val]) == 0)
					break;
			}
			if (val == node->nameslen)
				return -1;
		}
		break;
	default:
		val = oscgetint(msg);
		break;
	}
	if (oscend(msg) != 0)
		return -1;
	setreg(reg, val);
	return 0;
}

static int
newenum(const struct oscnode *path[], const char *addr, int reg, int val)
{
	const struct oscnode *node;

	node = *path;
	oscsendenum(addr, val, node->names, node->nameslen);
	return 0;
}

static int
setbool(const struct oscnode *path[], int reg, struct oscmsg *msg)
{
	bool val;

	val = oscgetint(msg);
	if (oscend(msg) != 0)
		return -1;
	setreg(reg, val);
	return 0;
}

static int
newbool(const struct oscnode *path[], const char *addr, int reg, int val)
{
	oscsend(addr, ",i", val != 0);
	return 0;
}

static int
setmonolevel(int reg, float level)
{
	long val;

	val = lroundf(level * 0x8000);
	assert(val >= 0);
	assert(val <= 0x10000);
	if (val > 0x4000)
		val = (val >> 3) - 0x8000;
	return setreg(reg, val);
}

static void
muteinput(struct input *in, bool mute)
{
	const struct output *out;
	int och, ich;

	if (in->mute == mute)
		return;
	ich = in - inputs;
	if (in->stereo && ich & 1)
		--in, --ich;
	in[0].mute = mute;
	if (in->stereo)
		in[1].mute = mute;
	for (och = 0; och < device->outputslen; ++och) {
		out = &outputs[och];
		if (out->mix[ich] > 0)
			setmonolevel(0x4000 | och << 6 | ich, mute ? 0 : out->mix[ich]);
		if (in->stereo && out->mix[ich + 1] > 0)
			setmonolevel(0x4000 | och << 6 | (ich + 1), mute ? 0 : out->mix[ich + 1]);
	}
}

static int
setinputmute(const struct oscnode *path[], int reg, struct oscmsg *msg)
{
	struct input *in;
	int inidx;
	bool val;

	val = oscgetint(msg);
	if (oscend(msg) != 0)
		return -1;
	inidx = path[-1] - path[-2]->child;
	assert(inidx < device->inputslen);
	/* mutex */
	in = &inputs[inidx];
	setreg(reg, val);
	muteinput(in, val);
	return 0;
}

static int
newinputmute(const struct oscnode *path[], const char *addr, int reg, int val)
{
	struct input *in;
	int inidx;

	inidx = path[-1] - path[-2]->child;
	assert(inidx < device->inputslen);
	in = &inputs[inidx];
	muteinput(in, val);
	return newbool(path, addr, reg, val);
}

static int
setinputstereo(const struct oscnode *path[], int reg, struct oscmsg *msg)
{
	int idx;
	bool val;

	val = oscgetint(msg);
	if (oscend(msg) != 0)
		return -1;
	idx = (path[-1] - path[-2]->child) & -2;
	assert(idx < device->inputslen);
	inputs[idx].stereo = val;
	inputs[idx + 1].stereo = val;
	setreg(idx << 6 | 2, val);
	setreg((idx + 1) << 6 | 2, val);
	return 0;
}

static int
newinputstereo(const struct oscnode *path[], const char *addr, int reg, int val)
{
	int idx;
	char addrbuf[256];

	idx = (path[-1] - path[-2]->child) & -2;
	assert(idx < device->inputslen);
	inputs[idx].stereo = val;
	inputs[idx + 1].stereo = val;
	addr = addrbuf;
	snprintf(addrbuf, sizeof addrbuf, "/input/%d/stereo", idx + 1);
	oscsend(addr, ",i", val != 0);
	snprintf(addrbuf, sizeof addrbuf, "/input/%d/stereo", idx + 2);
	oscsend(addr, ",i", val != 0);
	return 0;
}

static int
newoutputstereo(const struct oscnode *path[], const char *addr, int reg, int val)
{
	int idx;
	char addrbuf[256];

	idx = (path[-1] - path[-2]->child) & -2;
	assert(idx < device->outputslen);
	outputs[idx].stereo = val;
	outputs[idx + 1].stereo = val;
	addr = addrbuf;
	snprintf(addrbuf, sizeof addrbuf, "/output/%d/stereo", idx + 1);
	oscsend(addr, ",i", val != 0);
	snprintf(addrbuf, sizeof addrbuf, "/output/%d/stereo", idx + 2);
	oscsend(addr, ",i", val != 0);
	return 0;
}

static int
setinputname(const struct oscnode *path[], int reg, struct oscmsg *msg)
{
	const char *name;
	char namebuf[12];
	int i, ch, val;

	ch = path[-1] - path[-2]->child;
	if (ch >= 20)
		return -1;
	name = oscgetstr(msg);
	if (oscend(msg) != 0)
		return -1;
	strncpy(namebuf, name, sizeof namebuf);
	namebuf[sizeof namebuf - 1] = '\0';
	reg = 0x3200 + ch * 8;
	for (i = 0; i < sizeof namebuf; i += 2, ++reg) {
		val = getle16(namebuf + i);
		setreg(reg, val);
	}
	return 0;
}

static int
setinputgain(const struct oscnode *path[], int reg, struct oscmsg *msg)
{
	float val;
	bool mic;

	val = oscgetfloat(msg);
	if (oscend(msg) != 0)
		return -1;
	mic = (path[-1] - path[-2]->child) <= 1;
	if (val < 0 || val > 75 || (!mic && val > 24))
		return -1;
	setreg(reg, val * 10);
	return 0;
}

static int
newinputgain(const struct oscnode *path[], const char *addr, int reg, int val)
{
	oscsend(addr, ",f", val / 10.0);
	return 0;
}

static int
setinput48v(const struct oscnode *path[], int reg, struct oscmsg *msg)
{
	int idx;

	idx = path[-1] - path[-2]->child;
	assert(idx < device->inputslen);
	if (device->inputs[idx].flags & INPUT_48V)
		return setbool(path, reg, msg);
	return -1;
}

static int
newinput48v_reflevel(const struct oscnode *path[], const char *addr, int reg, int val)
{
	static const char *const names[] = {"+7dBu", "+13dBu", "+19dBu"};
	int idx;
	const struct inputinfo *info;

	idx = path[-1] - path[-2]->child;
	assert(idx < device->inputslen);
	info = &device->inputs[idx];
	if (info->flags & INPUT_48V) {
		char addrbuf[256];

		snprintf(addrbuf, sizeof addrbuf, "/input/%d/48v", idx + 1);
		return newbool(path, addrbuf, reg, val);
	} else if (info->flags & INPUT_HIZ) {
		oscsendenum(addr, val & 0xf, names, 2);
		return 0;
	} else if (info->flags & INPUT_REFLEVEL) {
		oscsendenum(addr, val & 0xf, names + 1, 2);
		return 0;
	}
	return -1;
}

static int
setinputhiz(const struct oscnode *path[], int reg, struct oscmsg *msg)
{
	int idx;
	
	idx = path[-1] - path[-2]->child;
	assert(idx < device->inputslen);
	if (device->inputs[idx].flags & INPUT_HIZ)
		return setbool(path, reg, msg);
	return -1;
}

static int
newinputhiz(const struct oscnode *path[], const char *addr, int reg, int val)
{
	int idx;
	
	idx = path[-1] - path[-2]->child;
	assert(idx < device->inputslen);
	if (device->inputs[idx].flags & INPUT_HIZ)
		return newbool(path, addr, reg, val);
	return -1;
}

static int
setoutputloopback(const struct oscnode *path[], int reg, struct oscmsg *msg)
{
	bool val;
	unsigned char buf[4], sysexbuf[7 + 5];
	int idx;

	val = oscgetint(msg);
	if (oscend(msg) != 0)
		return -1;
	idx = path[-1] - path[-2]->child;
	if (val)
		idx |= 0x80;
	putle32(buf, idx);
	writesysex(3, buf, sizeof buf, sysexbuf);
	return 0;
}

static int
seteqdrecord(const struct oscnode *path[], int reg, struct oscmsg *msg)
{
	bool val;
	unsigned char buf[4], sysexbuf[7 + 5];

	val = oscgetint(msg);
	if (oscend(msg) != 0)
		return -1;
	putle32(buf, val);
	writesysex(4, buf, sizeof buf, sysexbuf);
	return 0;
}

static int
newdspload(const struct oscnode *path[], const char *addr, int reg, int val)
{
	if (dsp.load != (val & 0xff)) {
		dsp.load = val & 0xff;
		oscsend("/hardware/dspload", ",i", dsp.load);
	}
	if (dsp.vers != val >> 8) {
		dsp.vers = val >> 8;
		oscsend("/hardware/dspvers", ",i", dsp.vers);
	}
	return 0;
}

static int
newdspavail(const struct oscnode *path[], const char *addr, int reg, int val)
{
	return 0;
}

static int
newdspactive(const struct oscnode *path[], const char *addr, int reg, int val)
{
	return 0;
}

static int
newarcencoder(const struct oscnode *path[], const char *addr, int reg, int val)
{
	return 0;
}

static int
setdb(int reg, float db)
{
	int val;

	val = (isinf(db) && db < 0 ? -650 : lroundf(db * 10.f)) & 0x7fff;
	return setreg(reg, val);
}

static int
setpan(int reg, int pan)
{
	int val;

	val = (pan & 0x7fff) | 0x8000;
	return setreg(reg, val);
}

struct level {
	float vol;  /* 0 (mute) to 1 (0dB) */
	short pan;  /* -100 (left) to 100 (right) */
	short width;  /* -100 (reversed) to 100 (full stereo) */
};

static void
calclevel(const struct output *out, const struct input *in, bool instereo, struct level *l)
{
	int ich;
	float ll, lr, rl, rr, w;

	if (instereo)
		instereo = in->stereo;
	if (instereo && (in - inputs) & 1)
		--in;
	if (out->stereo && (out - outputs) & 1)
		--out;
	ich = in - inputs;
	if (out->stereo) {
		ll = out[0].mix[ich];
		lr = out[1].mix[ich];
		if (instereo) {
			rl = out[0].mix[ich + 1];
			rr = out[1].mix[ich + 1];
			w = ll + rl == 0 ? 1 : 2 * ll / (ll + rl) - 1;
			if (ll < rr) {  /* p > 0 */
				l->vol = 2 * rr / (1 + w);
				l->pan = lroundf(100 * (1 - ll / rr));
			} else {
				l->vol = 2 * ll / (1 + w);
				l->pan = ll == 0 ? 0 : lroundf(100 * (rr / ll - 1));
			}
			l->width = lroundf(100 * w);
		} else {
			l->vol = sqrtf(ll * ll + lr * lr);
			l->pan = lroundf(acosf(ll / l->vol) * 400.f / PI - 100.f);
		}
	} else {
		ll = out[0].mix[ich];
		if (instereo) {
			rl = out[0].mix[ich + 1];
			if (ll < rl) {  /* p > 0 */
				l->vol = 2 * rl;
				l->pan = lroundf(100 * (1 - ll / rl));
			} else {
				l->vol = 2 * ll;
				l->pan = ll == 0 ? 0 : lroundf(100 * (rl / ll - 1));
			}
		} else {
			l->vol = ll;
			l->pan = 0;
		}
	}
}

static void
setlevel(struct output *out, const struct input *in, bool instereo, const struct level *l)
{
	int och, ich;
	float w, theta;
	float ll, lr, rl, rr;

	if (instereo)
		instereo = in->stereo;
	if (instereo && (in - inputs) & 1)
		--in;
	if (out->stereo && (out - outputs) & 1)
		--out;
	och = out - outputs;
	ich = in - inputs;
	if (out->stereo) {
		if (instereo) {
			w = l->width / 100.f;
			if (l->pan > 0) {
				ll = (100 - l->pan) * (1 + w) / 200.f * l->vol;
				lr = (1 - w) / 2.f * l->vol;
				rl = (100 - l->pan) * (1 - w) / 200.f * l->vol;
				rr = (1 + w) / 2.f * l->vol;
			} else {
				ll = (1 + w) / 2.f * l->vol;
				lr = (100 + l->pan) * (1 - w) / 200.f * l->vol;
				rl = (1 - w) / 2.f * l->vol;
				rr = (100 + l->pan) * (1 + w) / 200.f * l->vol;
			}
			out[0].mix[ich + 1] = rl;
			out[1].mix[ich + 1] = rr;
			if (!in->mute) {
				setmonolevel(0x4000 | och << 6 | (ich + 1), rl);
				setmonolevel(0x4000 | (och + 1) << 6 | (ich + 1), rr);
			}
		} else {
			theta = (l->pan + 100) * PI / 400.f;
			ll = cosf(theta) * l->vol;
			lr = sinf(theta) * l->vol;
		}
		out[0].mix[ich] = ll;
		out[1].mix[ich] = lr;
		if (!in->mute) {
			setmonolevel(0x4000 | och << 6 | ich, ll);
			setmonolevel(0x4000 | (och + 1) << 6 | ich, lr);
		}
	} else {
		if (instereo) {
			if (l->pan > 0) {
				ll = (100 - l->pan) / 200.f * l->vol;
				rl = l->vol / 2;
			} else {
				ll = l->vol / 2;
				rl = (100 + l->pan) / 200.f * l->vol;
			}
			out[0].mix[ich + 1] = rl;
			if (!in->mute)
				setmonolevel(0x4000 | och << 6 | (ich + 1), rl);
		} else {
			ll = l->vol;
		}
		out[0].mix[ich] = ll;
		if (!in->mute)
			setmonolevel(0x4000 | och << 6 | ich, ll);
	}
}

static int
setmix(const struct oscnode *path[], int reg, struct oscmsg *msg)
{
	int outidx, inidx;
	float vol;
	struct level level;
	struct output *out;
	struct input *in;

	outidx = path[-2] - path[-3]->child;
	assert(outidx < device->outputslen);
	out = &outputs[outidx];
	if (out->stereo && outidx & 1)
		--out, --outidx;

	inidx = path[0] - path[-1]->child;
	if (reg & 0x20) {
		assert(inidx < device->outputslen);
		in = &inputs[device->inputslen + inidx];
	} else {
		assert(inidx < device->inputslen);
		in = &inputs[inidx];
	}
	if (in->stereo && inidx & 1)
		--in, --inidx;

	calclevel(out, in, 1, &level);
	vol = oscgetfloat(msg);
	level.vol = vol <= -65.f ? 0 : powf(10.f, vol / 20.f);

	if (*msg->type) {
		level.pan = oscgetint(msg);
		if (level.pan < -100)
			level.pan = -100;
		else if (level.pan > 100)
			level.pan = 100;
		if (*msg->type && in->stereo && out->stereo)
			level.width = oscgetfloat(msg);
	}
	if (oscend(msg) != 0)
		return -1;
	setlevel(out, in, 1, &level);
	calclevel(out, in, 0, &level);
	setdb(reg, 20.f * log10f(level.vol));
	setpan(reg, level.pan);
	if (in->stereo) {
		calclevel(out, in + 1, 0, &level);
		setdb(reg + 1, 20.f * log10f(level.vol));
		setpan(reg + 1, level.pan);
	}
	return 0;
}

static int
newmix(const struct oscnode *path[], const char *addr, int reg, int val)
{
	struct output *out;
	struct input *in;
	int outidx, inidx;
	bool ispan;
	char addrbuf[256];
	struct level level;

	outidx = (reg & 0xfff) >> 6;
	inidx = reg & 0x3f;
	if (outidx >= device->outputslen || inidx >= device->inputslen)
		return -1;
	out = &outputs[outidx];
	in = &inputs[inidx];
	if (outidx & 1 && out[-1].stereo)
		return -1;
	ispan = val & 0x8000;
	val = ((val & 0x7fff) ^ 0x4000) - 0x4000;
	calclevel(out, in, 0, &level);
	if (ispan)
		level.pan = val;
	else
		level.vol = val <= -650 ? 0 : powf(10.f, val / 200.f);
	setlevel(out, in, 0, &level);
	if (in->stereo) {
		if (inidx & 1)
			--in, --inidx;
		calclevel(out, in, 1, &level);
	}
	snprintf(addrbuf, sizeof addrbuf, "/mix/%d/input/%d", outidx + 1, inidx + 1);
	oscsend(addrbuf, ",fi", level.vol > 0 ? 20.f * log10f(level.vol) : -INFINITY, level.pan);
	return 0;
}

static long
getsamplerate(int val)
{
	static const long samplerate[] = {
		32000,
		44100,
		48000,
		64000,
		88200,
		96000,
		128000,
		176400,
		192000,
	};
	return val > 0 && val < LEN(samplerate) ? samplerate[val] : 0;
}

static int
newsamplerate(const struct oscnode *path[], const char *addr, int reg, int val)
{
	uint_least32_t rate;

	rate = getsamplerate(val);
	if (rate != 0)
		oscsend(addr, ",i", rate);
	return 0;
}

static int
newdynlevel(const struct oscnode *path[], const char *unused, int reg, int val)
{
	/*
	char addr[256];
	int ch;

	ch = (reg - 0x3180) * 2;
	if (ch < 20) {
		snprintf(addr, sizeof addr, "/input/%d/dynamics/level", ch + 1);
		oscsend(addr, ",i", val >> 8 & 0xff);
		snprintf(addr, sizeof addr, "/input/%d/dynamics/level", ch + 2);
		oscsend(addr, ",i", val & 0xff);
	} else if (ch < 40) {
		ch -= 20;
		snprintf(addr, sizeof addr, "/output/%d/dynamics/level", ch + 1);
		oscsend(addr, ",i", val >> 8 & 0xff);
		snprintf(addr, sizeof addr, "/output/%d/dynamics/level", ch + 2);
		oscsend(addr, ",i", val & 0xff);
	}
	*/
	return 0;
}

static int
newdurecstatus(const struct oscnode *path[], const char *addr, int reg, int val)
{
	static const char *const names[] = {
		"No Media", "Filesystem Error", "Initializing", "Reinitializing",
		[5] = "Stopped", "Recording",
		[10] = "Playing", "Paused",
	};
	int status;
	int position;

	status = val & 0xf;
	if (status != durec.status) {
		durec.status = status;
		oscsendenum("/durec/status", val & 0xf, names, LEN(names));
	}
	position = (val >> 8) * 100 / 65;
	if (position != durec.position) {
		durec.position = position;
		oscsend("/durec/position", ",i", (val >> 8) * 100 / 65);
	}
	return 0;
}

static int
newdurectime(const struct oscnode *path[], const char *addr, int reg, int val)
{
	if (val != durec.time) {
		durec.time = val;
		oscsend(addr, ",i", val);
	}
	return 0;
}

static int
newdurecusbstatus(const struct oscnode *path[], const char *addr, int reg, int val)
{
	int usbload, usberrors;

	usbload = val >> 8;
	if (usbload != durec.usbload) {
		durec.usbload = usbload;
		oscsend("/durec/usbload", ",i", val >> 8);
	}
	usberrors = val & 0xff;
	if (usberrors != durec.usberrors) {
		durec.usberrors = usberrors;
		oscsend("/durec/usberrors", ",i", val & 0xff);
	}
	return 0;
}

static int
newdurectotalspace(const struct oscnode *path[], const char *addr, int reg, int val)
{
	float totalspace;

	totalspace = val / 16.f;
	if (totalspace != durec.totalspace) {
		durec.totalspace = totalspace;
		oscsend(addr, ",f", totalspace);
	}
	return 0;
}

static int
newdurecfreespace(const struct oscnode *path[], const char *addr, int reg, int val)
{
	float freespace;

	freespace = val / 16.f;
	if (freespace != durec.freespace) {
		durec.freespace = freespace;
		oscsend(addr, ",f", freespace);
	}
	return 0;
}

static int
newdurecfileslen(const struct oscnode *path[], const char *addr, int reg, int val)
{
	if (val < 0 || val == durec.fileslen)
		return 0;
	durec.files = realloc(durec.files, val * sizeof *durec.files);
	if (!durec.files)
		fatal(NULL);
	if (val > durec.fileslen)
		memset(durec.files + durec.fileslen, 0, (val - durec.fileslen) * sizeof *durec.files);
	durec.fileslen = val;
	if (durec.index >= durec.fileslen)
		durec.index = -1;
	oscsend(addr, ",i", val);
	return 0;
}

static int
setdurecfile(const struct oscnode *path[], int reg, struct oscmsg *msg)
{
	int val;

	val = oscgetint(msg);
	if (oscend(msg) != 0)
		return -1;
	setreg(0x3e9c, val | 0x8000);
	return 0;
}

static int
newdurecfile(const struct oscnode *path[], const char *addr, int reg, int val)
{
	if (val != durec.file) {
		durec.file = val;
		oscsend(addr, ",i", val);
	}
	return 0;
}

static int
newdurecnext(const struct oscnode *path[], const char *addr, int reg, int val)
{
	static const char *const names[] = {
		"Single", "UFX Single", "Continuous", "Single Next", "Repeat Single", "Repeat All",
	};
	int next, playmode;

	next = ((val & 0xfff) ^ 0x800) - 0x800;
	if (next != durec.next) {
		durec.next = next;
		oscsend(addr, ",i", ((val & 0xfff) ^ 0x800) - 0x800);
	}
	playmode = val >> 12;
	if (playmode != durec.playmode) {
		durec.playmode = playmode;
		oscsendenum("/durec/playmode", val >> 12, names, LEN(names));
	}
	return 0;
}

static int
newdurecrecordtime(const struct oscnode *path[], const char *addr, int reg, int val)
{
	if (val != durec.recordtime) {
		durec.recordtime = val;
		oscsend(addr, ",i", val);
	}
	return 0;
}

static int
newdurecindex(const struct oscnode *path[], const char *addr, int reg, int val)
{
	if (val + 1 > durec.fileslen)
		newdurecfileslen(NULL, "/durec/numfiles", -1, val + 1);
	durec.index = val;
	return 0;
}

static int
newdurecname(const struct oscnode *path[], const char *addr, int reg, int val)
{
	struct durecfile *f;
	char *pos, old[2];

	if (durec.index == -1)
		return 0;
	assert(durec.index < durec.fileslen);
	f = &durec.files[durec.index];
	reg -= 0x358b;
	assert(reg < sizeof f->name / 2);
	pos = f->name + reg * 2;
	memcpy(old, pos, sizeof old);
	putle16(pos, val);
	if (memcmp(old, pos, sizeof old) != 0)
		oscsend("/durec/name", ",is", durec.index, f->name);
	return 0;
}

static int
newdurecinfo(const struct oscnode *path[], const char *unused, int reg, int val)
{
	struct durecfile *f;
	unsigned long samplerate;
	int channels;

	if (durec.index == -1)
		return 0;
	f = &durec.files[durec.index];
	samplerate = getsamplerate(val & 0xff);
	if (samplerate != f->samplerate) {
		f->samplerate = samplerate;
		oscsend("/durec/samplerate", ",ii", durec.index, samplerate);
	}
	channels = val >> 8;
	if (channels != f->channels) {
		f->channels = channels;
		oscsend("/durec/channels", ",ii", durec.index, channels);
	}
	return 0;
}

static int
newdureclength(const struct oscnode *path[], const char *unused, int reg, int val)
{
	struct durecfile *f;

	if (durec.index == -1)
		return 0;
	f = &durec.files[durec.index];
	if (val != f->length) {
		f->length = val;
		oscsend("/durec/length", ",ii", durec.index, val);
	}
	return 0;
}

static int
setdurecstop(const struct oscnode *path[], int reg, struct oscmsg *msg)
{
	if (oscend(msg) != 0)
		return -1;
	setreg(0x3e9a, 0x8120);
	return 0;
}

static int
setdurecplay(const struct oscnode *path[], int reg, struct oscmsg *msg)
{
	if (oscend(msg) != 0)
		return -1;
	setreg(0x3e9a, 0x8123);
	return 0;
}

static int
setdurecrecord(const struct oscnode *path[], int reg, struct oscmsg *msg)
{
	if (oscend(msg) != 0)
		return -1;
	setreg(0x3e9a, 0x8122);
	return 0;
}

static int
setdurecdelete(const struct oscnode *path[], int reg, struct oscmsg *msg)
{
	int val;

	val = oscgetint(msg);
	if (oscend(msg) != 0)
		return -1;
	setreg(0x3e9b, 0x8000 | val);
	return 0;
}

static int
setrefresh(const struct oscnode *path[], int reg, struct oscmsg *msg)
{
	struct input *pb;
	char addr[256];
	int i;

	dsp.vers = -1;
	dsp.load = -1;
	setreg(0x3e04, 0x67cd);
	refreshing = true;
	/* FIXME: needs lock */
	for (i = 0; i < device->outputslen; ++i) {
		pb = &inputs[device->inputslen + i];
		snprintf(addr, sizeof addr, "/playback/%d/stereo", i + 1);
		oscsend(addr, ",i", pb->stereo);
	}
	oscflush();
	return 0;
}

static int
refreshdone(const struct oscnode *path[], const char *addr, int reg, int val)
{
	refreshing = false;
	if (dflag)
		fprintf(stderr, "refresh done\n");
	return 0;
}

static const struct oscnode lowcuttree[] = {
	{"freq", 1, .set=setint, .new=newint, .min=20, .max=500},
	{"slope", 2, .set=setint, .new=newint},
	{0},
};

static const struct oscnode eqtree[] = {
	{"band1type", 1, .set=setenum, .new=newenum, .names=(const char *const[]){
		"Peak", "Low Shelf", "High Pass", "Low Pass",
	}, .nameslen=4},
	{"band1gain", 2, .set=setfixed, .new=newfixed, .scale=0.1, .min=-200, .max=200},
	{"band1freq", 3, .set=setint, .new=newint, .min=20, .max=20000},
	{"band1q", 4, .set=setfixed, .new=newfixed, .scale=0.1, .min=4, .max=99},
	{"band2gain", 5, .set=setfixed, .new=newfixed, .scale=0.1, .min=-200, .max=200},
	{"band2freq", 6, .set=setint, .new=newint, .min=20, .max=20000},
	{"band2q", 7, .set=setfixed, .new=newfixed, .scale=0.1, .min=4, .max=99},
	{"band3type", 8, .set=setenum, .new=newenum, .names=(const char *const[]){
		"Peak", "High Shelf", "Low Pass", "High Pass",
	}, .nameslen=3},
	{"band3gain", 9, .set=setfixed, .new=newfixed, .scale=0.1, .min=-200, .max=200},
	{"band3freq", 10, .set=setint, .new=newint, .min=20, .max=20000},
	{"band3q", 11, .set=setfixed, .new=newfixed, .scale=0.1, .min=4, .max=99},
	{0},
};

static const struct oscnode dynamicstree[] = {
	{"gain", 1, .set=setfixed, .new=newfixed, .scale=0.1, .min=-300, .max=300},
	{"attack", 2, .set=setint, .new=newint, .min=0, .max=200},
	{"release", 3, .set=setint, .new=newint, .min=100, .max=999},
	{"compthres", 4, .set=setfixed, .new=newfixed, .scale=0.1, .min=-600, .max=0},
	{"compratio", 5, .set=setfixed, .new=newfixed, .scale=0.1, .min=10, .max=100},
	{"expthres", 6, .set=setfixed, .new=newfixed, .scale=0.1, .min=-990, .max=200},
	{"expratio", 7, .set=setfixed, .new=newfixed, .scale=0.1, .min=10, .max=100},
	{0},
};

static const struct oscnode autoleveltree[] = {
	{"maxgain", 1, .set=setfixed, .new=newfixed, .scale=0.1, .min=0, .max=180},
	{"headroom", 2, .set=setfixed, .new=newfixed, .scale=0.1, .min=30, .max=120},
	{"risetime", 3, .set=setint, .new=newint, .min=100, .max=9900},
	{0},
};

static const struct oscnode inputtree[] = {
	{"mute", 0x00, .set=setinputmute, .new=newinputmute},
	{"fx", 0x01, .set=setfixed, .new=newfixed, .min=-650, .max=0, .scale=0.1},
	{"stereo", 0x02, .set=setinputstereo, .new=newinputstereo},
	{"record", 0x03, .set=setbool, .new=newbool},
	{"", 0x04},  /* ? */
	{"playchan", 0x05, .set=setint, .new=newint, .min=1, .max=60},
	{"msproc", 0x06, .set=setbool, .new=newbool},
	{"phase", 0x07, .set=setbool, .new=newbool},
	{"gain", 0x08, .set=setinputgain, .new=newinputgain},
	{"48v", 0x09, .set=setinput48v},
	{"reflevel", 0x09, .set=setint, .new=newinput48v_reflevel},
	{"autoset", 0x0a, .set=setbool, .new=newbool},
	{"hi-z", 0x0b, .set=setinputhiz, .new=newinputhiz},
	{"lowcut", 0x0c, .set=setbool, .new=newbool, .child=lowcuttree},
	{"eq", 0x0f, .set=setbool, .new=newbool, .child=eqtree},
	{"dynamics", 0x1b, .set=setbool, .new=newbool, .child=dynamicstree},
	{"autolevel", 0x23, .set=setbool, .new=newbool, .child=autoleveltree},
	{"name", -1, .set=setinputname},
	{0},
};

static const struct oscnode roomeqtree[] = {
	{"delay", 0x00, .set=setfixed, .new=newfixed, .min=0, .max=425, .scale=0.001},
	{"enabled", 0x01, .set=setbool, .new=newbool},
	{"band1type", 0x02, .set=setenum, .new=newenum, .names=(const char *const[]){
		"Peak", "Low Shelf", "High Pass", "Low Pass",
	}, .nameslen=4},
	{"band1gain", 0x03, .set=setfixed, .new=newfixed, .min=-200, .max=200, .scale=0.1},
	{"band1freq", 0x04, .set=setint, .new=newint, .min=20, .max=20000},
	{"band1q", 0x05, .set=setfixed, .new=newfixed, .min=4, .max=99, .scale=0.1},
	{"band2gain", 0x06, .set=setfixed, .new=newfixed, .min=-200, .max=200, .scale=0.1},
	{"band2freq", 0x07, .set=setint, .new=newint, .min=20, .max=20000},
	{"band2q", 0x08, .set=setfixed, .new=newfixed, .min=4, .max=99, .scale=0.1},
	{"band3gain", 0x09, .set=setfixed, .new=newfixed, .min=-200, .max=200, .scale=0.1},
	{"band3freq", 0x0a, .set=setint, .new=newint, .min=20, .max=20000},
	{"band3q", 0x0b, .set=setfixed, .new=newfixed, .min=4, .max=99, .scale=0.1},
	{"band4gain", 0x0c, .set=setfixed, .new=newfixed, .min=-200, .max=200, .scale=0.1},
	{"band4freq", 0x0d, .set=setint, .new=newint, .min=20, .max=20000},
	{"band4q", 0x0e, .set=setfixed, .new=newfixed, .min=4, .max=99, .scale=0.1},
	{"band5gain", 0x0f, .set=setfixed, .new=newfixed, .min=-200, .max=200, .scale=0.1},
	{"band5freq", 0x10, .set=setint, .new=newint, .min=20, .max=20000},
	{"band5q", 0x11, .set=setfixed, .new=newfixed, .min=4, .max=99, .scale=0.1},
	{"band6gain", 0x12, .set=setfixed, .new=newfixed, .min=-200, .max=200, .scale=0.1},
	{"band6freq", 0x13, .set=setint, .new=newint, .min=20, .max=20000},
	{"band6q", 0x14, .set=setfixed, .new=newfixed, .min=4, .max=99, .scale=0.1},
	{"band7gain", 0x15, .set=setfixed, .new=newfixed, .min=-200, .max=200, .scale=0.1},
	{"band7freq", 0x16, .set=setint, .new=newint, .min=20, .max=20000},
	{"band7q", 0x17, .set=setfixed, .new=newfixed, .min=4, .max=99, .scale=0.1},
	{"band8type", 0x18, .set=setenum, .new=newenum, .names=(const char *const[]){
		"Peak", "High Shelf", "Low Pass", "High Pass",
	}, .nameslen=4},
	{"band8gain", 0x19, .set=setfixed, .new=newfixed, .min=-200, .max=200, .scale=0.1},
	{"band8freq", 0x1a, .set=setint, .new=newint, .min=20, .max=20000},
	{"band8q", 0x1b, .set=setfixed, .new=newfixed, .min=4, .max=99, .scale=0.1},
	{"band9type", 0x1c, .set=setenum, .new=newenum, .names=(const char *const[]){
		"Peak", "High Shelf", "Low Pass", "High Pass",
	}, .nameslen=4},
	{"band9gain", 0x1d, .set=setfixed, .new=newfixed, .min=-200, .max=200, .scale=0.1},
	{"band9freq", 0x1e, .set=setint, .new=newint, .min=20, .max=20000},
	{"band9q", 0x1f, .set=setfixed, .new=newfixed, .min=4, .max=99, .scale=0.1},
	{0},
};

static const struct oscnode outputtree[] = {
	{"volume", 0x00, .set=setfixed, .new=newfixed, .scale=0.1, .min=-65.0, .max=6.0},
	{"balance", 0x01, .set=setint, .new=newint, .min=-100, .max=100},
	{"mute", 0x02, .set=setbool, .new=newbool},
	{"fx", 0x03, .set=setfixed, .new=newfixed, .scale=0.1, .min=-65.0, .max=0.0},
	{"stereo", 0x04, .set=setbool, .new=newoutputstereo},
	{"record", 0x05, .set=setbool, .new=newbool},
	{"", 0x06},  /* ? */
	{"playchan", 0x07, .set=setint, .new=newint},
	{"phase", 0x08, .set=setbool, .new=newbool},
	{"reflevel", 0x09, .set=setenum, .new=newenum, .names=(const char *const[]){
		"+4dBu", "+13dBu", "+19dBu",
	}, .nameslen=3}, // TODO: phones
	{"crossfeed", 0x0a, .set=setint, .new=newint},
	{"volumecal", 0x0b, .set=setfixed, .new=newfixed, .min=-2400, .max=300, .scale=0.01},
	{"lowcut", 0x0c, .set=setbool, .new=newbool, .child=lowcuttree},
	{"eq", 0x0f, .set=setbool, .new=newbool, .child=eqtree},
	{"dynamics", 0x1b, .set=setbool, .new=newbool, .child=dynamicstree},
	{"autolevel", 0x23, .set=setbool, .new=newbool, .child=autoleveltree},
	{"roomeq", -1, .child=roomeqtree},
	{"loopback", -1, .set=setoutputloopback},
	{0},
};
static const struct oscnode outputroomeqtree[] = {
	{"roomeq", 0x00, .child=roomeqtree},
	{0},
};
static const struct oscnode mixtree[] = {
	{"input", 0, .child=(const struct oscnode[]){
		{"1", 0x00, .set=setmix, .new=newmix},
		{"2", 0x01, .set=setmix, .new=newmix},
		{"3", 0x02, .set=setmix, .new=newmix},
		{"4", 0x03, .set=setmix, .new=newmix},
		{"5", 0x04, .set=setmix, .new=newmix},
		{"6", 0x05, .set=setmix, .new=newmix},
		{"7", 0x06, .set=setmix, .new=newmix},
		{"8", 0x07, .set=setmix, .new=newmix},
		{"9", 0x08, .set=setmix, .new=newmix},
		{"10", 0x09, .set=setmix, .new=newmix},
		{"11", 0x0a, .set=setmix, .new=newmix},
		{"12", 0x0b, .set=setmix, .new=newmix},
		{"13", 0x0c, .set=setmix, .new=newmix},
		{"14", 0x0d, .set=setmix, .new=newmix},
		{"15", 0x0e, .set=setmix, .new=newmix},
		{"16", 0x0f, .set=setmix, .new=newmix},
		{"17", 0x10, .set=setmix, .new=newmix},
		{"18", 0x11, .set=setmix, .new=newmix},
		{"19", 0x12, .set=setmix, .new=newmix},
		{"20", 0x13, .set=setmix, .new=newmix},
		{0},
	}},
	{"playback", 0x20, .child=(const struct oscnode[]){
		{"1", 0x00, .set=setmix, .new=newmix},
		{"2", 0x01, .set=setmix, .new=newmix},
		{"3", 0x02, .set=setmix, .new=newmix},
		{"4", 0x03, .set=setmix, .new=newmix},
		{"5", 0x04, .set=setmix, .new=newmix},
		{"6", 0x05, .set=setmix, .new=newmix},
		{"7", 0x06, .set=setmix, .new=newmix},
		{"8", 0x07, .set=setmix, .new=newmix},
		{"9", 0x08, .set=setmix, .new=newmix},
		{"10", 0x09, .set=setmix, .new=newmix},
		{"11", 0x0a, .set=setmix, .new=newmix},
		{"12", 0x0b, .set=setmix, .new=newmix},
		{"13", 0x0c, .set=setmix, .new=newmix},
		{"14", 0x0d, .set=setmix, .new=newmix},
		{"15", 0x0e, .set=setmix, .new=newmix},
		{"16", 0x0f, .set=setmix, .new=newmix},
		{"17", 0x10, .set=setmix, .new=newmix},
		{"18", 0x11, .set=setmix, .new=newmix},
		{"19", 0x12, .set=setmix, .new=newmix},
		{"20", 0x13, .set=setmix, .new=newmix},
		{0},
	}},
	{0},
};
static const struct oscnode tree[] = {
	{"input", 0, .child=(const struct oscnode[]){
		{"1", 0x000, .child=inputtree},
		{"2", 0x040, .child=inputtree},
		{"3", 0x080, .child=inputtree},
		{"4", 0x0c0, .child=inputtree},
		{"5", 0x100, .child=inputtree},
		{"6", 0x140, .child=inputtree},
		{"7", 0x180, .child=inputtree},
		{"8", 0x1c0, .child=inputtree},
		{"9", 0x200, .child=inputtree},
		{"10", 0x240, .child=inputtree},
		{"11", 0x280, .child=inputtree},
		{"12", 0x2c0, .child=inputtree},
		{"13", 0x300, .child=inputtree},
		{"14", 0x340, .child=inputtree},
		{"15", 0x380, .child=inputtree},
		{"16", 0x3c0, .child=inputtree},
		{"17", 0x400, .child=inputtree},
		{"18", 0x440, .child=inputtree},
		{"19", 0x480, .child=inputtree},
		{"20", 0x4c0, .child=inputtree},
		{0},
	}},
	{"output", 0x0500, .child=(const struct oscnode[]){
		{"1", 0x000, .child=outputtree},
		{"2", 0x040, .child=outputtree},
		{"3", 0x080, .child=outputtree},
		{"4", 0x0c0, .child=outputtree},
		{"5", 0x100, .child=outputtree},
		{"6", 0x140, .child=outputtree},
		{"7", 0x180, .child=outputtree},
		{"8", 0x1c0, .child=outputtree},
		{"9", 0x200, .child=outputtree},
		{"10", 0x240, .child=outputtree},
		{"11", 0x280, .child=outputtree},
		{"12", 0x2c0, .child=outputtree},
		{"13", 0x300, .child=outputtree},
		{"14", 0x340, .child=outputtree},
		{"15", 0x380, .child=outputtree},
		{"16", 0x3c0, .child=outputtree},
		{"17", 0x400, .child=outputtree},
		{"18", 0x440, .child=outputtree},
		{"19", 0x480, .child=outputtree},
		{"20", 0x4c0, .child=outputtree},
		{0},
	}},
	{"mix", 0x2000, .child=(const struct oscnode[]){
		{"1", 0x000, .child=mixtree},
		{"2", 0x040, .child=mixtree},
		{"3", 0x080, .child=mixtree},
		{"4", 0x0c0, .child=mixtree},
		{"5", 0x100, .child=mixtree},
		{"6", 0x140, .child=mixtree},
		{"7", 0x180, .child=mixtree},
		{"8", 0x1c0, .child=mixtree},
		{"9", 0x200, .child=mixtree},
		{"10", 0x240, .child=mixtree},
		{"11", 0x280, .child=mixtree},
		{"12", 0x2c0, .child=mixtree},
		{"13", 0x300, .child=mixtree},
		{"14", 0x340, .child=mixtree},
		{"15", 0x380, .child=mixtree},
		{"16", 0x3c0, .child=mixtree},
		{"17", 0x400, .child=mixtree},
		{"18", 0x440, .child=mixtree},
		{"19", 0x480, .child=mixtree},
		{"20", 0x4c0, .child=mixtree},
		{0},
	}},
	{"", 0x2fc0, .new=refreshdone},
	{"reverb", 0x3000, .set=setbool, .new=newbool, .child=(const struct oscnode[]){
		{"type", 0x01, .set=setenum, .new=newenum, .names=(const char *const[]){
			"Small Room", "Medium Room", "Large Room", "Walls",
			"Shorty", "Attack", "Swagger", "Old School",
			"Echoistic", "8plus9", "Grand Wide", "Thicker",
			"Envelope", "Gated", "Space",
		}, .nameslen=15},
		{"predelay", 0x02, .set=setint, .new=newint},
		{"lowcut", 0x03, .set=setint, .new=newint},
		{"roomscale", 0x04, .set=setfixed, .new=newfixed, .scale=0.01},
		{"attack", 0x05, .set=setint, .new=newint},
		{"hold", 0x06, .set=setint, .new=newint},
		{"release", 0x07, .set=setint, .new=newint},
		{"highcut", 0x08, .set=setint, .new=newint},
		{"time", 0x09, .set=setfixed, .new=newfixed, .scale=0.1},
		{"highdamp", 0x0a, .set=setint, .new=newint},
		{"smooth", 0x0b, .set=setint, .new=newint},
		{"volume", 0x0c, .set=setfixed, .new=newfixed, .scale=0.1},
		{"width", 0x0d, .set=setfixed, .new=newfixed, .scale=0.01},
		{0},
	}},
	{"echo", 0x3014, .set=setbool, .new=newbool, .child=(const struct oscnode[]){
		{"type", 0x01, .set=setenum, .new=newenum, .names=(const char *const[]){
			"Stereo Echo",
			"Stereo Cross",
			"Pong Echo",
		}, .nameslen=3},
		{"delay", 0x02, .set=setfixed, .new=newfixed, .scale=0.001, .min=0, .max=2000},
		{"feedback", 0x03, .set=setint, .new=newint},
		{"highcut", 0x04, .set=setenum, .new=newenum, .names=(const char *const[]){
			"Off", "16kHz", "12kHz", "8kHz", "4kHz", "2kHz",
		}, .nameslen=6},
		{"volume", 0x05, .set=setfixed, .new=newfixed, .scale=0.1, .min=-650, .max=60},
		{"width", 0x06, .set=setfixed, .new=newfixed, .scale=0.01},
		{0},
	}},
	{"controlroom", 0x3050, .child=(const struct oscnode[]){
		{"mainout", 0, .set=setenum, .new=newenum, .names=(const char *const[]){
			"1/2", "3/4", "5/6", "7/8", "9/10",
			"11/12", "13/14", "15/16", "17/18", "19/20",
		}, .nameslen=10},
		{"mainmono", 1, .set=setbool, .new=newbool},
		{"", 2},  /* phones source? */
		{"muteenable", 3, .set=setbool, .new=newbool},
		{"dimreduction", 4, .set=setfixed, .new=newfixed, .scale=0.1, .min=-650, .max=0},
		{"dim", 5, .set=setbool, .new=newbool},
		{"recallvolume", 6, .set=setfixed, .new=newfixed, .scale=0.1, .min=-650, .max=0},
		{0},
	}},
	{"clock", 0x3060, .child=(const struct oscnode[]){
		{"source", 4, .set=setenum, .new=newenum, .names=(const char *const[]){
			"Internal", "Word Clock", "SPDIF", "AES", "Optical",
		}, .nameslen=5},
		{"samplerate", 5, .new=newsamplerate},
		{"wckout", 6, .set=setbool, .new=newbool},
		{"wcksingle", 7, .set=setbool, .new=newbool},
		{"wckterm", 8, .set=setbool, .new=newbool},
		{0},
	}},
	{"hardware", 0x3070, .child=(const struct oscnode[]){
		{"opticalout", 8, .set=setenum, .new=newenum, .names=(const char *const[]){
			"ADAT", "SPDIF",
		}, .nameslen=2},
		{"spdifout", 9, .set=setenum, .new=newenum, .names=(const char *const[]){
			"Consumer", "Professional",
		}, .nameslen=2},
		{"ccmode", 10},
		{"ccmix", 11, .set=setenum, .new=newenum, .names=(const char *const[]){
			"TotalMix App", "6ch + phones", "8ch", "20ch",
		}, .nameslen=4},
		{"standalonemidi", 12, .set=setbool, .new=newbool},
		{"standalonearc", 13, .set=setenum, .new=newenum, .names=(const char *const[]){
			"Volume", "1s Op", "Normal",
		}, .nameslen=3},
		{"lockkeys", 14, .set=setenum, .new=newenum, .names=(const char *const[]){
			"Off", "Keys", "All",
		}, .nameslen=3},
		{"remapkeys", 15, .set=setbool, .new=newbool},
		{"", 16, .new=newdspload},
		{"", 17, .new=newdspavail},
		{"", 18, .new=newdspactive},
		{"", 19, .new=newarcencoder},

		{"eqdrecord", -1, .set=seteqdrecord},
		{0},
	}},
	{"input", 0x3180, .child=(const struct oscnode[]){
		{"", 0, .new=newdynlevel},
		{"", 1, .new=newdynlevel},
		{"", 2, .new=newdynlevel},
		{"", 3, .new=newdynlevel},
		{"", 4, .new=newdynlevel},
		{"", 5, .new=newdynlevel},
		{"", 6, .new=newdynlevel},
		{"", 7, .new=newdynlevel},
		{"", 8, .new=newdynlevel},
		{"", 9, .new=newdynlevel},
		{"", 10, .new=newdynlevel},
		{"", 11, .new=newdynlevel},
		{"", 12, .new=newdynlevel},
		{"", 13, .new=newdynlevel},
		{"", 14, .new=newdynlevel},
		{"", 15, .new=newdynlevel},
		{"", 16, .new=newdynlevel},
		{"", 17, .new=newdynlevel},
		{"", 18, .new=newdynlevel},
		{"", 19, .new=newdynlevel},
		{0},
	}},
	{"durec", 0x3580, .child=(const struct oscnode[]){
		{"status", 0, .new=newdurecstatus},
		{"time", 1, .new=newdurectime},
		{"", 2},  /* ? */
		{"usbload", 3, .new=newdurecusbstatus},
		{"totalspace", 4, .new=newdurectotalspace},
		{"freespace", 5, .new=newdurecfreespace},
		{"numfiles", 6, .new=newdurecfileslen},
		{"file", 7, .new=newdurecfile, .set=setdurecfile},
		{"next", 8, .new=newdurecnext},
		{"recordtime", 9, .new=newdurecrecordtime},
		{"", 10, .new=newdurecindex},
		{"", 11, .new=newdurecname},
		{"", 12, .new=newdurecname},
		{"", 13, .new=newdurecname},
		{"", 14, .new=newdurecname},
		{"", 15, .new=newdurecinfo},
		{"", 16, .new=newdureclength},

		{"stop", -1, .set=setdurecstop},
		{"play", -1, .set=setdurecplay},
		{"record", -1, .set=setdurecrecord},
		{"delete", -1, .set=setdurecdelete},
		{0},
	}},
	{"output", 0x35d0, .child=(const struct oscnode[]){
		{"1", 0x000, .child=outputroomeqtree},
		{"2", 0x020, .child=outputroomeqtree},
		{"3", 0x040, .child=outputroomeqtree},
		{"4", 0x060, .child=outputroomeqtree},
		{"5", 0x080, .child=outputroomeqtree},
		{"6", 0x0a0, .child=outputroomeqtree},
		{"7", 0x0c0, .child=outputroomeqtree},
		{"8", 0x0e0, .child=outputroomeqtree},
		{"9", 0x100, .child=outputroomeqtree},
		{"10", 0x120, .child=outputroomeqtree},
		{"11", 0x140, .child=outputroomeqtree},
		{"12", 0x160, .child=outputroomeqtree},
		{"13", 0x180, .child=outputroomeqtree},
		{"14", 0x1a0, .child=outputroomeqtree},
		{"15", 0x1c0, .child=outputroomeqtree},
		{"16", 0x1e0, .child=outputroomeqtree},
		{"17", 0x200, .child=outputroomeqtree},
		{"18", 0x220, .child=outputroomeqtree},
		{"19", 0x240, .child=outputroomeqtree},
		{"20", 0x260, .child=outputroomeqtree},
		{0},
	}},
	/* write-only */
	{"refresh", -1, .set=setrefresh},
	{0},
};

static const char *
match(const char *pat, const char *str)
{
	for (;;) {
		if (*pat == '/' || *pat == '\0')
			return *str == '\0' ? pat : NULL;
		if (*pat != *str)
			return NULL;
		++pat;
		++str;
	}
}

int
handleosc(const unsigned char *buf, size_t len)
{
	const char *addr, *next;
	const struct oscnode *path[8], *node;
	size_t pathlen;
	struct oscmsg msg;
	int reg;

	if (len % 4 != 0)
		return -1;
	msg.err = NULL;
	msg.buf = (unsigned char *)buf;
	msg.end = (unsigned char *)buf + len;
	msg.type = "ss";

	addr = oscgetstr(&msg);
	msg.type = oscgetstr(&msg);
	if (msg.err) {
		fprintf(stderr, "invalid osc message: %s\n", msg.err);
		return -1;
	}
	++msg.type;

	reg = 0;
	pathlen = 0;
	for (node = tree; node->name;) {
		next = match(addr + 1, node->name);
		if (next) {
			assert(pathlen < LEN(path));
			path[pathlen++] = node;
			reg += node->reg;
			if (*next) {
				node = node->child;
				addr = next;
			} else {
				if (node->set) {
					node->set(path + pathlen - 1, reg, &msg);
					if (msg.err)
						fprintf(stderr, "%s: %s\n", addr, msg.err);
				}
				break;
			}
		} else {
			++node;
		}
	}
	return 0;
}

static unsigned char oscbuf[8192];
static struct oscmsg oscmsg;

static void
oscsend(const char *addr, const char *type, ...)
{
	unsigned char *len;
	va_list ap;

	_Static_assert(sizeof(float) == sizeof(uint32_t), "unsupported float type");
	assert(addr[0] == '/');
	assert(type[0] == ',');

	if (!oscmsg.buf) {
		oscmsg.buf = oscbuf;
		oscmsg.end = oscbuf + sizeof oscbuf;
		oscmsg.type = NULL;
		oscputstr(&oscmsg, "#bundle");
		oscputint(&oscmsg, 0);
		oscputint(&oscmsg, 1);
	}

	len = oscmsg.buf;
	oscmsg.type = NULL;
	oscputint(&oscmsg, 0);
	oscputstr(&oscmsg, addr);
	oscputstr(&oscmsg, type);
	oscmsg.type = ++type;
	va_start(ap, type);
	for (; *type; ++type) {
		switch (*type) {
		case 'f': oscputfloat(&oscmsg, va_arg(ap, double)); break;
		case 'i': oscputint(&oscmsg, va_arg(ap, uint_least32_t)); break;
		case 's': oscputstr(&oscmsg, va_arg(ap, const char *)); break;
		default: assert(0);
		}
	}
	va_end(ap);
	putbe32(len, oscmsg.buf - len - 4);
}

static void
oscsendenum(const char *addr, int val, const char *const names[], size_t nameslen)
{
	if (val >= 0 && val < nameslen) {
		oscsend(addr, ",is", val, names[val]);
	} else {
		fprintf(stderr, "unexpected enum value %d\n", val);
		oscsend(addr, ",i", val);
	}
}

static void
oscflush(void)
{
	if (oscmsg.buf) {
		writeosc(oscbuf, oscmsg.buf - oscbuf);
		oscmsg.buf = NULL;
	}
}

static void
handleregs(uint_least32_t *payload, size_t len)
{
	size_t i;
	int reg, val, off;
	const struct oscnode *node;
	char addr[256], *addrend;
	const struct oscnode *path[8];
	size_t pathlen;

	for (i = 0; i < len; ++i) {
		reg = payload[i] >> 16 & 0x7fff;
		val = (long)((payload[i] & 0xffff) ^ 0x8000) - 0x8000;
		addrend = addr;
		off = 0;
		node = tree;
		pathlen = 0;
		while (node->name) {
			if (reg >= off + node[1].reg && node[1].name && node[1].reg != -1) {
				++node;
				continue;
			}
			*addrend++ = '/';
			addrend = memccpy(addrend, node->name, '\0', addr + sizeof addr - addrend);
			assert(addrend);
			--addrend;
			assert(pathlen < LEN(path));
			path[pathlen++] = node;
			if (reg == off + node->reg && node->new) {
				node->new(path + pathlen - 1, addr, reg, val);
			} else if (node->child) {
				off += node->reg;
				node = node->child;
				continue;
			} else if (dflag && reg != off + node->reg) {
				switch (reg) {
				case 0x3180:
				case 0x3380:
					break;
				default:
					fprintf(stderr, "[%.4x]=%.4hx (%.4x %s)\n", reg, (short)val, off + node->reg, addr);
					break;
				}
			}
			break;
		}
	}
}

static void
handlelevels(int subid, uint_least32_t *payload, size_t len)
{
	static uint_least32_t inputpeakfx[22], outputpeakfx[22];
	static uint_least64_t inputrmsfx[22], outputrmsfx[22];
	uint_least32_t peak, *peakfx;
	uint_least64_t rms, *rmsfx;
	float peakdb, peakfxdb, rmsdb, rmsfxdb;
	const char *type;
	char addr[128];
	size_t i;

	if (len % 3 != 0) {
		fprintf(stderr, "unexpected levels data\n");
		return;
	}
	len /= 3;
	type = NULL;
	peakfx = NULL;
	rmsfx = NULL;
	switch (subid) {
	case 4: type = "input";  /* fallthrough */
	case 1: peakfx = inputpeakfx, rmsfx = inputrmsfx; break;
	case 5: type = "output";  /* fallthrough */
	case 3: peakfx = outputpeakfx, rmsfx = outputrmsfx; break;
	case 2: type = "playback"; break;
	default: assert(0);
	}
	for (i = 0; i < len; ++i) {
		rms = *payload++;
		rms |= (uint_least64_t)*payload++ << 32;
		peak = *payload++;
		if (type) {
			peakdb = 20 * log10((peak >> 4) / 0x1p23);
			rmsdb = 10 * log10(rms / 0x1p54);
			snprintf(addr, sizeof addr, "/%s/%d/level", type, (int)i + 1);
			if (peakfx) {
				peakfxdb = 20 * log10((peakfx[i] >> 4) / 0x1p23);
				rmsfxdb = 10 * log10(rmsfx[i] / 0x1p54);
				oscsend(addr, ",ffffi", peakdb, rmsdb, peakfxdb, rmsfxdb, peak & peakfx[i] & 1);
			} else {
				oscsend(addr, ",ffi", peakdb, rmsdb, peak & 1);
			}
		} else {
			*peakfx++ = peak;
			*rmsfx++ = rms;
		}
	}
}

void
handlesysex(const unsigned char *buf, size_t len, uint_least32_t *payload)
{
	struct sysex sysex;
	int ret;
	size_t i;
	uint_least32_t *pos;

	ret = sysexdec(&sysex, buf, len, SYSEX_MFRID | SYSEX_DEVID | SYSEX_SUBID);
	if (ret != 0 || sysex.mfrid != 0x200d || sysex.devid != 0x10 || sysex.datalen % 5 != 0) {
		if (ret == 0)
			fprintf(stderr, "ignoring unknown sysex packet (mfr=%x devid=%x datalen=%zu)\n", sysex.mfrid, sysex.devid, sysex.datalen);
		else
			fprintf(stderr, "ignoring unknown sysex packet\n");
		return;
	}
	pos = payload;
	for (i = 0; i < sysex.datalen; i += 5)
		*pos++ = getle32_7bit(sysex.data + i);
	switch (sysex.subid) {
	case 0:
		handleregs(payload, pos - payload);
		fflush(stdout);
		fflush(stderr);
		break;
	case 1: case 2: case 3: case 4: case 5:
		handlelevels(sysex.subid, payload, pos - payload);
		break;
	default:
		fprintf(stderr, "ignoring unknown sysex sub ID\n");
	}
	oscflush();
}

void
handletimer(bool levels)
{
	static int serial;
	unsigned char buf[7];

	if (levels && !refreshing) {
		/* XXX: ~60 times per second levels, ~30 times per second serial */
		writesysex(2, NULL, 0, buf);
	}

	setreg(0x3f00, serial);
	serial = (serial + 1) & 0xf;
}

int
init(const char *port)
{
	extern const struct device ffucxii;
	static const struct device *devices[] = {
		&ffucxii,
	};
	int i;
	size_t namelen;

	for (i = 0; i < LEN(devices); ++i) {
		device = devices[i];
		if (strcmp(port, device->id) == 0)
			break;
		namelen = strlen(device->name);
		if (strncmp(port, device->name, namelen) == 0) {
			if (!port[namelen] || (port[namelen] == ' ' && port[namelen + 1] == '('))
				break;
		}
	}
	if (i == LEN(devices)) {
		fprintf(stderr, "unsupported device '%s'\n", port);
		return -1;
	}

	inputs = calloc(device->inputslen + device->outputslen, sizeof *inputs);
	outputs = calloc(device->outputslen, sizeof *outputs);
	if (!inputs || !outputs) {
		perror(NULL);
		return -1;
	}
	for (i = 0; i < device->outputslen; ++i) {
		struct output *out;

		inputs[device->inputslen + i].stereo = true;
		out = &outputs[i];
		out->mix = calloc(device->inputslen + device->outputslen, sizeof *out->mix);
		if (!out->mix) {
			perror(NULL);
			return -1;
		}
	}
	return 0;
}
