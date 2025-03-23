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

struct context {
	const struct node *node;
	const char *pattern;
	char *addr, *addrpos, *addrend;
	struct param param;
	bool exact;
};

struct node {
	const char *name;
	enum control ctl;
	void (*set)(struct context *ctx, struct oscmsg *msg);
	void (*new)(struct context *ctx, int val);
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
	const struct node *tree;
};

struct input {
	bool stereo;
	bool mute;
	int width;
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
		fprintf(stderr, "setreg %.4X %.4X\n", reg, val);
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

static void
setval(struct context *ctx, int val)
{
	int reg;

	reg = device->ctltoreg(ctx->node->ctl, &ctx->param);
	if (reg != -1)
		setreg(reg, val);
}

static void
setint(struct context *ctx, struct oscmsg *msg)
{
	int_least32_t val;

	val = oscgetint(msg);
	if (oscend(msg) != 0)
		return;
	setval(ctx, val);
}

static void
newint(struct context *ctx, int val)
{
	oscsend(ctx->addr, ",i", val);
}

static void
setfixed(struct context *ctx, struct oscmsg *msg)
{
	float val;

	val = oscgetfloat(msg);
	if (oscend(msg) != 0)
		return;
	setval(ctx, lroundf(val / ctx->node->scale));
}

static void
newfixed(struct context *ctx, int val)
{
	oscsend(ctx->addr, ",f", val * ctx->node->scale);
}

static void
setenum(struct context *ctx, struct oscmsg *msg)
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
newenum(struct context *ctx, int val)
{
	oscsendenum(ctx->addr, val, ctx->node->names, ctx->node->nameslen);
}

static void
setbool(struct context *ctx, struct oscmsg *msg)
{
	bool val;

	if (!ctx->exact)
		return;
	val = oscgetint(msg);
	if (oscend(msg) != 0)
		return;
	setval(ctx, val);
}

static void
newbool(struct context *ctx, int val)
{
	if (ctx->exact)
		oscsend(ctx->addr, ",i", val != 0);
}

static void
setmixlevel(const struct input *in, const struct output *out, float level)
{
	int reg;
	long val;
	struct param p;

	p.in = in - inputs;
	p.out = out - outputs;
	reg = device->ctltoreg(MIX_LEVEL, &p);
	if (reg == -1)
		return;
	val = lroundf(level * 0x8000);
	assert(val >= 0);
	assert(val <= 0x10000);
	if (val > 0x4000)
		val = (val >> 3) - 0x8000;
	setreg(reg, val);
}

static void
setchannel(struct context *ctx, struct oscmsg *msg)
{
	char *end;
	long index;

	index = strtol(ctx->pattern + 1, &end, 10);
	if (*end != '/')
		return;
	if (strcmp(ctx->node->name, "input") == 0)
		ctx->param.in = index - 1;
	else
		ctx->param.out = index - 1;
	ctx->pattern = end;
}

static void
newchannel(struct context *ctx, int val)
{
	const char *type;
	int ret, chan;

	if (ctx->param.in != -1) {
		type = "input";
		chan = ctx->param.in;
	} else {
		type = "output";
		chan = ctx->param.out;
	}
	ret = snprintf(ctx->addr, ctx->addrend - ctx->addr, "/%s/%d", type, chan + 1);
	if (ret >= 0)
		ctx->addrpos = ctx->addr + ret;
}

static void
muteinput(struct input *in, bool mute)
{
	const struct output *out;
	const float *mix;

	if (in->mute == mute)
		return;
	if (in->stereo && (in - inputs) & 1)
		--in;
	in[0].mute = mute;
	if (in->stereo)
		in[1].mute = mute;
	for (out = outputs; out != outputs + device->outputslen; ++out) {
		mix = &out->mix[in - inputs];
		if (mix[0] > 0)
			setmixlevel(in, out, mute ? 0 : mix[0]);
		if (in->stereo && mix[1] > 0)
			setmixlevel(in + 1, out, mute ? 0 : mix[1]);
	}
}

static void
setinputmute(struct context *ctx, struct oscmsg *msg)
{
	struct input *in;
	bool val;

	val = oscgetint(msg);
	if (oscend(msg) != 0)
		return;
	assert((unsigned)ctx->param.in < device->inputslen);
	/* mutex */
	in = &inputs[ctx->param.in];
	setval(ctx, val);
	muteinput(in, val);
}

static void
newinputmute(struct context *ctx, int val)
{
	struct input *in;

	assert((unsigned)ctx->param.in < device->inputslen);
	in = &inputs[ctx->param.in];
	muteinput(in, val);
	newbool(ctx, val);
}

static void
setinputstereo(struct context *ctx, struct oscmsg *msg)
{
	struct input *in;
	bool val;

	val = oscgetint(msg);
	if (oscend(msg) != 0)
		return;
	in = &inputs[ctx->param.in & ~1];
	in[0].stereo = in[1].stereo = val;
	setval(ctx, val);
	ctx->param.in ^= 1;
	setval(ctx, val);
}

static void
newinputstereo(struct context *ctx, int val)
{
	struct input *in;

	assert((unsigned)ctx->param.in < device->inputslen);
	in = &inputs[ctx->param.in & ~1];
	in[0].stereo = val;
	in[1].stereo = val;
	snprintf(ctx->addr, ctx->addrend - ctx->addr, "/input/%d/stereo", (int)(in - inputs) + 1);
	oscsend(ctx->addr, ",i", val != 0);
	snprintf(ctx->addr, ctx->addrend - ctx->addr, "/input/%d/stereo", (int)(in - inputs) + 2);
	oscsend(ctx->addr, ",i", val != 0);
}

static void
newoutputstereo(struct context *ctx, int val)
{
	struct output *out;

	assert((unsigned)ctx->param.out < device->outputslen);
	out = &outputs[ctx->param.out & ~1];
	out[0].stereo = val;
	out[1].stereo = val;
	snprintf(ctx->addr, ctx->addrend - ctx->addr, "/output/%d/stereo", (int)(out - outputs));
	oscsend(ctx->addr, ",i", val != 0);
	snprintf(ctx->addr, ctx->addrend - ctx->addr, "/output/%d/stereo", (int)(out - outputs + 1));
	oscsend(ctx->addr, ",i", val != 0);
}

static void
setname(struct context *ctx, struct oscmsg *msg)
{
	const char *name;
	char namebuf[12];
	int i, reg, val;

	name = oscgetstr(msg);
	if (oscend(msg) != 0)
		return;
	reg = device->ctltoreg(ctx->node->ctl, &ctx->param);
	if (reg == -1)
		return;
	strncpy(namebuf, name, sizeof namebuf - 1);
	namebuf[sizeof namebuf - 1] = '\0';
	for (i = 0; i < sizeof namebuf; i += 2, ++reg) {
		val = getle16(namebuf + i);
		setreg(reg, val);
	}
}

static void
setinputgain(struct context *ctx, struct oscmsg *msg)
{
	const struct channelinfo *info;
	float val;

	val = oscgetfloat(msg);
	if (oscend(msg) != 0)
		return;
	assert((unsigned)ctx->param.in < device->inputslen);
	info = &device->inputs[ctx->param.in];
	if (info->flags & INPUT_HAS_GAIN) {
		if (val < info->gain.min)
			val = info->gain.min;
		if (val > info->gain.max)
			val = info->gain.max;
		setval(ctx, val * 10);
	}
}

static void
newinputgain(struct context *ctx, int val)
{
	oscsend(ctx->addr, ",f", val / 10.0);
}

static void
newinputreflevel(struct context *ctx, int val)
{
	const struct channelinfo *info;

	assert((unsigned)ctx->param.in < device->inputslen);
	info = &device->inputs[ctx->param.in];
	oscsendenum(ctx->addr, val & 0xf, info->reflevel.names, info->reflevel.nameslen);
}

static void
setoutputloopback(struct context *ctx, struct oscmsg *msg)
{
	bool val;
	unsigned char buf[4], sysexbuf[7 + 5];

	val = oscgetint(msg);
	if (oscend(msg) != 0)
		return;
	putle32(buf, val << 7 | ctx->param.in);
	writesysex(3, buf, sizeof buf, sysexbuf);
}

static void
seteqdrecord(struct context *ctx, struct oscmsg *msg)
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
newdspload(struct context *ctx, int val)
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
newdspavail(struct context *ctx, int val)
{
}

static void
newdspstatus(struct context *ctx, int val)
{
}

static void
newarcdelta(struct context *ctx, int val)
{
}

static void
setdb(const struct output *out, const struct input *in, float db)
{
	int reg, val;
	struct param p;

	p.in = in - inputs;
	p.out = out - outputs;
	reg = device->ctltoreg(MIX, &p);
	if (reg == -1)
		return;
	val = (isinf(db) && db < 0 ? -650 : lroundf(db * 10.f)) & 0x7fff;
	setreg(reg, val);
}

static void
setpan(const struct output *out, const struct input *in, int pan)
{
	int reg, val;
	struct param p;

	p.in = in - inputs;
	p.out = out - outputs;
	reg = device->ctltoreg(MIX, &p);
	if (reg == -1)
		return;
	val = (pan & 0x7fff) | 0x8000;
	setreg(reg, val);
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
	float w, theta;
	float ll, lr, rl, rr;
	float *mix[2];

	if (instereo)
		instereo = in->stereo;
	if (instereo && (in - inputs) & 1)
		--in;
	if (out->stereo && (out - outputs) & 1)
		--out;
	mix[0] = &out[0].mix[in - inputs];
	if (out->stereo) {
		mix[1] = &out[1].mix[in - inputs];
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
			mix[0][1] = rl;
			mix[1][1] = rr;
			if (!in->mute) {
				setmixlevel(in + 1, out, rl);
				setmixlevel(in + 1, out + 1, rr);
			}
		} else {
			theta = (l->pan + 100) * PI / 400.f;
			ll = cosf(theta) * l->vol;
			lr = sinf(theta) * l->vol;
		}
		mix[0][0] = ll;
		mix[1][0] = lr;
		if (!in->mute) {
			setmixlevel(in, out, ll);
			setmixlevel(in, out + 1, lr);
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
			mix[0][1] = rl;
			if (!in->mute)
				setmixlevel(in + 1, out, rl);
		} else {
			ll = l->vol;
		}
		mix[0][0] = ll;
		if (!in->mute)
			setmixlevel(in, out, ll);
	}
}

static void
setmix(struct context *ctx, struct oscmsg *msg)
{
	unsigned long i;
	char *end;
	float vol;
	struct level level;
	struct output *out;
	struct input *in;
	int base;

	i = strtoul(ctx->pattern + 1, &end, 10) - 1;
	if (*end != '/' || i >= device->outputslen)
		return;
	ctx->param.out = i;
	ctx->pattern = end;
	out = &outputs[i];

	if (oscmatch(ctx->pattern, "input", &end))
		base = 0;
	else if (oscmatch(ctx->pattern, "playback", &end))
		base = device->inputslen;
	else
		return;
	i = strtoul(end + 1, &end, 10) - 1;
	if (*end || i >= device->inputslen + device->outputslen - base)
		return;
	i += base;
	ctx->param.in = i;
	ctx->pattern = end;
	in = &inputs[i];

	if (out->stereo && (out - outputs) & 1)
		--out;
	if (in->stereo && (in - inputs) & 1)
		--in;

	calclevel(out, in, 1, &level);
	level.width = in->width;
	vol = oscgetfloat(msg);
	level.vol = vol <= -65.f ? 0 : powf(10.f, vol / 20.f);

	if (*msg->type) {
		level.pan = oscgetint(msg);
		if (level.pan < -100)
			level.pan = -100;
		else if (level.pan > 100)
			level.pan = 100;
	}
	if (oscend(msg) != 0)
		return;
	setlevel(out, in, 1, &level);
	calclevel(out, in, 0, &level);
	setdb(out, in, 20.f * log10f(level.vol));
	setpan(out, in, level.pan);
	if (in->stereo) {
		calclevel(out, in + 1, 0, &level);
		setdb(out, in + 1, 20.f * log10f(level.vol));
		setpan(out, in + 1, level.pan);
	}
}

static void
newmix(struct context *ctx, int val)
{
	struct output *out;
	struct input *in;
	bool ispan;
	struct level level;

	if (ctx->param.out >= device->outputslen || ctx->param.in >= device->inputslen)
		return;
	assert(ctx->param.in >= 0);
	assert(ctx->param.out >= 0);
	in = &inputs[ctx->param.in];
	out = &outputs[ctx->param.out];
	if ((out - outputs) & 1 && out->stereo)
		return;
	ispan = val & 0x8000;
	val = ((val & 0x7fff) ^ 0x4000) - 0x4000;
	calclevel(out, in, 0, &level);
	in->width = level.width;
	if (ispan) {
		if (val < -100)
			val = -100;
		if (val > 100)
			val = 100;
		level.pan = val;
	} else {
		level.vol = val <= -650 ? 0 : powf(10.f, val / 200.f);
		if (level.vol > 2)
			level.vol = 2;
	}
	setlevel(out, in, 0, &level);
	if (in->stereo) {
		if ((in - inputs) & 1)
			--in;
		calclevel(out, in, 1, &level);
	}
	snprintf(ctx->addr, ctx->addrend - ctx->addr, "/mix/%d/input/%d", (int)(out - outputs) + 1, (int)(in - inputs) + 1);
	oscsend(ctx->addr, ",fi", level.vol > 0 ? 20.f * log10f(level.vol) : -INFINITY, level.pan);
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
	return val >= 0 && val < LEN(samplerate) ? samplerate[val] : 0;
}

static void
newsamplerate(struct context *ctx, int val)
{
	long rate;

	rate = getsamplerate(val);
	if (rate != 0)
		oscsend(ctx->addr, ",i", rate);
}

static void
newmeter(struct context *ctx, int val)
{
	const char *type, *name;
	int chan;

	if (ctx->param.in != -1) {
		type = "input";
		chan = ctx->param.in;
	} else {
		type = "output";
		chan = ctx->param.out;
	}
	name = ctx->node->ctl == AUTOLEVEL_METER ? "autolevel" : "dynamics";
	snprintf(ctx->addr, ctx->addrend - ctx->addr, "/%s/%d/%s/meter", type, chan + 1, name);
	oscsend(ctx->addr, ",i", val >> 8 & 0xFF);
	snprintf(ctx->addr, ctx->addrend - ctx->addr, "/%s/%d/%s/meter", type, chan + 2, name);
	oscsend(ctx->addr, ",i", val & 0xFF);
}

static void
newdurecstatus(struct context *ctx, int val)
{
	static const char *const names[] = {
		"No Media", "Filesystem Error", "Initializing", "Reinitializing",
		[5] = "Stopped", "Recording",
		[10] = "Playing", "Paused",
	};
	int status;
	int position;

	status = val & 0xF;
	if (status != durec.status) {
		durec.status = status;
		oscsendenum("/durec/status", status, names, LEN(names));
	}
	position = (val >> 8) * 100 / 65;
	if (position != durec.position) {
		durec.position = position;
		oscsend("/durec/position", ",i", position);
	}
}

static void
newdurectime(struct context *ctx, int val)
{
	if (val != durec.time) {
		durec.time = val;
		oscsend("/durec/time", ",i", val);
	}
}

static void
newdurecusbstatus(struct context *ctx, int val)
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
newdurectotalspace(struct context *ctx, int val)
{
	float totalspace;

	totalspace = val / 16.f;
	if (totalspace != durec.totalspace) {
		durec.totalspace = totalspace;
		oscsend("/durec/totalspace", ",f", totalspace);
	}
}

static void
newdurecfreespace(struct context *ctx, int val)
{
	float freespace;

	freespace = val / 16.f;
	if (freespace != durec.freespace) {
		durec.freespace = freespace;
		oscsend("/durec/freespace", ",f", freespace);
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
newdurecfileslen(struct context *ctx, int val)
{
	resizedurecfiles(val);
}

static void
setdurecfile(struct context *ctx, struct oscmsg *msg)
{
	int val;

	val = oscgetint(msg);
	if (oscend(msg) != 0)
		return;
	setval(ctx, val | 0x8000);
}

static void
newdurecfile(struct context *ctx, int val)
{
	if (val != durec.file) {
		durec.file = val;
		oscsend(ctx->addr, ",i", val);
	}
}

static void
newdurecnext(struct context *ctx, int val)
{
	static const char *const names[] = {
		"Single", "UFX Single", "Continuous", "Single Next", "Repeat Single", "Repeat All",
	};
	int next, playmode;

	next = ((val & 0xfff) ^ 0x800) - 0x800;
	if (next != durec.next) {
		durec.next = next;
		oscsend("/durec/next", ",i", next);
	}
	playmode = val >> 12;
	if (playmode != durec.playmode) {
		durec.playmode = playmode;
		oscsendenum("/durec/playmode", playmode, names, LEN(names));
	}
}

static void
newdurecrecordtime(struct context *ctx, int val)
{
	unsigned time;

	time = (unsigned)val & 0xFFFF;
	if (time != durec.recordtime) {
		durec.recordtime = time;
		oscsend("/durec/recordtime", ",i", time);
	}
}

static void
newdurecindex(struct context *ctx, int val)
{
	if (val + 1 > durec.fileslen)
		resizedurecfiles(val + 1);
	durec.index = val;
}

static void
newdurecname(struct context *ctx, int val)
{
	struct durecfile *f;
	char *pos, old[2];
	int off;

	if (durec.index == -1)
		return;
	assert(durec.index < durec.fileslen);
	f = &durec.files[durec.index];
	off = (ctx->node->ctl - DUREC_NAME0) * 2;
	assert(off >= 0 && off < sizeof f->name);
	pos = f->name + off;
	memcpy(old, pos, sizeof old);
	putle16(pos, val);
	if (memcmp(old, pos, sizeof old) != 0)
		oscsend("/durec/name", ",is", durec.index, f->name);
}

static void
newdurecinfo(struct context *ctx, int val)
{
	struct durecfile *f;
	unsigned long samplerate;
	int channels;

	if (durec.index == -1)
		return;
	f = &durec.files[durec.index];
	samplerate = getsamplerate(val & 0xFF);
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
newdureclength(struct context *ctx, int val)
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
setdurecstop(struct context *ctx, struct oscmsg *msg)
{
	if (oscend(msg) != 0)
		return;
	setval(ctx, 0x8120);
}

static void
setdurecplay(struct context *ctx, struct oscmsg *msg)
{
	if (oscend(msg) != 0)
		return;
	setval(ctx, 0x8123);
}

static void
setdurecrecord(struct context *ctx, struct oscmsg *msg)
{
	if (oscend(msg) != 0)
		return;
	setval(ctx, 0x8122);
}

static void
setdurecdelete(struct context *ctx, struct oscmsg *msg)
{
	int val;

	val = oscgetint(msg);
	if (oscend(msg) != 0)
		return;
	setval(ctx, 0x8000 | val);
}

static void
setrefresh(struct context *ctx, struct oscmsg *msg)
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
		pb = &inputs[device->inputslen + i];
		snprintf(addr, sizeof addr, "/playback/%d/stereo", i + 1);
		oscsend(addr, ",i", pb->stereo);
	}
	oscflush();
}

static void
newrefresh(struct context *ctx, int val)
{
	refreshing = false;
	if (dflag)
		fprintf(stderr, "refresh done\n");
}

static const struct node lowcuttree[] = {
	{"freq", LOWCUT_FREQ, .set=setint, .new=newint, .min=20, .max=500},
	{"slope", LOWCUT_SLOPE, .set=setint, .new=newint},
	{0},
};

static const struct node eqtree[] = {
	{"band1freq", EQ_BAND1FREQ, .set=setint, .new=newint, .min=20, .max=20000},
	{"band1gain", EQ_BAND1GAIN, .set=setfixed, .new=newfixed, .scale=0.1, .min=-200, .max=200},
	{"band1q", EQ_BAND1Q, .set=setfixed, .new=newfixed, .scale=0.1, .min=4, .max=99},
	{"band1type", EQ_BAND1TYPE, .set=setenum, .new=newenum, .names=(const char *const[]){
		"Peak", "Low Shelf", "High Pass", "Low Pass",
	}, .nameslen=4},
	{"band2freq", EQ_BAND2FREQ, .set=setint, .new=newint, .min=20, .max=20000},
	{"band2gain", EQ_BAND2GAIN, .set=setfixed, .new=newfixed, .scale=0.1, .min=-200, .max=200},
	{"band2q", EQ_BAND2Q, .set=setfixed, .new=newfixed, .scale=0.1, .min=4, .max=99},
	{"band3freq", EQ_BAND3FREQ, .set=setint, .new=newint, .min=20, .max=20000},
	{"band3gain", EQ_BAND3GAIN, .set=setfixed, .new=newfixed, .scale=0.1, .min=-200, .max=200},
	{"band3q", EQ_BAND3Q, .set=setfixed, .new=newfixed, .scale=0.1, .min=4, .max=99},
	{"band3type", EQ_BAND3TYPE, .set=setenum, .new=newenum, .names=(const char *const[]){
		"Peak", "High Shelf", "Low Pass", "High Pass",
	}, .nameslen=3},
	{0},
};

static const struct node dynamicstree[] = {
	{"gain", DYNAMICS_GAIN, .set=setfixed, .new=newfixed, .scale=0.1, .min=-300, .max=300},
	{"attack", DYNAMICS_ATTACK, .set=setint, .new=newint, .min=0, .max=200},
	{"release", DYNAMICS_RELEASE, .set=setint, .new=newint, .min=100, .max=999},
	{"compthres", DYNAMICS_COMPTHRES, .set=setfixed, .new=newfixed, .scale=0.1, .min=-600, .max=0},
	{"compratio", DYNAMICS_COMPRATIO, .set=setfixed, .new=newfixed, .scale=0.1, .min=10, .max=100},
	{"expthres", DYNAMICS_EXPTHRES, .set=setfixed, .new=newfixed, .scale=0.1, .min=-990, .max=200},
	{"expratio", DYNAMICS_EXPRATIO, .set=setfixed, .new=newfixed, .scale=0.1, .min=10, .max=100},
	{NULL, DYNAMICS_METER, .new=newmeter},
	{0},
};

static const struct node autoleveltree[] = {
	{"maxgain", AUTOLEVEL_MAXGAIN, .set=setfixed, .new=newfixed, .scale=0.1, .min=0, .max=180},
	{"headroom", AUTOLEVEL_HEADROOM, .set=setfixed, .new=newfixed, .scale=0.1, .min=30, .max=120},
	{"risetime", AUTOLEVEL_RISETIME, .set=setfixed, .new=newfixed, .scale=0.1, .min=1, .max=99},
	{NULL, AUTOLEVEL_METER, .new=newmeter},
	{""},
};

static const struct node roomeqtree[] = {
	{"delay", ROOMEQ_DELAY, .set=setfixed, .new=newfixed, .min=0, .max=425, .scale=0.001},
	{"band1type", ROOMEQ_BAND1TYPE, .set=setenum, .new=newenum, .names=(const char *const[]){
		"Peak", "Low Shelf", "High Pass", "Low Pass",
	}, .nameslen=4},
	{"band1gain", ROOMEQ_BAND1GAIN, .set=setfixed, .new=newfixed, .min=-200, .max=200, .scale=0.1},
	{"band1freq", ROOMEQ_BAND1FREQ, .set=setint, .new=newint, .min=20, .max=20000},
	{"band1q", ROOMEQ_BAND1Q, .set=setfixed, .new=newfixed, .min=4, .max=99, .scale=0.1},
	{"band2gain", ROOMEQ_BAND2GAIN, .set=setfixed, .new=newfixed, .min=-200, .max=200, .scale=0.1},
	{"band2freq", ROOMEQ_BAND2FREQ, .set=setint, .new=newint, .min=20, .max=20000},
	{"band2q", ROOMEQ_BAND2Q, .set=setfixed, .new=newfixed, .min=4, .max=99, .scale=0.1},
	{"band3gain", ROOMEQ_BAND3GAIN, .set=setfixed, .new=newfixed, .min=-200, .max=200, .scale=0.1},
	{"band3freq", ROOMEQ_BAND3FREQ, .set=setint, .new=newint, .min=20, .max=20000},
	{"band3q", ROOMEQ_BAND3Q, .set=setfixed, .new=newfixed, .min=4, .max=99, .scale=0.1},
	{"band4gain", ROOMEQ_BAND4GAIN, .set=setfixed, .new=newfixed, .min=-200, .max=200, .scale=0.1},
	{"band4freq", ROOMEQ_BAND4FREQ, .set=setint, .new=newint, .min=20, .max=20000},
	{"band4q", ROOMEQ_BAND4Q, .set=setfixed, .new=newfixed, .min=4, .max=99, .scale=0.1},
	{"band5gain", ROOMEQ_BAND5GAIN, .set=setfixed, .new=newfixed, .min=-200, .max=200, .scale=0.1},
	{"band5freq", ROOMEQ_BAND5FREQ, .set=setint, .new=newint, .min=20, .max=20000},
	{"band5q", ROOMEQ_BAND5Q, .set=setfixed, .new=newfixed, .min=4, .max=99, .scale=0.1},
	{"band6gain", ROOMEQ_BAND6GAIN, .set=setfixed, .new=newfixed, .min=-200, .max=200, .scale=0.1},
	{"band6freq", ROOMEQ_BAND6FREQ, .set=setint, .new=newint, .min=20, .max=20000},
	{"band6q", ROOMEQ_BAND6Q, .set=setfixed, .new=newfixed, .min=4, .max=99, .scale=0.1},
	{"band7gain", ROOMEQ_BAND7GAIN, .set=setfixed, .new=newfixed, .min=-200, .max=200, .scale=0.1},
	{"band7freq", ROOMEQ_BAND7FREQ, .set=setint, .new=newint, .min=20, .max=20000},
	{"band7q", ROOMEQ_BAND7Q, .set=setfixed, .new=newfixed, .min=4, .max=99, .scale=0.1},
	{"band8type", ROOMEQ_BAND8TYPE, .set=setenum, .new=newenum, .names=(const char *const[]){
		"Peak", "High Shelf", "Low Pass", "High Pass",
	}, .nameslen=4},
	{"band8gain", ROOMEQ_BAND8GAIN, .set=setfixed, .new=newfixed, .min=-200, .max=200, .scale=0.1},
	{"band8freq", ROOMEQ_BAND8FREQ, .set=setint, .new=newint, .min=20, .max=20000},
	{"band8q", ROOMEQ_BAND8Q, .set=setfixed, .new=newfixed, .min=4, .max=99, .scale=0.1},
	{"band9type", ROOMEQ_BAND9TYPE, .set=setenum, .new=newenum, .names=(const char *const[]){
		"Peak", "High Shelf", "Low Pass", "High Pass",
	}, .nameslen=4},
	{"band9gain", ROOMEQ_BAND9GAIN, .set=setfixed, .new=newfixed, .min=-200, .max=200, .scale=0.1},
	{"band9freq", ROOMEQ_BAND9FREQ, .set=setint, .new=newint, .min=20, .max=20000},
	{"band9q", ROOMEQ_BAND9Q, .set=setfixed, .new=newfixed, .min=4, .max=99, .scale=0.1},
	{0},
};

static const struct node roottree[] = {
	{"input", .set=setchannel, .new=newchannel, .tree=(const struct node[]){
		{"mute", INPUT_MUTE, .set=setinputmute, .new=newinputmute},
		{"fx", INPUT_FXSEND, .set=setfixed, .new=newfixed, .min=-650, .max=0, .scale=0.1},
		{"stereo", INPUT_STEREO, .set=setinputstereo, .new=newinputstereo},
		{"record", INPUT_RECORD, .set=setbool, .new=newbool},
		{"name", NAME, .set=setname},
		{"playchan", INPUT_PLAYCHAN, .set=setint, .new=newint, .min=1, .max=60},
		{"msproc", INPUT_MSPROC, .set=setbool, .new=newbool},
		{"phase", INPUT_PHASE, .set=setbool, .new=newbool},
		{"gain", INPUT_GAIN, .set=setinputgain, .new=newinputgain},
		{"reflevel", INPUT_REFLEVEL, .set=setint, .new=newinputreflevel},
		{"48v", INPUT_48V, .set=setbool, .new=newbool},
		{"autoset", INPUT_AUTOSET, .set=setbool, .new=newbool},
		{"hi-z", INPUT_HIZ, .set=setbool, .new=newbool},
		{"lowcut", LOWCUT, .set=setbool, .new=newbool, .tree=lowcuttree},
		{"eq", EQ, .set=setbool, .new=newbool, .tree=eqtree},
		{"dynamics", DYNAMICS, .set=setbool, .new=newbool, .tree=dynamicstree},
		{"autolevel", AUTOLEVEL, .set=setbool, .new=newbool, .tree=autoleveltree},
		{0},
	}},
	{"output", .set=setchannel, .new=newchannel, .tree=(const struct node[]){
		{"volume", OUTPUT_VOLUME, .set=setfixed, .new=newfixed, .scale=0.1, .min=-65.0, .max=6.0},
		{"pan", OUTPUT_PAN, .set=setint, .new=newint, .min=-100, .max=100},
		{"mute", OUTPUT_MUTE, .set=setbool, .new=newbool},
		{"fx", OUTPUT_FXRETURN, .set=setfixed, .new=newfixed, .scale=0.1, .min=-65.0, .max=0.0},
		{"stereo", OUTPUT_STEREO, .set=setbool, .new=newoutputstereo},
		{"record", OUTPUT_RECORD, .set=setbool, .new=newbool},
		{"name", NAME, .set=setname},
		{"playchan", OUTPUT_PLAYCHAN, .set=setint, .new=newint},
		{"phase", OUTPUT_PHASE, .set=setbool, .new=newbool},
		{"reflevel", OUTPUT_REFLEVEL, .set=setenum, .new=newenum, .names=(const char *const[]){
			"+4dBu", "+13dBu", "+19dBu",
		}, .nameslen=3}, // TODO: phones
		{"crossfeed", OUTPUT_CROSSFEED, .set=setint, .new=newint},
		{"volumecal", OUTPUT_VOLUMECAL, .set=setfixed, .new=newfixed, .min=-2400, .max=300, .scale=0.01},
		{"lowcut", LOWCUT, .set=setbool, .new=newbool, .tree=lowcuttree},
		{"eq", EQ, .set=setbool, .new=newbool, .tree=eqtree},
		{"dynamics", DYNAMICS, .set=setbool, .new=newbool, .tree=dynamicstree},
		{"autolevel", AUTOLEVEL, .set=setbool, .new=newbool, .tree=autoleveltree},
		{"roomeq", ROOMEQ, .set=setbool, .new=newbool, .tree=roomeqtree},
		{"loopback", .set=setoutputloopback},
		{0},
	}},
	{"playback", .tree=(const struct node[]){
		{0},
	}},
	{"mix", MIX, .set=setmix, .new=newmix},
	{"reverb", REVERB, .set=setbool, .new=newbool, .tree=(const struct node[]){
		{"type", REVERB_TYPE, .set=setenum, .new=newenum, .names=(const char *const[]){
			"Small Room", "Medium Room", "Large Room", "Walls",
			"Shorty", "Attack", "Swagger", "Old School",
			"Echoistic", "8plus9", "Grand Wide", "Thicker",
			"Envelope", "Gated", "Space",
		}, .nameslen=15},
		{"predelay", REVERB_PREDELAY, .set=setint, .new=newint},
		{"lowcut", REVERB_LOWCUT, .set=setint, .new=newint},
		{"roomscale", REVERB_ROOMSCALE, .set=setfixed, .new=newfixed, .scale=0.01},
		{"attack", REVERB_ATTACK, .set=setint, .new=newint},
		{"hold", REVERB_HOLD, .set=setint, .new=newint},
		{"release", REVERB_RELEASE, .set=setint, .new=newint},
		{"highcut", REVERB_HIGHCUT, .set=setint, .new=newint},
		{"time", REVERB_TIME, .set=setfixed, .new=newfixed, .scale=0.1},
		{"highdamp", REVERB_HIGHDAMP, .set=setint, .new=newint},
		{"smooth", REVERB_SMOOTH, .set=setint, .new=newint},
		{"volume", REVERB_VOLUME, .set=setfixed, .new=newfixed, .scale=0.1},
		{"width", REVERB_WIDTH, .set=setfixed, .new=newfixed, .scale=0.01},
		{0},
	}},
	{"echo", ECHO, .set=setbool, .new=newbool, .tree=(const struct node[]){
		{"type", ECHO_TYPE, .set=setenum, .new=newenum, .names=(const char *const[]){
			"Stereo Echo",
			"Stereo Cross",
			"Pong Echo",
		}, .nameslen=3},
		{"delay", ECHO_DELAY, .set=setfixed, .new=newfixed, .scale=0.001, .min=0, .max=2000},
		{"feedback", ECHO_FEEDBACK, .set=setint, .new=newint},
		{"highcut", ECHO_HIGHCUT, .set=setenum, .new=newenum, .names=(const char *const[]){
			"Off", "16kHz", "12kHz", "8kHz", "4kHz", "2kHz",
		}, .nameslen=6},
		{"volume", ECHO_VOLUME, .set=setfixed, .new=newfixed, .scale=0.1, .min=-650, .max=60},
		{"width", ECHO_WIDTH, .set=setfixed, .new=newfixed, .scale=0.01},
		{0},
	}},
	{"controlroom", .tree=(const struct node[]){
		{"mainout", CTLROOM_MAINOUT, .set=setenum, .new=newenum, .names=(const char *const[]){
			"1/2", "3/4", "5/6", "7/8", "9/10",
			"11/12", "13/14", "15/16", "17/18", "19/20",
		}, .nameslen=10},
		{"mainmono", CTLROOM_MAINMONO, .set=setbool, .new=newbool},
		{"muteenable", CTLROOM_MUTEENABLE, .set=setbool, .new=newbool},
		{"dimreduction", CTLROOM_DIMREDUCTION, .set=setfixed, .new=newfixed, .scale=0.1, .min=-650, .max=0},
		{"dim", CTLROOM_DIM, .set=setbool, .new=newbool},
		{"recallvolume", CTLROOM_RECALLVOLUME, .set=setfixed, .new=newfixed, .scale=0.1, .min=-650, .max=0},
		{0},
	}},
	{"clock", .tree=(const struct node[]){
		{"source", CLOCK_SOURCE, .set=setenum, .new=newenum, .names=(const char *const[]){
			"Internal", "Word Clock", "SPDIF", "AES", "Optical",
		}, .nameslen=5},
		{"samplerate", CLOCK_SAMPLERATE, .new=newsamplerate},
		{"wckout", CLOCK_WCKOUT, .set=setbool, .new=newbool},
		{"wcksingle", CLOCK_WCKSINGLE, .set=setbool, .new=newbool},
		{"wckterm", CLOCK_WCKTERM, .set=setbool, .new=newbool},
		{0},
	}},
	{"hardware", .tree=(const struct node[]){
		{"opticalout", HARDWARE_OPTICALOUT, .set=setenum, .new=newenum, .names=(const char *const[]){
			"ADAT", "SPDIF",
		}, .nameslen=2},
		{"spdifout", HARDWARE_SPDIFOUT, .set=setenum, .new=newenum, .names=(const char *const[]){
			"Consumer", "Professional",
		}, .nameslen=2},
		{"ccmode", HARDWARE_CCMODE, .new=newbool},
		{"ccmix", HARDWARE_CCMIX, .set=setenum, .new=newenum, .names=(const char *const[]){
			"TotalMix App", "6ch + phones", "8ch", "20ch",
		}, .nameslen=4},
		{"standalonemidi", HARDWARE_STANDALONEMIDI, .set=setbool, .new=newbool},
		{"standalonearc", HARDWARE_STANDALONEARC, .set=setenum, .new=newenum, .names=(const char *const[]){
			"Volume", "1s Op", "Normal",
		}, .nameslen=3},
		{"lockkeys", HARDWARE_LOCKKEYS, .set=setenum, .new=newenum, .names=(const char *const[]){
			"Off", "Keys", "All",
		}, .nameslen=3},
		{"remapkeys", HARDWARE_REMAPKEYS, .set=setbool, .new=newbool},
		{"eqdrecord", .set=seteqdrecord},
		{NULL, HARDWARE_DSPVERLOAD, .new=newdspload},
		{NULL, HARDWARE_DSPAVAIL, .new=newdspavail},
		{NULL, HARDWARE_DSPSTATUS, .new=newdspstatus},
		{NULL, HARDWARE_ARCDELTA, .new=newarcdelta},
		{0},
	}},
	{"durec", .tree=(const struct node[]){
		{"play", .set=setdurecplay},
		{"stop", .set=setdurecstop},
		{"record", .set=setdurecrecord},
		{"delete", .set=setdurecdelete},
		{"file", DUREC_FILE, .set=setdurecfile, .new=newdurecfile},
		{NULL, DUREC_STATUS, .new=newdurecstatus},
		{NULL, DUREC_TIME, .new=newdurectime},
		{NULL, DUREC_USBLOAD, .new=newdurecusbstatus},
		{NULL, DUREC_TOTALSPACE, .new=newdurectotalspace},
		{NULL, DUREC_FREESPACE, .new=newdurecfreespace},
		{NULL, DUREC_NUMFILES, .new=newdurecfileslen},
		{NULL, DUREC_NEXT, .new=newdurecnext},
		{NULL, DUREC_RECORDTIME, .new=newdurecrecordtime},
		{NULL, DUREC_INDEX, .new=newdurecindex},
		{NULL, DUREC_NAME0, .new=newdurecname},
		{NULL, DUREC_NAME1, .new=newdurecname},
		{NULL, DUREC_NAME2, .new=newdurecname},
		{NULL, DUREC_NAME3, .new=newdurecname},
		{NULL, DUREC_INFO, .new=newdurecinfo},
		{NULL, DUREC_LENGTH, .new=newdureclength},
		{0},
	}},
	{"refresh", REFRESH, .set=setrefresh, .new=newrefresh},
	{0},
};

/* maps control number to indices into roottree */
static unsigned char nodeindex[NUMCTLS][4];

int
handleosc(const unsigned char *buf, size_t len)
{
	const struct node *node;
	struct context ctx;
	struct oscmsg msg;
	const char *pattern;
	char *end;

	if (len % 4 != 0)
		return -1;
	msg.err = NULL;
	msg.buf = (unsigned char *)buf;
	msg.end = (unsigned char *)buf + len;
	msg.type = "ss";

	pattern = oscgetstr(&msg);
	msg.type = oscgetstr(&msg);
	if (msg.err) {
		fprintf(stderr, "invalid osc message: %s\n", msg.err);
		return -1;
	}
	if (pattern[0] != '/') {
		fprintf(stderr, "invalid osc address '%s'\n", pattern);
		return -1;
	}
	if (msg.type[0] != ',') {
		fprintf(stderr, "invalid osc types '%s'\n", msg.type);
		return -1;
	}
	++msg.type;

	ctx.pattern = pattern;
	ctx.param.in = ctx.param.out = -1;
	for (node = roottree; ctx.pattern[0] && node && node->name;) {
		if (!oscmatch(ctx.pattern, node->name, &end)) {
			++node;
			continue;
		}
		ctx.pattern = end;
		ctx.node = node;
		if (node->set) {
			ctx.exact = !ctx.pattern[0];
			node->set(&ctx, &msg);
			if (msg.err)
				fprintf(stderr, "%s: %s\n", pattern, msg.err);
		}
		node = node->tree;
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
		case 'i': oscputint(&oscmsg, va_arg(ap, int)); break;
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
	struct context ctx;
	enum control ctl;
	int reg, val;
	const struct node *tree, *node;
	const unsigned char *idx, *end;
	char addr[256];

	ctx.addr = addr;
	ctx.addrend = addr + sizeof addr;
	for (i = 0; i < len; ++i) {
		reg = payload[i] >> 16 & 0x7fff;
		val = (long)((payload[i] & 0xffff) ^ 0x8000) - 0x8000;
		ctx.param.in = ctx.param.out = -1;
		ctl = device->regtoctl(reg, &ctx.param);
		if (ctl == -1) {
			if (dflag)
				fprintf(stderr, "[%.4X]=%.4X\n", reg, val & 0xFFFFU);
			continue;
		}
		if (ctl == UNKNOWN)
			continue;
		assert(ctl < LEN(nodeindex));
		assert(nodeindex[ctl][0] != 0xFF);

		ctx.addrpos = addr;
		tree = roottree;
		for (idx = nodeindex[ctl], end = idx + sizeof nodeindex[ctl]; idx != end && *idx != 0xFF; ++idx) {
			node = &tree[*idx];
			if (node->name) {
				*ctx.addrpos++ = '/';
				ctx.addrpos = memccpy(ctx.addrpos, node->name, '\0', ctx.addrend - ctx.addrpos);
				assert(ctx.addrpos);
				--ctx.addrpos;
			}
			ctx.node = node;
			if (node->new) {
				ctx.exact = idx == end || idx[1] == 0xFF;
				node->new(&ctx, val);
			}
			tree = node->tree;
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
				oscsend(addr, ",ffffi", peakdb, rmsdb, peakfxdb, rmsfxdb, (int)(peak & peakfx[i] & 1));
			} else {
				oscsend(addr, ",ffi", peakdb, rmsdb, (int)(peak & 1));
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

	setreg(0x3F00, serial);
	serial = (serial + 1) & 0xf;
}

static void
maptree(const struct node *tree, int i)
{
	static unsigned char index[sizeof nodeindex[0]];
	const struct node *node;

	assert(i < sizeof index);
	index[i] = 0;
	for (node = tree; node->set || node->new || node->tree; ++node, ++index[i]) {
		if (node->ctl) {
			memcpy(nodeindex[node->ctl], index, i + 1);
			memset(nodeindex[node->ctl] + i + 1, 0xFF, sizeof index - (i + 1));
		}
		if (node->tree)
			maptree(node->tree, i + 1);
	}
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

	memset(nodeindex, 0xFF, sizeof nodeindex);
	maptree(roottree, 0);

	inputs = calloc(device->inputslen + device->outputslen, sizeof *inputs);
	outputs = calloc(device->outputslen, sizeof *outputs);
	if (!inputs || !outputs) {
		perror(NULL);
		return -1;
	}
	for (i = 0; i < device->inputslen + device->outputslen; ++i) {
		struct input *in;

		in = &inputs[i];
		in->width = 100;
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
