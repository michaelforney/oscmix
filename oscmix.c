#define _XOPEN_SOURCE 700  /* for memccpy */
#include <assert.h>
#include <limits.h>
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

struct oscctx {
	const struct oscnode *node;
	const char *addr;
	unsigned char ctl[4];
	int depth;
};

struct oscnode {
	const char *name;
	void (*set)(struct oscctx *ctx, struct oscmsg *msg);
	void (*new)(struct oscctx *ctx, int val);
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

struct mix {
	signed char pan;
	short vol;
};

struct input {
	bool stereo;
	bool mute;
	float width;
};

struct output {
	bool stereo;
	struct mix *mix;
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
static struct input *playbacks;
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

	if (dflag && reg != 0x3f00)
		fprintf(stderr, "setreg %.4X %.4hX\n", reg, (unsigned short)val);
	regval = (reg & 0x7fff) << 16 | (val & 0xffff);
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

static void
setval(struct oscctx *ctx, int val)
{
	int reg;

	reg = device->ctltoreg(getle32(ctx->ctl));
	if (reg != -1)
		setreg(reg, val);
}

static void
setint(struct oscctx *ctx, struct oscmsg *msg)
{
	int_least32_t val;

	val = oscgetint(msg);
	if (oscend(msg) != 0)
		return;
	setval(ctx, val);
}

static void
newint(struct oscctx *ctx, int val)
{
	oscsend(ctx->addr, ",i", val);
}

static void
setfixed(struct oscctx *ctx, struct oscmsg *msg)
{
	float val;

	val = oscgetfloat(msg);
	if (oscend(msg) != 0)
		return;
	setval(ctx, val / ctx->node->scale);
}

static void
newfixed(struct oscctx *ctx, int val)
{
	oscsend(ctx->addr, ",f", val * ctx->node->scale);
}

static void
setenum(struct oscctx *ctx, struct oscmsg *msg)
{
	const char *str;
	int val;

	switch (*msg->type) {
	case 's':
		str = oscgetstr(msg);
		if (str) {
			for (val = 0; val < ctx->node->nameslen; ++val) {
				if (strcasecmp(str, ctx->node->names[val]) == 0)
					break;
			}
			if (val == ctx->node->nameslen)
				return;
		}
		break;
	default:
		val = oscgetint(msg);
		break;
	}
	if (oscend(msg) != 0)
		return;
	setval(ctx, val);
}

static void
newenum(struct oscctx *ctx, int val)
{
	oscsendenum(ctx->addr, val, ctx->node->names, ctx->node->nameslen);
}

static void
setbool(struct oscctx *ctx, struct oscmsg *msg)
{
	bool val;

	fprintf(stderr, "setbool2\n");
	val = oscgetint(msg);
	if (oscend(msg) != 0)
		return;
	setval(ctx, val);
}

static void
newbool(struct oscctx *ctx, int val)
{
	oscsend(ctx->addr, ",i", val != 0);
}

static int
setlevel(int reg, float level)
{
	long val;

	val = level * 0x8000l;
	assert(val >= 0);
	assert(val <= 0x10000);
	if (val > 0x4000)
		val = (val >> 3) - 0x8000;
	return setreg(reg, val);
}

static void
setlevels(struct output *out, struct input *in, struct mix *mix)
{
	int reg;
	float level, theta;

	reg = 0x4000 | (out - outputs) << 6 | (in - inputs);
	level = in->mute ? 0 : mix->vol <= -650 ? 0 : powf(10, mix->vol / 200.f);
	if (out->stereo) {
		theta = (mix->pan + 100) / 400.f * PI;
		setlevel(reg, level * cosf(theta));
		setlevel(reg + 0x40, level * sinf(theta));
	} else {
		setlevel(reg, level);
	}
}

static const char *
match(const char *pat, const char *str)
{
	assert(*pat == '/');
	++pat;
	for (;;) {
		if (*pat == '/' || *pat == '\0')
			return *str == '\0' ? pat : NULL;
		if (*pat != *str)
			return NULL;
		++pat;
		++str;
	}
}

static void
setinput(struct oscctx *ctx, struct oscmsg *msg)
{
	const char *next;
	long ch;
	char buf[(sizeof ch * CHAR_BIT + 2) / 3];

	if (!ctx->addr[0])
		return;
	assert(ctx->addr[0] == '/');
	ch = strtol(ctx->addr + 1, (char **)&next, 10);
	if (next == ctx->addr + 1 || (*next != '\0' && *next != '/')) {
		for (ch = 1; ch <= device->inputslen; ++ch) {
			snprintf(buf, sizeof buf, "%ld", ch);
			next = match(ctx->addr, buf);
			if (next)
				break;
		}
	}
	if (ch < 1 || ch > device->inputslen)
		return;
	ctx->addr = next;
	fprintf(stderr, "input ch=%ld\n", ch);
	ctx->ctl[0] = INPUT;
	ctx->ctl[1] = ch - 1;
	ctx->depth = 2;
}

static void
setoutput(struct oscctx *ctx, struct oscmsg *msg)
{
	const char *next;
	long ch;
	char buf[(sizeof ch * CHAR_BIT + 2) / 3];

	if (!ctx->addr[0])
		return;
	assert(ctx->addr[0] == '/');
	ch = strtol(ctx->addr + 1, (char **)&next, 10);
	if (next == ctx->addr + 1 || (*next != '\0' && *next != '/')) {
		for (ch = 1; ch <= device->inputslen; ++ch) {
			snprintf(buf, sizeof buf, "%ld", ch);
			next = match(ctx->addr, buf);
			if (next)
				break;
		}
		if (ch > device->inputslen)
			return;
	}
	ctx->addr = next;
	fprintf(stderr, "output ch=%ld\n", ch);
	ctx->ctl[0] = OUTPUT;
	ctx->ctl[1] = ch -1;
	ctx->depth = 2;
}

static void
setinputmute(struct oscctx *ctx, struct oscmsg *msg)
{
	struct input *in;
	struct output *out;
	struct mix *mix;
	int inidx, outidx;
	bool val;

	val = oscgetint(msg);
	if (oscend(msg) != 0)
		return;
	inidx = ctx->ctl[1];
	assert(inidx < device->inputslen);
	/* mutex */
	in = &inputs[inidx];
	if (inidx % 2 == 1 && in[-1].stereo)
		--in, --inidx;
	setval(ctx, val);
	if (in->mute != val) {
		in->mute = val;
		if (in->stereo)
			in[1].mute = val;
		for (outidx = 0; outidx < device->outputslen; ++outidx) {
			out = &outputs[outidx];
			mix = &out->mix[inidx];
			if (mix->vol > -650)
				setlevels(out, in, mix);
			if (in->stereo && (++mix)->vol > -650)
				setlevels(out, in + 1, mix);
			if (out->stereo) {
				assert(outidx % 2 == 0);
				++outidx;
			}
		}
	}
}

static void
setinputstereo(struct oscctx *ctx, struct oscmsg *msg)
{
	bool val;

	val = oscgetint(msg);
	if (oscend(msg) != 0)
		return;
	inputs[ctx->ctl[1]].stereo = val;
	setval(ctx, val);
	ctx->ctl[1] ^= 1;
	inputs[ctx->ctl[1]].stereo = val;
	setval(ctx, val);
}

static void
newinputstereo(struct oscctx *ctx, int val)
{
	int idx;
	char addr[256];

	idx = ctx->ctl[1];
	assert(idx < device->inputslen);
	inputs[idx].stereo = val;
	inputs[idx + 1].stereo = val;
	snprintf(addr, sizeof addr, "/input/%d/stereo", idx + 1);
	oscsend(addr, ",i", val != 0);
	snprintf(addr, sizeof addr, "/input/%d/stereo", idx + 2);
	oscsend(addr, ",i", val != 0);
}

static void
newoutputstereo(struct oscctx *ctx, int val)
{
	int idx;
	char addr[256];

	idx = ctx->ctl[1];
	assert(idx < device->outputslen);
	outputs[idx].stereo = val;
	outputs[idx + 1].stereo = val;
	snprintf(addr, sizeof addr, "/output/%d/stereo", idx + 1);
	oscsend(addr, ",i", val != 0);
	snprintf(addr, sizeof addr, "/output/%d/stereo", idx + 2);
	oscsend(addr, ",i", val != 0);
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

static void
setinputgain(struct oscctx *ctx, struct oscmsg *msg)
{
	const struct inputinfo *info;
	float val;

	val = oscgetfloat(msg);
	if (oscend(msg) != 0)
		return;
	info = &device->inputs[ctx->ctl[1]];
	if (info->flags & INPUT_HAS_GAIN) {
		if (val < info->gain.min)
			val = info->gain.min;
		if (val > info->gain.max)
			val = info->gain.max;
		setval(ctx, val * 10);
	}
}

static void
newinputgain(struct oscctx *ctx, int val)
{
	oscsend(ctx->addr, ",f", val / 10.0);
}

static void
setinput48v(struct oscctx *ctx, struct oscmsg *msg)
{
	if (device->inputs[ctx->ctl[1]].flags & INPUT_HAS_48V)
		setbool(ctx, msg);
}

static void
newinput48v_reflevel(struct oscctx *ctx, int val)
{
	int idx;
	const struct inputinfo *info;

	idx = ctx->ctl[1];
	assert(idx < device->inputslen);
	info = &device->inputs[idx];
	if (info->flags & INPUT_HAS_48V) {
		char addrbuf[256];

		snprintf(addrbuf, sizeof addrbuf, "/input/%d/48v", idx + 1);
		ctx->addr = addrbuf;
		newbool(ctx, val);
	} else if (info->flags & INPUT_HAS_REFLEVEL) {
		oscsendenum(ctx->addr, val & 0xf, info->reflevel.names, info->reflevel.nameslen);
	}
}

static void
setinputhiz(struct oscctx *ctx, struct oscmsg *msg)
{
	if (device->inputs[ctx->ctl[1]].flags & INPUT_HAS_HIZ)
		setbool(ctx, msg);
}

static void
newinputhiz(struct oscctx *ctx, int val)
{
	int idx;
	
	idx = ctx->ctl[1];
	assert(idx < device->inputslen);
	if (device->inputs[idx].flags & INPUT_HAS_HIZ)
		newbool(ctx, val);
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

static void
seteqdrecord(struct oscctx *ctx, struct oscmsg *msg)
{
	bool val;
	unsigned char buf[4], sysexbuf[7 + 5];

	val = oscgetint(msg);
	if (oscend(msg) != 0)
		return;
	putle32(buf, val);
	writesysex(4, buf, sizeof buf, sysexbuf);
}

static void
newdspload(struct oscctx *ctx, int val)
{
	if (dsp.load != (val & 0xff)) {
		dsp.load = val & 0xff;
		oscsend("/hardware/dspload", ",i", dsp.load);
	}
	if (dsp.vers != val >> 8) {
		dsp.vers = val >> 8;
		oscsend("/hardware/dspvers", ",i", dsp.vers);
	}
}

static void
newdspavail(struct oscctx *ctx, int val)
{
}

static void
newdspstatus(struct oscctx *ctx, int val)
{
}

static void
newarcdelta(struct oscctx *ctx, int val)
{
}

static int
setdb(int reg, float db)
{
	int val;

	val = (isinf(db) && db < 0 ? -650 : (int)(db * 10)) & 0x7fff;
	return setreg(reg, val);
}

static int
setpan(int reg, int pan)
{
	int val;

	val = (pan & 0x7fff) | 0x8000;
	return setreg(reg, val);
}

static int
setmix(const struct oscnode *path[], int reg, struct oscmsg *msg)
{
	int outidx, inidx, pan;
	float vol, level, theta, width;
	struct output *out;
	struct input *in;

	outidx = path[-2] - path[-3]->child;
	assert(outidx < device->outputslen);
	out = &outputs[outidx];

	inidx = path[0] - path[-1]->child;
	if (reg & 0x20) {
		assert(inidx < device->outputslen);
		in = &playbacks[inidx];
	} else {
		assert(inidx < device->inputslen);
		in = &inputs[inidx];
	}

	vol = oscgetfloat(msg);
	if (vol <= -65)
		vol = -INFINITY;
	printf("setmix %d %d %f\n", (reg >> 6) & 0x3f, reg & 0x3f, vol);

	pan = 0;
	width = 1;
	if (*msg->type) {
		pan = oscgetint(msg);
		if (*msg->type && in->stereo && out->stereo)
			width = oscgetfloat(msg);
	}
	if (oscend(msg) != 0)
		return -1;

	level = pow(10, vol / 20);
	if (in->stereo) {
		float level0, level1, level00, level10, level01, level11;

		level0 = (100 - (pan > 0 ? pan : 0)) / 200.f * level;
		level1 = (100 + (pan < 0 ? pan : 0)) / 200.f * level;
		if (out->stereo) {
			level00 = level0 * (1 + width);
			level10 = level0 * (1 - width);
			level01 = level1 * (1 - width);
			level11 = level1 * (1 + width);
			setlevel(reg + 0x2000, level00);
			setlevel(reg + 0x2001, level10);
			setlevel(reg + 0x2040, level01);
			setlevel(reg + 0x2041, level11);

			level00 = level00 * level00;
			level0 = level00 + level01 * level01;
			/*
			L0 = level0^2 * (1 + width)^2 + level1^2 * (1 - width)^2
			L1 = level0^2 * (1 - width)^2 + level1^2 * (1 + width)^2
			*/
			setdb(reg, 10 * log10(level0));
			setpan(reg, acos(2 * level00 / level0 - 1) * 200 / PI - 100);

			level10 = level10 * level10;
			level1 = level10 + level11 * level11;
			setdb(reg + 1, 10 * log10(level1));
			setpan(reg + 1, acos(2 * level10 / level1 - 1) * 200 / PI - 100);
		} else {
			setlevel(reg + 0x2000, level0);
			setlevel(reg + 0x2001, level1);
			setdb(reg, 20 * log10(level0));
			setpan(reg, 0);
			setdb(reg + 1, 20 * log10(level1));
			setpan(reg + 1, 0);
		}
	} else {
		if (out->stereo) {
			theta = (pan + 100) * PI / 400;
			setlevel(reg + 0x2000, level * cos(theta));
			setlevel(reg + 0x2040, level * sin(theta));
		} else {
			setlevel(reg + 0x2000, level);
		}
		setdb(reg, vol);
		setpan(reg, pan);
	}
	return 0;
}

static int
newmix(const struct oscnode *path[], const char *addr, int reg, int val)
{
	struct output *out;
	struct input *in;
	struct mix *mix;
	int outidx, inidx;
	bool newpan;
	char addrbuf[256];
	float vol;
	int pan;

	outidx = (reg & 0xfff) >> 6;
	inidx = reg & 0x3f;
	if (outidx >= device->outputslen || inidx >= device->inputslen)
		return -1;
	out = &outputs[outidx];
	in = &inputs[inidx];
	mix = &out->mix[inidx];
	newpan = val & 0x8000;
	val = ((val & 0x7fff) ^ 0x4000) - 0x4000;
	if (newpan)
		mix->pan = val;
	else
		mix->vol = val;
	if (outidx & 1 && out[-1].stereo)
		--out, --outidx;
	if (inidx & 1 && in[-1].stereo)
		--in, --inidx;
	mix = &out->mix[inidx];
	if (in->stereo) {
		float level0, level1, scale;

		level0 = mix[0].vol <= -650 ? 0 : powf(10, mix[0].vol / 200.f);
		level1 = mix[1].vol <= -650 ? 0 : powf(10, mix[1].vol / 200.f);
		if (out->stereo) {
			//scale = sqrtf(2.f / (1 + in->width * in->width));
			scale = 1;
		} else {
			scale = 2;
		}
		level0 *= scale;
		level1 *= scale;
		if (level0 == 0 && level1 == 0) {
			vol = -INFINITY;
			pan = 0;
		} else if (level0 >= level1) {
			vol = 20 * log10f(level0);
			pan = 100 * (level1 / level0 - 1);
		} else {
			vol = 20 * log10f(level1);
			pan = -100 * (level0 / level1 - 1);
		}
	} else {
		vol = mix->vol <= -650 ? -65.f : mix->vol / 10.f;
		pan = mix->pan;
	}
	snprintf(addrbuf, sizeof addrbuf, "/mix/%d/input/%d", outidx + 1, inidx + 1);
	oscsend(addrbuf, ",f", vol);
	snprintf(addrbuf, sizeof addrbuf, "/mix/%d/input/%d/pan", outidx + 1, inidx + 1);
	oscsend(addrbuf, ",i", pan);
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

static void
newsamplerate(struct oscctx *ctx, int val)
{
	uint_least32_t rate;

	rate = getsamplerate(val);
	if (rate != 0)
		oscsend(ctx->addr, ",i", rate);
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

static void
newdurecstatus(struct oscctx *ctx, int val)
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
}

static void
newdurectime(struct oscctx *ctx, int val)
{
	if (val != durec.time) {
		durec.time = val;
		oscsend(ctx->addr, ",i", val);
	}
}

static void
newdurecusbstatus(struct oscctx *ctx, int val)
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
}

static void
newdurectotalspace(struct oscctx *ctx, int val)
{
	float totalspace;

	totalspace = val / 16.f;
	if (totalspace != durec.totalspace) {
		durec.totalspace = totalspace;
		oscsend(ctx->addr, ",f", totalspace);
	}
}

static void
newdurecfreespace(struct oscctx *ctx, int val)
{
	float freespace;

	freespace = val / 16.f;
	if (freespace != durec.freespace) {
		durec.freespace = freespace;
		oscsend(ctx->addr, ",f", freespace);
	}
}

static void
resizedurecfiles(size_t len)
{
	if (len < 0 || len == durec.fileslen)
		return;
	durec.files = realloc(durec.files, len * sizeof *durec.files);
	if (!durec.files)
		fatal(NULL);  /* XXX: probably shouldn't exit */
	if (len > durec.fileslen)
		memset(durec.files + durec.fileslen, 0, (len - durec.fileslen) * sizeof *durec.files);
	durec.fileslen = len;
	if (durec.index >= durec.fileslen)
		durec.index = -1;
	oscsend("/durec/numfiles", ",i", len);
}

static void
newdurecfileslen(struct oscctx *ctx, int val)
{
	resizedurecfiles(val);
}

static void
setdurecfile(struct oscctx *ctx, struct oscmsg *msg)
{
	int val;

	val = oscgetint(msg);
	if (oscend(msg) != 0)
		return;
	setval(ctx, val | 0x8000);
}

static void
newdurecfile(struct oscctx *ctx, int val)
{
	if (val != durec.file) {
		durec.file = val;
		oscsend(ctx->addr, ",i", val);
	}
}

static void
newdurecnext(struct oscctx *ctx, int val)
{
	static const char *const names[] = {
		"Single", "UFX Single", "Continuous", "Single Next", "Repeat Single", "Repeat All",
	};
	int next, playmode;

	next = ((val & 0xfff) ^ 0x800) - 0x800;
	if (next != durec.next) {
		durec.next = next;
		oscsend(ctx->addr, ",i", ((val & 0xfff) ^ 0x800) - 0x800);
	}
	playmode = val >> 12;
	if (playmode != durec.playmode) {
		durec.playmode = playmode;
		oscsendenum("/durec/playmode", val >> 12, names, LEN(names));
	}
}

static void
newdurecrecordtime(struct oscctx *ctx, int val)
{
	if (val != durec.recordtime) {
		durec.recordtime = val;
		oscsend(ctx->addr, ",i", val);
	}
}

static void
newdurecindex(struct oscctx *ctx, int val)
{
	if (val + 1 > durec.fileslen)
		resizedurecfiles(val + 1);
	durec.index = val;
}

static void
newdurecname(struct oscctx *ctx, int val)
{
	struct durecfile *f;
	char *pos, old[2];
	int off;

	if (durec.index == -1)
		return;
	assert(durec.index < durec.fileslen);
	f = &durec.files[durec.index];
	off = (ctx->ctl[1] - DUREC_NAME0) * 2;
	assert(off >= 0 && off < sizeof f->name);
	pos = f->name + off * 2;
	memcpy(old, pos, sizeof old);
	putle16(pos, val);
	if (memcmp(old, pos, sizeof old) != 0)
		oscsend("/durec/name", ",is", durec.index, f->name);
}

static void
newdurecinfo(struct oscctx *ctx, int val)
{
	struct durecfile *f;
	unsigned long samplerate;
	int channels;

	if (durec.index == -1)
		return;
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
}

static void
newdureclength(struct oscctx *ctx, int val)
{
	struct durecfile *f;

	if (durec.index == -1)
		return;
	f = &durec.files[durec.index];
	if (val != f->length) {
		f->length = val;
		oscsend("/durec/length", ",ii", durec.index, val);
	}
}

static void
setdurecstop(struct oscctx *ctx, struct oscmsg *msg)
{
	if (oscend(msg) != 0)
		return;
	setval(ctx, 0x8120);
}

static void
setdurecplay(struct oscctx *ctx, struct oscmsg *msg)
{
	if (oscend(msg) != 0)
		return;
	setval(ctx, 0x8123);
}

static void
setdurecrecord(struct oscctx *ctx, struct oscmsg *msg)
{
	if (oscend(msg) != 0)
		return;
	setval(ctx, 0x8122);
}

static void
setdurecdelete(struct oscctx *ctx, struct oscmsg *msg)
{
	int val;

	val = oscgetint(msg);
	if (oscend(msg) != 0)
		return;
	setval(ctx, 0x8000 | val);
}

static void
setrefresh(struct oscctx *ctx, struct oscmsg *msg)
{
	struct input *pb;
	char addr[256];
	int i;

	dsp.vers = -1;
	dsp.load = -1;
	setval(ctx, device->refresh);
	refreshing = true;
	/* FIXME: needs lock */
	for (i = 0; i < device->outputslen; ++i) {
		pb = &playbacks[i];
		snprintf(addr, sizeof addr, "/playback/%d/stereo", i + 1);
		oscsend(addr, ",i", pb->stereo);
	}
	oscflush();
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
	[LOWCUT_FREQ]={"freq",  .set=setint, .new=newint, .min=20, .max=500},
	[LOWCUT_SLOPE]={"slope", .set=setint, .new=newint},
	{0},
};

static const struct oscnode eqtree[] = {
	[EQ_BAND1TYPE]={"band1type", .set=setenum, .new=newenum, .names=(const char *const[]){
		"Peak", "Low Shelf", "High Pass", "Low Pass",
	}, .nameslen=4},
	[EQ_BAND1GAIN]={"band1gain", .set=setfixed, .new=newfixed, .scale=0.1, .min=-200, .max=200},
	[EQ_BAND1FREQ]={"band1freq", .set=setint, .new=newint, .min=20, .max=20000},
	[EQ_BAND1Q]={"band1q", .set=setfixed, .new=newfixed, .scale=0.1, .min=4, .max=99},
	[EQ_BAND2GAIN]={"band2gain", .set=setfixed, .new=newfixed, .scale=0.1, .min=-200, .max=200},
	[EQ_BAND2FREQ]={"band2freq", .set=setint, .new=newint, .min=20, .max=20000},
	[EQ_BAND2Q]={"band2q", .set=setfixed, .new=newfixed, .scale=0.1, .min=4, .max=99},
	[EQ_BAND3TYPE]={"band3type", .set=setenum, .new=newenum, .names=(const char *const[]){
		"Peak", "High Shelf", "Low Pass", "High Pass",
	}, .nameslen=3},
	[EQ_BAND3GAIN]={"band3gain", .set=setfixed, .new=newfixed, .scale=0.1, .min=-200, .max=200},
	[EQ_BAND3FREQ]={"band3freq", .set=setint, .new=newint, .min=20, .max=20000},
	[EQ_BAND3Q]={"band3q", .set=setfixed, .new=newfixed, .scale=0.1, .min=4, .max=99},
	{0},
};

static const struct oscnode dynamicstree[] = {
	[DYNAMICS_GAIN]={"gain", .set=setfixed, .new=newfixed, .scale=0.1, .min=-300, .max=300},
	[DYNAMICS_ATTACK]={"attack", .set=setint, .new=newint, .min=0, .max=200},
	[DYNAMICS_RELEASE]={"release", .set=setint, .new=newint, .min=100, .max=999},
	[DYNAMICS_COMPTHRES]={"compthres", .set=setfixed, .new=newfixed, .scale=0.1, .min=-600, .max=0},
	[DYNAMICS_COMPRATIO]={"compratio", .set=setfixed, .new=newfixed, .scale=0.1, .min=10, .max=100},
	[DYNAMICS_EXPTHRES]={"expthres", .set=setfixed, .new=newfixed, .scale=0.1, .min=-990, .max=200},
	[DYNAMICS_EXPRATIO]={"expratio", .set=setfixed, .new=newfixed, .scale=0.1, .min=10, .max=100},
	{0},
};

static const struct oscnode autoleveltree[] = {
	[AUTOLEVEL_MAXGAIN]={"maxgain", .set=setfixed, .new=newfixed, .scale=0.1, .min=0, .max=180},
	[AUTOLEVEL_HEADROOM]={"headroom", .set=setfixed, .new=newfixed, .scale=0.1, .min=30, .max=120},
	[AUTOLEVEL_RISETIME]={"risetime", .set=setint, .new=newint, .min=100, .max=9900},
	{0},
};

static const struct oscnode roomeqtree[] = {
	[ROOMEQ_DELAY]    ={"delay", .set=setfixed, .new=newfixed, .min=0, .max=425, .scale=0.001},
	[ROOMEQ_ENABLED]  ={"enabled", .set=setbool, .new=newbool},
	[ROOMEQ_BAND1TYPE]={"band1type", .set=setenum, .new=newenum, .names=(const char *const[]){
		"Peak", "Low Shelf", "High Pass", "Low Pass",
	}, .nameslen=4},
	[ROOMEQ_BAND1GAIN]={"band1gain", .set=setfixed, .new=newfixed, .min=-200, .max=200, .scale=0.1},
	[ROOMEQ_BAND1FREQ]={"band1freq", .set=setint, .new=newint, .min=20, .max=20000},
	[ROOMEQ_BAND1Q]   ={"band1q", .set=setfixed, .new=newfixed, .min=4, .max=99, .scale=0.1},
	[ROOMEQ_BAND2GAIN]={"band2gain", .set=setfixed, .new=newfixed, .min=-200, .max=200, .scale=0.1},
	[ROOMEQ_BAND2FREQ]={"band2freq", .set=setint, .new=newint, .min=20, .max=20000},
	[ROOMEQ_BAND2Q]   ={"band2q", .set=setfixed, .new=newfixed, .min=4, .max=99, .scale=0.1},
	[ROOMEQ_BAND3GAIN]={"band3gain", .set=setfixed, .new=newfixed, .min=-200, .max=200, .scale=0.1},
	[ROOMEQ_BAND3FREQ]={"band3freq", .set=setint, .new=newint, .min=20, .max=20000},
	[ROOMEQ_BAND3Q]   ={"band3q", .set=setfixed, .new=newfixed, .min=4, .max=99, .scale=0.1},
	[ROOMEQ_BAND4GAIN]={"band4gain", .set=setfixed, .new=newfixed, .min=-200, .max=200, .scale=0.1},
	[ROOMEQ_BAND4FREQ]={"band4freq", .set=setint, .new=newint, .min=20, .max=20000},
	[ROOMEQ_BAND4Q]   ={"band4q", .set=setfixed, .new=newfixed, .min=4, .max=99, .scale=0.1},
	[ROOMEQ_BAND5GAIN]={"band5gain", .set=setfixed, .new=newfixed, .min=-200, .max=200, .scale=0.1},
	[ROOMEQ_BAND5FREQ]={"band5freq", .set=setint, .new=newint, .min=20, .max=20000},
	[ROOMEQ_BAND5Q]   ={"band5q", .set=setfixed, .new=newfixed, .min=4, .max=99, .scale=0.1},
	[ROOMEQ_BAND6GAIN]={"band6gain", .set=setfixed, .new=newfixed, .min=-200, .max=200, .scale=0.1},
	[ROOMEQ_BAND6FREQ]={"band6freq", .set=setint, .new=newint, .min=20, .max=20000},
	[ROOMEQ_BAND6Q]   ={"band6q", .set=setfixed, .new=newfixed, .min=4, .max=99, .scale=0.1},
	[ROOMEQ_BAND7GAIN]={"band7gain", .set=setfixed, .new=newfixed, .min=-200, .max=200, .scale=0.1},
	[ROOMEQ_BAND7FREQ]={"band7freq", .set=setint, .new=newint, .min=20, .max=20000},
	[ROOMEQ_BAND7Q]   ={"band7q", .set=setfixed, .new=newfixed, .min=4, .max=99, .scale=0.1},
	[ROOMEQ_BAND8TYPE]={"band8type", .set=setenum, .new=newenum, .names=(const char *const[]){
		"Peak", "High Shelf", "Low Pass", "High Pass",
	}, .nameslen=4},
	[ROOMEQ_BAND8GAIN]={"band8gain", .set=setfixed, .new=newfixed, .min=-200, .max=200, .scale=0.1},
	[ROOMEQ_BAND8FREQ]={"band8freq", .set=setint, .new=newint, .min=20, .max=20000},
	[ROOMEQ_BAND8Q]   ={"band8q", .set=setfixed, .new=newfixed, .min=4, .max=99, .scale=0.1},
	[ROOMEQ_BAND9TYPE]={"band9type", .set=setenum, .new=newenum, .names=(const char *const[]){
		"Peak", "High Shelf", "Low Pass", "High Pass",
	}, .nameslen=4},
	[ROOMEQ_BAND9GAIN]={"band9gain", .set=setfixed, .new=newfixed, .min=-200, .max=200, .scale=0.1},
	[ROOMEQ_BAND9FREQ]={"band9freq", .set=setint, .new=newint, .min=20, .max=20000},
	[ROOMEQ_BAND9Q]   ={"band9q", .set=setfixed, .new=newfixed, .min=4, .max=99, .scale=0.1},
	{0},
};

static const struct oscnode tree[] = {
	[INPUT]={"input", .set=setinput, .child=(const struct oscnode[]){
		[INPUT_MUTE]     ={"mute",      .set=setinputmute, .new=newbool},
		[INPUT_FXSEND]   ={"fx",        .set=setfixed, .new=newfixed, .min=-650, .max=0, .scale=0.1},
		[INPUT_STEREO]   ={"stereo",    .set=setinputstereo, .new=newinputstereo},
		[INPUT_RECORD]   ={"record",    .set=setbool, .new=newbool},
		[INPUT_PLAYCHAN] ={"playchan",  .set=setint, .new=newint, .min=1, .max=60},
		[INPUT_MSPROC]   ={"msproc",    .set=setbool, .new=newbool},
		[INPUT_PHASE]    ={"phase",     .set=setbool, .new=newbool},
		[INPUT_GAIN]     ={"gain",      .set=setinputgain, .new=newinputgain},
		[INPUT_REFLEVEL_48V]={"48v",    .set=setinput48v, .new=newinput48v_reflevel},
		[INPUT_AUTOSET]  ={"autoset",   .set=setbool, .new=newbool},
		[INPUT_HIZ]      ={"hi-z",      .set=setinputhiz, .new=newinputhiz},
		[INPUT_LOWCUT]   ={"lowcut",    .set=setbool, .new=newbool, .child=lowcuttree},
		[INPUT_EQ]       ={"eq",        .set=setbool, .new=newbool, .child=eqtree},
		[INPUT_DYNAMICS] ={"dynamics",  .set=setbool, .new=newbool, .child=dynamicstree},
		[INPUT_AUTOLEVEL]={"autolevel", .set=setbool, .new=newbool, .child=autoleveltree},
		{"reflevel", .set=setint},
		{0},
	}},
	[OUTPUT]={"output", .set=setoutput, .child=(const struct oscnode[]){
		[OUTPUT_VOLUME]   ={"volume", .set=setfixed, .new=newfixed, .scale=0.1, .min=-65.0, .max=6.0},
		[OUTPUT_BALANCE]  ={"balance", .set=setint, .new=newint, .min=-100, .max=100},
		[OUTPUT_MUTE]     ={"mute", .set=setbool, .new=newbool},
		[OUTPUT_FXRETURN] ={"fx", .set=setfixed, .new=newfixed, .scale=0.1, .min=-65.0, .max=0.0},
		[OUTPUT_STEREO]   ={"stereo", .set=setbool, .new=newoutputstereo},
		[OUTPUT_RECORD]   ={"record", .set=setbool, .new=newbool},
		[OUTPUT_PLAYCHAN] ={"playchan", .set=setint, .new=newint},
		[OUTPUT_PHASE]    ={"phase", .set=setbool, .new=newbool},
		[OUTPUT_REFLEVEL] ={"reflevel", .set=setenum, .new=newenum, .names=(const char *const[]){
			"+4dBu", "+13dBu", "+19dBu",
		}, .nameslen=3}, // TODO: phones
		[OUTPUT_CROSSFEED]={"crossfeed", .set=setint, .new=newint},
		[OUTPUT_VOLUMECAL]={"volumecal", .set=setfixed, .new=newfixed, .min=-2400, .max=300, .scale=0.01},
		[OUTPUT_LOWCUT]  ={"lowcut", .set=setbool, .new=newbool, .child=lowcuttree},
		[OUTPUT_EQ]      ={"eq", .set=setbool, .new=newbool, .child=eqtree},
		[OUTPUT_DYNAMICS]={"dynamics", .set=setbool, .new=newbool, .child=dynamicstree},
		[OUTPUT_AUTOLEVEL]={"autolevel", .set=setbool, .new=newbool, .child=autoleveltree},
		[OUTPUT_ROOMEQ]  ={"roomeq", .set=setbool, .new=newbool, .child=roomeqtree},
	}},
	[REVERB]={"reverb", .set=setbool, .child=(const struct oscnode[]){
		[REVERB_TYPE]={"type", .set=setenum, .new=newenum, .names=(const char *const[]){
			"Small Room", "Medium Room", "Large Room", "Walls",
			"Shorty", "Attack", "Swagger", "Old School",
			"Echoistic", "8plus9", "Grand Wide", "Thicker",
			"Envelope", "Gated", "Space",
		}, .nameslen=15},
		[REVERB_PREDELAY] ={"predelay", .set=setint, .new=newint},
		[REVERB_LOWCUT]   ={"lowcut", .set=setint, .new=newint},
		[REVERB_ROOMSCALE]={"roomscale", .set=setfixed, .new=newfixed, .scale=0.01},
		[REVERB_ATTACK]   ={"attack", .set=setint, .new=newint},
		[REVERB_HOLD]     ={"hold", .set=setint, .new=newint},
		[REVERB_RELEASE]  ={"release", .set=setint, .new=newint},
		[REVERB_HIGHCUT]  ={"highcut", .set=setint, .new=newint},
		[REVERB_TIME]     ={"time", .set=setfixed, .new=newfixed, .scale=0.1},
		[REVERB_HIGHDAMP] ={"highdamp", .set=setint, .new=newint},
		[REVERB_SMOOTH]   ={"smooth", .set=setint, .new=newint},
		[REVERB_VOLUME]   ={"volume", .set=setfixed, .new=newfixed, .scale=0.1},
		[REVERB_WIDTH]    ={"width", .set=setfixed, .new=newfixed, .scale=0.01},
		{0},
	}},
	[ECHO]={"echo", .set=setbool, .new=newbool, .child=(const struct oscnode[]){
		[ECHO_TYPE]={"type", .set=setenum, .new=newenum, .names=(const char *const[]){
			"Stereo Echo",
			"Stereo Cross",
			"Pong Echo",
		}, .nameslen=3},
		[ECHO_DELAY]={"delay", .set=setfixed, .new=newfixed, .scale=0.001, .min=0, .max=2000},
		[ECHO_FEEDBACK]={"feedback", .set=setint, .new=newint},
		[ECHO_HIGHCUT]={"highcut", .set=setenum, .new=newenum, .names=(const char *const[]){
			"Off", "16kHz", "12kHz", "8kHz", "4kHz", "2kHz",
		}, .nameslen=6},
		[ECHO_VOLUME]={"volume", .set=setfixed, .new=newfixed, .scale=0.1, .min=-650, .max=60},
		[ECHO_WIDTH]={"width", .set=setfixed, .new=newfixed, .scale=0.01},
		{0},
	}},
	[CTLROOM]={"controlroom", .child=(const struct oscnode[]){
		[CTLROOM_MAINOUT]={"mainout", .set=setenum, .new=newenum, .names=(const char *const[]){
			"1/2", "3/4", "5/6", "7/8", "9/10",
			"11/12", "13/14", "15/16", "17/18", "19/20",
		}, .nameslen=10},
		[CTLROOM_MAINMONO]={"mainmono", .set=setbool, .new=newbool},
		[CTLROOM_MUTEENABLE]={"muteenable", .set=setbool, .new=newbool},
		[CTLROOM_DIMREDUCTION]={"dimreduction", .set=setfixed, .new=newfixed, .scale=0.1, .min=-650, .max=0},
		[CTLROOM_DIM]={"dim", .set=setbool, .new=newbool},
		[CTLROOM_RECALLVOLUME]={"recallvolume", .set=setfixed, .new=newfixed, .scale=0.1, .min=-650, .max=0},
		{0},
	}},
	[CLOCK]={"clock", .child=(const struct oscnode[]){
		[CLOCK_SOURCE]={"source", .set=setenum, .new=newenum, .names=(const char *const[]){
			"Internal", "Word Clock", "SPDIF", "AES", "Optical",
		}, .nameslen=5},
		[CLOCK_SAMPLERATE]={"samplerate", .new=newsamplerate},
		[CLOCK_WCKOUT]={"wckout", .set=setbool, .new=newbool},
		[CLOCK_WCKSINGLE]={"wcksingle", .set=setbool, .new=newbool},
		[CLOCK_WCKTERM]={"wckterm", .set=setbool, .new=newbool},
		{0},
	}},
	[HARDWARE]={"hardware", .child=(const struct oscnode[]){
		[HARDWARE_OPTICALOUT]={"opticalout", .set=setenum, .new=newenum, .names=(const char *const[]){
			"ADAT", "SPDIF",
		}, .nameslen=2},
		[HARDWARE_SPDIFOUT]={"spdifout", .set=setenum, .new=newenum, .names=(const char *const[]){
			"Consumer", "Professional",
		}, .nameslen=2},
		[HARDWARE_CCMODE]={"ccmode", .new=newbool},
		[HARDWARE_CCMIX]={"ccmix", .set=setenum, .new=newenum, .names=(const char *const[]){
			"TotalMix App", "6ch + phones", "8ch", "20ch",
		}, .nameslen=4},
		[HARDWARE_STANDALONEMIDI]={"standalonemidi", .set=setbool, .new=newbool},
		[HARDWARE_STANDALONEARC]={"standalonearc", .set=setenum, .new=newenum, .names=(const char *const[]){
			"Volume", "1s Op", "Normal",
		}, .nameslen=3},
		[HARDWARE_LOCKKEYS]={"lockkeys", .set=setenum, .new=newenum, .names=(const char *const[]){
			"Off", "Keys", "All",
		}, .nameslen=3},
		[HARDWARE_REMAPKEYS]={"remapkeys", .set=setbool, .new=newbool},
		[HARDWARE_DSPVERLOAD]={"", .new=newdspload},
		[HARDWARE_DSPAVAIL]={"", .new=newdspavail},
		[HARDWARE_DSPSTATUS]={"", .new=newdspstatus},
		[HARDWARE_ARCDELTA]={"", .new=newarcdelta},

		{"eqdrecord", .set=seteqdrecord},
		{0},
	}},
	[DUREC]={"durec", .child=(const struct oscnode[]){
		[DUREC_STATUS]={"status", .new=newdurecstatus},
		[DUREC_TIME]={"time", .new=newdurectime},
		[DUREC_USBLOAD]={"usbload", .new=newdurecusbstatus},
		[DUREC_TOTALSPACE]={"totalspace", .new=newdurectotalspace},
		[DUREC_FREESPACE]={"freespace", .new=newdurecfreespace},
		[DUREC_NUMFILES]={"numfiles", .new=newdurecfileslen},
		[DUREC_FILE]={"file", .new=newdurecfile, .set=setdurecfile},
		[DUREC_NEXT]={"next", .new=newdurecnext},
		[DUREC_RECORDTIME]={"recordtime", .new=newdurecrecordtime},
		[DUREC_INDEX]={"", .new=newdurecindex},
		[DUREC_NAME0]={"", .new=newdurecname},
		[DUREC_NAME1]={"", .new=newdurecname},
		[DUREC_NAME2]={"", .new=newdurecname},
		[DUREC_NAME3]={"", .new=newdurecname},
		[DUREC_INFO]={"", .new=newdurecinfo},
		[DUREC_LENGTH]={"", .new=newdureclength},
		/*
		{"", 10, .new=newdurecindex},
		{"", 11, .new=newdurecname},
		{"", 12, .new=newdurecname},
		{"", 13, .new=newdurecname},
		{"", 14, .new=newdurecname},
		{"", 15, .new=newdurecinfo},
		{"", 16, .new=newdureclength},
		*/

		[DUREC_STOP]={"stop", .set=setdurecstop},
		[DUREC_PLAY]={"play", .set=setdurecplay},
		[DUREC_RECORD]={"record", .set=setdurecrecord},
		[DUREC_DELETE]={"delete", .set=setdurecdelete},
		{0},
	}},
	[REFRESH]={"refresh", .set=setrefresh},
	{0},
};

#if 0
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
	/* write-only */
	{"refresh", -1, .set=setrefresh},
	{0},
};
#endif

int
handleosc(const unsigned char *buf, size_t len)
{
	const char *next;
	const struct oscnode *node;
	struct oscctx ctx;
	struct oscmsg msg;
	int index;

	if (len % 4 != 0)
		return -1;
	msg.err = NULL;
	msg.buf = (unsigned char *)buf;
	msg.end = (unsigned char *)buf + len;
	msg.type = "ss";

	ctx.addr = oscgetstr(&msg);
	msg.type = oscgetstr(&msg);
	if (msg.err) {
		fprintf(stderr, "invalid osc message: %s\n", msg.err);
		return -1;
	}
	if (ctx.addr[0] != '/') {
		fprintf(stderr, "invalid osc address '%s'\n", ctx.addr);
		return -1;
	}
	if (msg.type[0] != ',') {
		fprintf(stderr, "invalid osc types '%s'\n", msg.type);
		return -1;
	}
	++msg.type;

	fprintf(stderr, "handleosc %s\n", ctx.addr);
	memset(ctx.ctl, -1, sizeof ctx.ctl);
	ctx.depth = 0;
	index = 0;
	for (node = tree; node && node->name;) {
		fprintf(stderr, "addr=%s name=%s\n", ctx.addr, node->name);
		next = match(ctx.addr, node->name);
		if (next) {
			ctx.node = node;
			ctx.addr = next;
			fprintf(stderr, "match %s %p\n", node->name, (void *)node->set);
			ctx.ctl[ctx.depth] = index;
			if (node->set) {
				node->set(&ctx, &msg);
				if (msg.err)
					fprintf(stderr, "%s: %s\n", ctx.addr, msg.err);
			}
			node = node->child;
			++ctx.depth;
			index = 0;
		} else {
			++node;
			++index;
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
	struct oscctx ctx;
	int reg, val, j;
	const struct oscnode *node, *child;
	unsigned long ctl;
	char addr[256], *addrend;

	ctx.addr = addr;
	for (i = 0; i < len; ++i) {
		reg = payload[i] >> 16 & 0x7fff;
		val = (long)((payload[i] & 0xffff) ^ 0x8000) - 0x8000;
		addrend = addr;

		ctl = device->regtoctl(reg);
		if (ctl == -1) {
			if (dflag)
				fprintf(stderr, "[%.4X]=%.4hX\n", reg, (unsigned short)val);
			continue;
		}
		putle32(ctx.ctl, ctl);
		child = tree;
		for (j = 0; j < sizeof ctx.ctl && ctx.ctl[j] != (unsigned char)-1; ++j) {
			assert(child);
			node = &child[ctx.ctl[j]];
			*addrend++ = '/';
			addrend = memccpy(addrend, node->name, '\0', addr + sizeof addr - addrend);
			assert(addrend);
			--addrend;
			child = node->child;
		}
		node->new(&ctx, val);

		/*
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
		*/
	}
#if 0
	int reg, val, off;
	const struct oscnode *node;
	const struct oscnode *path[8];
	size_t pathlen;

#endif
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
	int i, j;
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

	inputs = calloc(device->inputslen, sizeof *inputs);
	playbacks = calloc(device->outputslen, sizeof *playbacks);
	outputs = calloc(device->outputslen, sizeof *outputs);
	if (!inputs || !playbacks || !outputs) {
		perror(NULL);
		return -1;
	}
	for (i = 0; i < device->outputslen; ++i) {
		struct output *out;

		playbacks[i].stereo = true;
		out = &outputs[i];
		out->mix = calloc(device->inputslen + device->outputslen, sizeof *out->mix);
		if (!out->mix) {
			perror(NULL);
			return -1;
		}
		for (j = 0; j < device->inputslen + device->outputslen; ++j)
			out->mix[j].vol = -650;
	}
	return 0;
}
