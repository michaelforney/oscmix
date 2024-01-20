#define _XOPEN_SOURCE 700
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include "arg.h"
#include "intpack.h"
#include "osc.h"
#include "sysex.h"

#define LEN(a) (sizeof (a) / sizeof *(a))

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

struct mix {
	bool new;
	signed char pan;
	short vol;
};

struct input {
	bool stereo;
	float width;
};

struct output {
	bool stereo;
	struct mix mix[40];
};

struct durecfile {
	short reg[6];
	char name[9];
	unsigned long samplerate;
	unsigned channels;
	unsigned length;
};

static int dflag;
static int lflag;
static struct input inputs[20];
static struct input playbacks[20];
static struct output outputs[20];
static int sendsock[8];
static size_t sendsocklen;
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
static bool refreshing;

enum eqbandtype {
	EQ_BANDTYPE_PEAK,
	EQ_BANDTYPE_SHELF,
	EO_BANDTYPE_HICUT,
};

enum durecstatus {
	DUREC_STATUS_NOMEDIA,
	DUREC_STATUS_FSERROR,
	DUREC_STATUS_INITIALIZING,
	DUREC_STATUS_DELETING,
	/* ? */
	DUREC_STATUS_STOPPED = 5,
	DUREC_STATUS_RECORDING,
	DUREC_STATUS_PLAYING = 10,
};

enum durecplaymode {
	DUREC_PLAYMODE_SINGLE,
	DUREC_PLAYMODE_UFXSINGLE,
	DUREC_PLAYMODE_CONTINUOUS,
	DUREC_PLAYMODE_SINGLENEXT,
	DUREC_PLAYMODE_REPEATSINGLE,
	DUREC_PLAYMODE_REPEATALL,
};

enum reverbtype {
	REVERB_TYPE_SMALLROOM,
	REVERB_TYPE_MEDIUMROOM,
	REVERB_TYPE_LARGEROOM,
	REVERB_TYPE_WALLS,
	REVERB_TYPE_SHORTY,
	REVERB_TYPE_ATTACK,
	REVERB_TYPE_SWAGGER,
	REVERB_TYPE_OLDSCHOOL,
	REVERB_TYPE_ECHOISTIC,
	REVERB_TYPE_8PLUS9,
	REVERB_TYPE_GRANDWIDE,
	REVERB_TYPE_THICKER,
	REVERB_TYPE_ENVELOPE,
	REVERB_TYPE_GATED,
	REVERB_TYPE_SPACE,
};

enum echotype {
	ECHO_TYPE_STEREOECHO,
	ECHO_TYPE_STEREOCROSS,
	ECHO_TYPE_PONGECHO,
};

enum clocksource {
	CLOCK_SOURCE_INTERNAL,
	CLOCK_SOURCE_WCLK,
	CLOCK_SOURCE_SPDIF,
	CLOCK_SOURCE_AES,
	CLOCK_SOURCE_OPTICAL,
};

enum opticaltype {
	OPTICAL_ADAT,
	OPTICAL_SPDIF,
};

enum spdiftype {
	SPDIF_CONSUMER,
	SPDIF_PROFESSIONAL,
};

enum ccmix {
	CCMIX_TOTALMIX,
	CCMIX_6CHPHONES,
	CCMIX_8CH,
	CCMIX_20CH,
};

enum arcmode {
	ARCMODE_VOLUME,
	ARCMODE_1SEC,
	ARCMODE_NORMAL,
};

enum lockkeys {
	LOCKKEYS_OFF,
	LOCKKEYS_KEYS,
	LOCKKEYS_ALL,
};

static void oscsend(const char *addr, const char *type, ...);
static void oscsendenum(const char *addr, int val, const char *const names[], size_t nameslen);

static void
fatal(const char *msg, ...)
{
	va_list ap;

	va_start(ap, msg);
	vfprintf(stderr, msg, ap);
	va_end(ap);
	if (!msg)
		fprintf(stderr, "%s\n", strerror(errno));
	else if (*msg && msg[strlen(msg) - 1] == ':')
		fprintf(stderr, " %s\n", strerror(errno));
	else
		fputc('\n', stderr);
	exit(1);
}

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

static int
midiwrite(const void *buf, size_t len)
{
	static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
	ssize_t ret;

	pthread_mutex_lock(&lock);
	while (len > 0) {
		ret = write(7, buf, len);
		if (ret < 0)
			goto error;
		buf = (char *)buf + ret;
		len -= ret;
	}
	ret = 0;
error:
	pthread_mutex_unlock(&lock);
	return ret;
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
	//assert(len == sizeof sysexbuf);
	base128enc(sysex.data, buf, len);
	//dump("sysex", sysexbuf, sysexlen);

	if (midiwrite(sysexbuf, sysexlen) != 0)
		fatal("write 7:");
}

static int
setreg(unsigned reg, unsigned val)
{
	unsigned long regval;
	unsigned char buf[4], sysexbuf[7 + 5];
	unsigned par;

	if (reg != 0x3f00)
		fprintf(stderr, "setreg %#.4x %#.4x\n", reg, val);
	regval = (reg & 0x7fff) << 16 | (val & 0xffff);
	par = regval >> 16 ^ regval;
	par ^= par >> 8;
	par ^= par >> 4;
	par ^= par >> 2;
	par ^= par >> 1;
	regval |= (~par & 1) << 31;
	putle32(buf, regval);
	//dump("->", buf, sizeof buf);

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
	setreg(reg, val / node->scale);
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
		printf("setenum %s\n", node->name);
		if (strcmp(node->name, "mainout") == 0)
			printf("setmainout %d\n", val);
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
	printf("setbool %.4x %d\n", reg, val);
	setreg(reg, val);
	return 0;
}

static int
newbool(const struct oscnode *path[], const char *addr, int reg, int val)
{
	if (reg >= 0x0500 && reg < 0x1000 && (reg & 0x3f) == 0x4)
		printf("new stereo %s %d\n", addr, val);
	oscsend(addr, ",i", val != 0);
	return 0;
}

static int
setinputmute(const struct oscnode *path[], int reg, struct oscmsg *msg)
{
	//int ch;
	//struct output *out;

	if (setbool(path, reg, msg) != 0)
		return -1;
	//ch = reg >> 6;
	/*
	for (out = state.output; out != state.output + LEN(state.output); ++out) {
		
	}
	//ch = (reg & 0xff) >> 
	bool val;

	val = oscgetint(msg);
	if (oscend(msg) != 0)
		return -1;
	setreg(reg, val);
	*/
	return 0;
}

static int
newinputstereo(const struct oscnode *path[], const char *addr, int reg, int val)
{
	int i;

	i = reg >> 6;
	assert(i < LEN(inputs));
	inputs[i].stereo = val;
	return newbool(path, addr, reg, val);
}

static int
newoutputstereo(const struct oscnode *path[], const char *addr, int reg, int val)
{
	int i;

	i = (reg - 0x0500) >> 6;
	assert(i < LEN(outputs));
	outputs[i].stereo = val;
	return newbool(path, addr, reg, val);
}

static int
setinputname(const struct oscnode *path[], int reg, struct oscmsg *msg)
{
	const char *name;
	char namebuf[12];
	int i, ch, val;

	ch = reg >> 6;
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
	mic = (reg >> 6) <= 2;
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
	int ch;

	ch = (reg >> 6) + 1;
	if (ch < 3 || ch > 4)
		return -1;
	return setbool(path, reg, msg);
}

static int
newinput48v_reflevel(const struct oscnode *path[], const char *addr, int reg, int val)
{
	int ch;
	const char *const names[] = {"+7dBu", "+13dBu", "+19dBu"};

	fprintf(stderr, "newinput48v_reflevel %.4x %.4hx\n", reg, (short)val);
	ch = (reg >> 6) + 1;
	if (ch >= 1 && ch <= 2) {
		return newbool(path, addr, reg, val);
	} else if (ch >= 3 && ch <= 4) {
		oscsendenum(addr, val & 0xf, names, 2);
		return 0;
	} else if (ch >= 5 && ch <= 8) {
		oscsendenum(addr, val & 0xf, names + 1, 2);
		return 0;
	}
	return -1;
}

static int
setinputhiz(const struct oscnode *path[], int reg, struct oscmsg *msg)
{
	int ch;
	
	ch = (reg >> 6) + 1;
	if (ch >= 3 && ch <= 4)
		return setbool(path, reg, msg);
	return -1;
}

static int
newinputhiz(const struct oscnode *path[], const char *addr, int reg, int val)
{
	int ch;
	
	ch = (reg >> 6) + 1;
	if (ch >= 3 && ch <= 4)
		return newbool(path, addr, reg, val);
	return -1;
}

static int
setdb(int reg, float db)
{
	int val;

	fprintf(stderr, "setdb %.4x %f\n", reg, db);
	val = isinf(db) && db < 0 ? -300 : (int)(db * 10) & 0x7fff;
	return setreg(reg, val);
}

static int
setpan(int reg, int pan)
{
	int val;

	fprintf(stderr, "setpan %.4x %d\n", reg, pan);
	val = (pan & 0x7fff) | 0x8000;
	return setreg(reg, val);
}

static int
setlevel(int reg, float level)
{
	int val;

	fprintf(stderr, "setlevel %.4x %f\n", reg, level);
	assert(level >= 0);
	assert(level <= 2);
	val = level > 0.5 ? (int)(level * 0x1000) | 0x8000 : (int)(level * 0x8000);
	return setreg(reg, val);
}

static int
setmix(const struct oscnode *path[], int reg, struct oscmsg *msg)
{
	int outidx, inidx, pan;
	float vol, level, theta, width;
	//bool outstereo, instereo;
	struct output *out;
	struct input *in;
	/* XXX: int promotion */

	outidx = (reg & 0xfff) >> 6;
	if (outidx >= LEN(outputs))
		return -1;
	out = &outputs[outidx];

	inidx = reg & 0x3f;
	if (inidx < LEN(inputs))
		in = &inputs[inidx];
	else if (inidx - 0x20 < LEN(playbacks))
		in = &playbacks[inidx - 0x20];
	else
		return -1;
	/*
	outstereo = regs[0x0504 | (reg >> 6 & 0x3f) << 6];
	instereo = regs[0x0002 | (reg & 0x3f) << 6];
	*/
	//outstereo = false;
	//instereo = false;

	vol = oscgetfloat(msg);
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
	printf("pan=%d\n", pan);
	printf("instereo=%d outstereo=%d\n", in->stereo, out->stereo);

	level = pow(10, vol / 20);
	if (in->stereo) {
		float level0, level1, level00, level10, level01, level11;

		level0 = (100 - (pan > 0 ? pan : 0)) / 200.f * level;
		level1 = (100 + (pan < 0 ? pan : 0)) / 200.f * level;
		printf("level0=%f level1=%f\n", level0 / level, level1 / level);
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
			setpan(reg, acos(2 * level00 / level0 - 1) * 200 / M_PI - 100);

			level10 = level10 * level10;
			level1 = level10 + level11 * level11;
			setdb(reg + 1, 10 * log10(level1));
			setpan(reg + 1, acos(2 * level10 / level1 - 1) * 200 / M_PI - 100);
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
			theta = (pan + 100) * M_PI / 400;
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
	if (outidx >= LEN(outputs) || inidx >= LEN(inputs))
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
	//fprintf(stderr, "newmix %s %d %d %d\n", newpan ? "pan" : "vol", val, outidx + 1, inidx + 1);
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
		//fprintf(stderr, "\tvol=%f pan=%d level0=%f level1=%f %d %d\n", vol, pan, level0, level1, mix[0].vol, mix[1].vol);
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

static int
setregs(const struct oscnode *path[], int unused, struct oscmsg *msg)
{
	int reg, val;

	while (*msg->type) {
		reg = oscgetint(msg);
		val = oscgetint(msg);
		setreg(reg, val);
	}
	return oscend(msg);
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
		[DUREC_STATUS_NOMEDIA] = "No Media",
		[DUREC_STATUS_FSERROR] = "Filesystem Error",
		[DUREC_STATUS_INITIALIZING] = "Initializing",
		[DUREC_STATUS_DELETING] = "Deleting",
		[DUREC_STATUS_STOPPED] = "Stopped",
		[DUREC_STATUS_RECORDING] = "Recording",
		[DUREC_STATUS_PLAYING] = "Playing",
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
		[DUREC_PLAYMODE_SINGLE] = "Single",
		[DUREC_PLAYMODE_UFXSINGLE] = "UFX Single",
		[DUREC_PLAYMODE_CONTINUOUS] = "Continuous",
		[DUREC_PLAYMODE_SINGLENEXT] = "Single Next",
		[DUREC_PLAYMODE_REPEATSINGLE] = "Repeat Single",
		[DUREC_PLAYMODE_REPEATALL] = "Repeat All",
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

static void
refresh(void)
{
	setreg(0x3e04, 0x67cd);
	refreshing = true;
}

static int
setrefresh(const struct oscnode *path[], int reg, struct oscmsg *msg)
{
	refresh();
	return 0;
}

static int
refreshdone(const struct oscnode *path[], int reg, struct oscmsg *msg)
{
	refreshing = false;
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
	{"mute", 0x00, .set=setinputmute, .new=newbool},
	{"fxsend", 0x01, .set=setfixed, .new=newfixed, .min=-650, .max=0, .scale=0.1},
	{"stereo", 0x02, .set=setbool, .new=newinputstereo},
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
	{"fxreturn", 0x03, .set=setfixed, .new=newfixed, .scale=0.1, .min=-65.0, .max=0.0},
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
		{"hicut", 0x08, .set=setint, .new=newint},
		{"time", 0x09, .set=setfixed, .new=newfixed, .scale=0.1},
		{"hidamp", 0x0a, .set=setint, .new=newint},
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
		{"hicut", 0x04, .set=setenum, .new=newenum, .names=(const char *const[]){
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
	{"register", -1, .set=setregs},
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

static int
dispatch(unsigned char *buf, size_t len)
{
	const char *addr, *next;
	const struct oscnode *path[8], *node;
	size_t pathlen;
	struct oscmsg msg;
	int reg;

	//dump("osc <-", buf, len);

	if (len % 4 != 0)
		return -1;
	msg.err = NULL;
	msg.buf = buf;
	msg.end = buf + len;
	msg.type = "ss";

	addr = oscgetstr(&msg);
	msg.type = oscgetstr(&msg);
	if (msg.err) {
		fprintf(stderr, "invalid osc message: %s\n", msg.err);
		return -1;
	}
	++msg.type;

	fprintf(stderr, "dispatch %s %s\n", addr, msg.type);
	/*
	if (buf == end) {
		fprintf(stderr, "bad osc packet: no type string\n");
		return -1;
	}
	*/

	reg = 0;
	pathlen = 0;
	for (node = tree; node->name;) {
		//printf("%s\n", node->name);
		next = match(addr + 1, node->name);
		if (next) {
			//printf("match next=%s\n", next);
			path[pathlen++] = node;
			reg += node->reg;
			if (*next) {
				node = node->child;
				addr = next;
			} else {
				//printf("dispatch %s\n", node->name);
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

static inline
uint_least32_t
getle32_7bit(const void *p)
{
	const unsigned char *b = p;
	uint_least32_t v;

	v = b[0] & 0x7ful;
	v |= (b[1] & 0x7ful) << 7;
	v |= (b[2] & 0x7ful) << 14;
	v |= (b[3] & 0x7ful) << 21;
	v |= (b[4] & 0xful) << 28;
	return v;
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
		printf("%zu %s\n", nameslen, names[0]);
		oscsend(addr, ",i", val);
	}
}

static void
oscflush(void)
{
	const unsigned char *buf;
	size_t len, i;
	ssize_t ret;

	if (oscmsg.buf) {
		buf = oscbuf;
		len = oscmsg.buf - oscbuf;
		//dump("bundle", buf, len);
		for (i = 0; i < sendsocklen; ++i) {
			ret = write(sendsock[i], buf, len);
			if (ret < 0) {
				perror("write");
			} else if (ret != len) {
				fprintf(stderr, "write: %zd != %zu", ret, len);
			}
		}
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
	/*
	const struct oscnode *path[8];
	size_t pathlen;
	*/

	for (i = 0; i < len; ++i) {
		reg = payload[i] >> 16 & 0x7fff;
		val = (long)((payload[i] & 0xffff) ^ 0x8000) - 0x8000;
		//if (reg < 0x0500)
		//	printf("[%.4x] = %.4x\n", reg, val);
		/*
		if (reg >= LEN(regs)) {
			fprintf(stderr, "unknown reg %04x", reg);
			continue;
		}
		old = regs[reg];
		if (old != val) {
			regs[reg] = val;
			//regchange(reg, val, old);
		}
		*/
		addrend = addr;
		off = 0;
		node = tree;
		while (node->name) {
			if (reg >= off + node[1].reg && node[1].name && node[1].reg != -1) {
				++node;
				continue;
			}
			*addrend++ = '/';
			addrend = memccpy(addrend, node->name, '\0', addr + sizeof addr - addrend);
			assert(addrend);
			--addrend;
			if (reg == off + node->reg && node->new) {
				node->new(&node, addr, reg, val);
			} else if (node->child) {
				off += node->reg;
				node = node->child;
				continue;
			} else if (reg != off + node->reg) {
				switch (reg) {
				case 0x3080:
				case 0x3081:
				case 0x3082:
				case 0x3083:
				case 0x3180:
				case 0x3380:
					break;
				default:
					if (reg == off + node->reg)
						fprintf(stderr, "[%.4x]=%.4hx (%s)\n", reg, (short)val, addr);
					else
						fprintf(stderr, "[%.4x]=%.4hx\n", reg, (short)val);
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

	if (len % 3 != 0)
		fatal("unexpected levels data");
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

static void
handlesysex(unsigned char *buf, size_t len, uint_least32_t *payload)
{
	struct sysex sysex;
	int ret;
	size_t i;
	uint_least32_t *pos;

	//dump("sysex", buf, len);
	ret = sysexdec(&sysex, buf, len, SYSEX_MFRID | SYSEX_DEVID | SYSEX_SUBID);
	if (ret != 0 || sysex.mfrid != 0x200d || sysex.devid != 0x10 || sysex.datalen % 5 != 0) {
		if (ret == 0)
			fprintf(stderr, "ignoring unknown sysex packet (mfr=%x devid=%x datalen=%zu)\n", sysex.mfrid, sysex.devid, sysex.datalen);
		else
			fprintf(stderr, "ignoring unknown sysex packet\n");
		return;
	}
	//dump("sysex", sysex.data, sysex.datalen);
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

static void *
midiread(void *arg)
{
	unsigned char data[8192], *datapos, *dataend, *nextpos;
	uint_least32_t payload[sizeof data / 4];
	ssize_t ret;

	dataend = data;
	for (;;) {
		ret = read(6, dataend, (data + sizeof data) - dataend);
		if (ret < 0)
			fatal("read 6:");
		//dump(NULL, dataend, ret);
		dataend += ret;
		datapos = data;
		for (;;) {
			assert(datapos <= dataend);
			datapos = memchr(datapos, 0xf0, dataend - datapos);
			if (!datapos) {
				dataend = data;
				break;
			}
			nextpos = memchr(datapos + 1, 0xf7, dataend - datapos - 1);
			if (!nextpos) {
				if (dataend == data + sizeof data) {
					fprintf(stderr, "sysex packet too large; dropping\n");
					dataend = data;
				} else {
					memmove(data, datapos, dataend - datapos);
					dataend -= datapos - data;
				}
				break;
				
			}
			++nextpos;
			//dump("sysex", datapos, nextpos - datapos);
			handlesysex(datapos, nextpos - datapos, payload);
			datapos = nextpos;
		}
	}
	return NULL;
}

static void *
sockread(void *arg)
{
	int fd;
	ssize_t ret;
	unsigned char buf[8192];

	fd = *(int *)arg;
	for (;;) {
		ret = recv(fd, buf, sizeof buf, 0);
		if (ret < 0) {
			perror("recv");
			break;
		}
		dispatch(buf, ret);
	}
	return NULL;
}

static void
timer(void)
{
	static int serial;
	unsigned char buf[7];

	if (lflag && !refreshing) {
		/* XXX: ~60 times per second levels, ~30 times per second serial */
		writesysex(2, NULL, 0, buf);
	}

	setreg(0x3f00, serial);
	serial = (serial + 1) & 0xf;
}

static void
usage(void)
{
	exit(1);
}

static int
opensock(char *addr, int flags)
{
	struct addrinfo hint;
	char *type, *port, *sep;
	struct addrinfo *ais, *ai;
	int err, sock;

	type = addr;
	addr = NULL;
	port = NULL;
	sep = strchr(type, '!');
	if (sep) {
		*sep = '\0';
		addr = sep + 1;
		sep = strchr(addr, '!');
		if (sep) {
			*sep = '\0';
			port = sep + 1;
			if (*port == '\0')
				port = NULL;
		}
		if (*addr == '\0')
			addr = NULL;
	}
	if (strcmp(type, "udp") == 0) {
		memset(&hint, 0, sizeof hint);
		hint.ai_flags = flags;
		hint.ai_family = AF_UNSPEC;
		hint.ai_socktype = SOCK_DGRAM;
		hint.ai_protocol = IPPROTO_UDP;
		err = getaddrinfo(addr, port, &hint, &ais);
		if (err != 0)
			fatal("getaddrinfo: %s", gai_strerror(err));
		for (ai = ais; ai; ai = ai->ai_next) {
			sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
			if (sock >= 0) {
				if ((flags & AI_PASSIVE ? bind : connect)(sock, ai->ai_addr, ai->ai_addrlen) == 0)
					break;
				close(sock);
				sock = -1;
			}
		}
		freeaddrinfo(ais);
		if (sock == -1)
			fatal("connect:");
	} else {
		fatal("unsupported address type '%s'", type);
	}
	return sock;
}

int
main(int argc, char *argv[])
{
	int err, sig, i;
	struct itimerval it;
	int recvsock[8];
	size_t recvsocklen;
	pthread_t midireader, sockreader[LEN(recvsock)];
	sigset_t set;

	recvsocklen = 0;
	ARGBEGIN {
	case 'd':
		dflag = 1;
		break;
	case 'l':
		lflag = 1;
		break;
	case 'r':
		if (recvsocklen == LEN(recvsock))
			fatal("too many recv sockets");
		recvsock[recvsocklen++] = opensock(EARGF(usage()), AI_PASSIVE);
		break;
	case 's':
		if (sendsocklen == LEN(sendsock))
			fatal("too many send sockets");
		sendsock[sendsocklen++] = opensock(EARGF(usage()), 0);
		break;
	default:
		usage();
		break;
	} ARGEND

	if (fcntl(6, F_GETFD) < 0)
		fatal("fcntl 6:");
	if (fcntl(7, F_GETFD) < 0)
		fatal("fcntl 7:");

	if (recvsocklen == 0)
		recvsock[recvsocklen++] = opensock((char[]){"udp!127.0.0.1!7000"}, AI_PASSIVE);
	if (sendsocklen == 0)
		sendsock[sendsocklen++] = opensock((char[]){"udp!127.0.0.1!8000"}, 0);

	sigfillset(&set);
	pthread_sigmask(SIG_SETMASK, &set, NULL);
	err = pthread_create(&midireader, NULL, midiread, NULL);
	if (err) {
		fprintf(stderr, "pthread_create: %s\n", strerror(err));
		return 1;
	}
	for (i = 0; i < recvsocklen; ++i) {
		err = pthread_create(&sockreader[i], NULL, sockread, &recvsock[i]);
		if (err) {
			fprintf(stderr, "pthread_create: %s\n", strerror(err));
			return 1;
		}
	}

	sigemptyset(&set);
	sigaddset(&set, SIGALRM);
	pthread_sigmask(SIG_SETMASK, &set, NULL);
	it.it_interval.tv_sec = 0;
	it.it_interval.tv_usec = 100000;
	it.it_value = it.it_interval;
	if (setitimer(ITIMER_REAL, &it, NULL) != 0)
		fatal("setitimer:");

	refresh();
	for (;;) {
		sigwait(&set, &sig);
		timer();
	}
}
