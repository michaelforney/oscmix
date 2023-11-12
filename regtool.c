#define _POSIX_C_SOURCE 200809L
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <alsa/asoundlib.h>
#include "arg.h"
#include "intpack.h"
#include "sysex.h"

static int dflag;
static int nflag;
static int uflag;
#ifdef WITH_LIBUSB
static libusb_device_handle *usbdev;
#endif
static snd_rawmidi_t *mididev;
static int subid;

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
dump(const char *name, const unsigned char *buf, size_t len)
{
	size_t i;

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
setreg(unsigned reg, unsigned val)
{
	unsigned long cmd;
	unsigned char buf[4];
	unsigned char sysexbuf[12];
	unsigned par;
	int err, len;

	fprintf(stderr, "setreg %#.4x %#.4x\n", reg, val);
	cmd = (reg & 0x7fff) << 16 | (val & 0xffff);
	if (subid == 0) {
		par = cmd >> 16 ^ cmd;
		par ^= par >> 8;
		par ^= par >> 4;
		par ^= par >> 2;
		par ^= par >> 1;
		cmd |= (~par & 1) << 31;
	}
	putle32(buf, cmd);
	dump("->", buf, sizeof buf);
	if (!uflag) {
		struct sysex sysex;
		ssize_t ret;

		sysex.mfrid = 0x200d;
		sysex.devid = 0x10;
		sysex.data = NULL;
		sysex.datalen = 5;
		sysex.subid = subid;
		len = sysexenc(&sysex, sysexbuf, SYSEX_MFRID | SYSEX_DEVID | SYSEX_SUBID);
		base128enc(sysex.data, buf, sizeof buf);
		dump("sysex", sysexbuf, len);
		if (!nflag) {
			ret = snd_rawmidi_write(mididev, sysexbuf, sizeof sysexbuf);
			if (ret != sizeof sysexbuf) {
				fprintf(stderr, "snd_rawmidi_write: %s", snd_strerror(ret));
				return -1;
			}
		}
#ifdef WITH_LIBUSB
	} else if (usbdev) {
		err = libusb_bulk_transfer(usbdev, 12, buf, sizeof buf, &len, 0);
		if (err < 0) {
			fprintf(stderr, "libusb_bulk_transfer: %d\n", err);
			return -1;
		}
#endif
	}
	return 0;
}

static snd_rawmidi_t *
midiopen(const char *port)
{
	snd_ctl_t *ctl;
	snd_rawmidi_t *midi;
	snd_rawmidi_info_t *info;
	int card, dev, err;
	char portbuf[3 + (sizeof card * CHAR_BIT + 2) / 3 + 1];

	ctl = NULL;
	midi = NULL;
	if (!port) {
		port = portbuf;
		card = -1;
		for (;;) {
			err = snd_card_next(&card);
			if (err)
				fatal("snd_card_next: %s", snd_strerror(err));
			if (card < 0)
				break;
			sprintf(portbuf, "hw:%d", card);
			err = snd_ctl_open(&ctl, port, 0);
			if (err)
				fatal("snd_ctl_open %s: %s", port, snd_strerror(err));
			err = snd_rawmidi_info_malloc(&info);
			if (err)
				fatal("snd_rawmidi_info_malloc: %s", snd_strerror(err));
			dev = -1;
			for (;;) {
				err = snd_ctl_rawmidi_next_device(ctl, &dev);
				if (err)
					fatal("snd_ctl_rawmidi_next_device: %s", snd_strerror(err));
				if (dev < 0)
					break;
				snd_rawmidi_info_set_device(info, dev);
				err = snd_ctl_rawmidi_info(ctl, info);
				if (err)
					fatal("snd_ctl_rawmidi_info: %s", snd_strerror(err));
				if (strncmp(snd_rawmidi_info_get_name(info), "Fireface UCX II (", 17) == 0)
					goto found;
			}
		}
		if (card == -1)
			fatal("could not find UCX II midi device");
	found:
		err = snd_ctl_rawmidi_prefer_subdevice(ctl, 1);
		if (err)
			fatal("snd_ctl_rawmidi_prefer_subdevice 1: %s", port, snd_strerror(err));
	}
	err = snd_rawmidi_open(NULL, &midi, port, 0);
	if (err)
		fatal("snd_rawmidi_open: %s", snd_strerror(err));
	if (ctl)
		snd_ctl_close(ctl);
	err = snd_rawmidi_info(midi, info);
	return midi;
}

static void
usage(void)
{
	fprintf(stderr, "regtool [register value]...\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
#ifdef WITH_LIBUSB
	libusb_context *ctx;
#endif
	unsigned reg, val;
	const char *port;

	port = NULL;
	ARGBEGIN {
	case 'n':
		nflag = 1;
		break;
	case 'd':
		dflag = 1;
		break;
	case 'u':
		uflag = 1;
		break;
	case 'p':
		port = EARGF(usage());
		break;
	case 's':
		subid = strtoul(EARGF(usage()), NULL, 0);
		break;
	} ARGEND

	if (argc != 2)
		usage();

	if (!nflag) {
		if (!uflag) {
			mididev = midiopen(port);
		} else {
#ifdef WITH_LIBUSB
			if (libusb_init(&ctx) != 0) {
				fprintf(stderr, "libusb_init failed\n");
				return 1;
			}
			usbdev = libusb_open_device_with_vid_pid(ctx, 0x2a39, /*0x3f82*/0x3fd9);
			if (!usbdev) {
				fprintf(stderr, "libusb_open_device_with_vid_pid failed\n");
				return 1;
			}
#endif
		}
	}
	reg = strtoul(argv[0], NULL, 0);
	val = strtoul(argv[1], NULL, 0);
	setreg(reg, val);
	snd_rawmidi_close(mididev);
}
