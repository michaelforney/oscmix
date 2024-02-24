#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <alsa/asoundlib.h>
#include "../arg.h"
#include "../sysex.h"

static snd_seq_t *seq;
static int sflag;
static int wflag;

static void
usage(void)
{
	fprintf(stderr,
		"usage: regtool [-s] client:port\n"
		"       regtool [-s] -w client:port [reg val]...\n"
	);
	exit(1);
}

static void
dumpsysex(const char *prefix, const unsigned char *buf, size_t len)
{
	static const unsigned char hdr[] = {0xf0, 0x00, 0x20, 0x0d, 0x10};
	const unsigned char *pos, *end;
	unsigned long regval;
	unsigned reg, val, par;

	pos = buf;
	end = pos + len;
	if (sflag) {
		fputs(prefix, stdout);
		for (; pos != end; ++pos)
			printf(" %.2X", *pos);
		fputc('\n', stdout);
	}
	pos = buf;
	--end;
	if (len < sizeof hdr || memcmp(pos, hdr, sizeof hdr) != 0 || (len - sizeof hdr - 2) % 5 != 0) {
		printf("skipping unexpected sysex\n");
		return;
	}
	if (pos[5] != 0) {
		printf("subid=%d", pos[5]);
		for (pos += sizeof hdr + 1; pos != end; pos += 5) {
			regval = getle32_7bit(pos);
			printf("%c%.8lX", pos == buf + sizeof hdr + 1 ? '\t' : ' ', regval);
		}
		fputc('\n', stdout);
		return;
	}
	for (pos += sizeof hdr + 1; pos != end; pos += 5) {
		regval = getle32_7bit(pos);
		reg = regval >> 16 & 0x7fff;
		val = regval & 0xffff;
		par = regval ^ regval >> 16 ^ 1;
		par ^= par >> 8;
		par ^= par >> 4;
		par ^= par >> 2;
		par ^= par >> 1;
		printf("%.4X\t%.4X", reg, val);
		if (par & 1)
			printf("bad parity");
		fputc('\n', stdout);
	}
	fflush(stdout);
}

static void
midiread(void)
{
	int ret;
	size_t len;
	snd_seq_event_t *evt;
	const unsigned char *evtbuf;
	size_t evtlen;
	unsigned char buf[8192];

	len = 0;
	for (;;) {
		ret = snd_seq_event_input(seq, &evt);
		if (ret < 0) {
			fprintf(stderr, "snd_seq_event_input: %s\n", snd_strerror(ret));
			if (ret == -ENOSPC) {
				fprintf(stderr, "buffer overrun: some events were dropped\n");
				continue;
			}
			exit(1);
		}
		if (evt->type != SND_SEQ_EVENT_SYSEX || evt->data.ext.len == 0)
			continue;
		evtbuf = evt->data.ext.ptr;
		evtlen = evt->data.ext.len;
		if (evtbuf[0] == 0xf0) {
			if (len > 0) {
				fprintf(stderr, "dropping incomplete sysex\n");
				len = 0;
			}
		}
		if (evtlen > sizeof buf - len) {
			fprintf(stderr, "dropping sysex that is too long\n");
			len = evtbuf[evtlen - 1] == 0xf7 ? 0 : sizeof buf;
			continue;
		}
		memcpy(buf + len, evtbuf, evtlen);
		len += evtlen;
		if (buf[len - 1] == 0xf7) {
			dumpsysex("<-", buf, len);
			len = 0;
		}
	}
}

static void
setreg(unsigned reg, unsigned val)
{
	snd_seq_event_t evt;
	int err;
	unsigned par;
	unsigned long regval;
	unsigned char buf[12] = {0xf0, 0x00, 0x20, 0x0d, 0x10, 0x00, [sizeof buf - 1]=0xf7};

	reg &= 0x7fff;
	val &= 0xffff;
	par = reg ^ val ^ 1;
	par ^= par >> 8;
	par ^= par >> 4;
	par ^= par >> 2;
	par ^= par >> 1;
	regval = par << 31 | reg << 16 | val;
	putle32_7bit(buf + 6, regval);
	dumpsysex("->", buf, sizeof buf);
	snd_seq_ev_clear(&evt);
	snd_seq_ev_set_source(&evt, 0);
	snd_seq_ev_set_subs(&evt);
	snd_seq_ev_set_direct(&evt);
	snd_seq_ev_set_sysex(&evt, sizeof buf, buf);
	err = snd_seq_event_output_direct(seq, &evt);
	if (err < 0)
		fprintf(stderr, "snd_seq_event_output: %s\n", snd_strerror(err));
}

static void
midiwrite(void)
{
	unsigned reg, val;
	char str[256];

	while (fgets(str, sizeof str, stdin)) {
		if (sscanf(str, "%x %x", &reg, &val) != 2) {
			fprintf(stderr, "invalid input\n");
			continue;
		}
		setreg(reg, val);
	}
}

int
main(int argc, char *argv[])
{
	int err, flags;
	snd_seq_addr_t dest, self;
	snd_seq_port_subscribe_t *sub;
	char *end;

	ARGBEGIN {
	case 's':
		sflag = 1;
		break;
	case 'w':
		wflag = 1;
		break;
	default:
		usage();
	} ARGEND

	if (argc < 1 || (!wflag && argc != 1) || argc % 2 != 1)
		usage();

	dest.client = strtol(argv[0], &end, 10);
	if (*end != ':')
		usage();
	dest.port = strtol(end + 1, &end, 10);
	if (*end)
		usage();

	err = snd_seq_open(&seq, "default", wflag ? SND_SEQ_OPEN_OUTPUT : SND_SEQ_OPEN_INPUT, 0);
	if (err) {
		fprintf(stderr, "snd_seq_open: %s\n", snd_strerror(err));
		return 1;
	}
	err = snd_seq_set_client_name(seq, "regtool");
	if (err) {
		fprintf(stderr, "snd_seq_set_client_name: %s\n", snd_strerror(err));
		return 1;
	}
	if (wflag)
		flags = SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ;
	else
		flags = SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE;
	err = snd_seq_create_simple_port(seq, "regtool", flags, SND_SEQ_PORT_TYPE_MIDI_GENERIC);
	if (err) {
		fprintf(stderr, "snd_seq_create_simple_port: %s\n", snd_strerror(err));
		return 1;
	}

	err = snd_seq_port_subscribe_malloc(&sub);
	if (err) {
		fprintf(stderr, "snd_seq_port_subscribe_malloc: %s\n", snd_strerror(err));
		return 1;
	}
	self.client = snd_seq_client_id(seq);
	self.port = 0;
	snd_seq_port_subscribe_set_sender(sub, wflag ? &self : &dest);
	snd_seq_port_subscribe_set_dest(sub, wflag ? &dest : &self);
	err = snd_seq_subscribe_port(seq, sub);
	if (err) {
		fprintf(stderr, "snd_seq_subscribe_port: %s\n", snd_strerror(err));
		return 1;
	}

	if (wflag) {
		if (argc > 1) {
			int i;
			long reg, val;

			for (i = 1; i < argc; i += 2) {
				reg = strtol(argv[i], &end, 16);
				if (*end || reg < 0 || reg > 0x7fff)
					usage();
				val = strtol(argv[i + 1], &end, 16);
				if (*end || val < -0x8000 || val > 0xffff)
					usage();
				setreg(reg, val);
			}
		} else {
			midiwrite();
		}
	} else {
		midiread();
	}
}
