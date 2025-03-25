#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/MIDIServices.h>
#include "arg.h"
#include "fatal.h"
#include "spawn.h"

struct context {
	MIDIPortRef port;
	MIDIEndpointRef ep;
	int fd;
};

static void
usage(void)
{
	fprintf(stderr, "usage: coremidiio [-rw] [-f rfd,wfd] [-p port] [cmd...]\n");
	exit(1);
}

static void
epdesc(MIDIObjectRef obj, char *buf, size_t len)
{
	char *pos, *end;
	CFStringRef model, name;
	CFIndex used;
	CFRange range;
	OSStatus err;

	if (len == 0)
		return;
	pos = buf;
	end = buf + len - 1;
	err = MIDIObjectGetStringProperty(obj, kMIDIPropertyModel, &model);
	if (err)
		model = 0;
	err = MIDIObjectGetStringProperty(obj, kMIDIPropertyName, &name);
	if (err)
		name = 0;
	if (model) {
		range = CFRangeMake(0, CFStringGetLength(model));
		CFStringGetBytes(model, range, kCFStringEncodingUTF8, '?', false, (unsigned char *)pos, end - pos, &used);
		pos += used;
	}
	if (name && (!model || CFStringCompare(model, name, 0) != kCFCompareEqualTo)) {
		if (pos != buf && pos != end)
			*pos++ = ' ';
		range = CFRangeMake(0, CFStringGetLength(name));
		CFStringGetBytes(name, range, kCFStringEncodingUTF8, '?', false, (unsigned char *)pos, end - pos, &used);
		pos += used;
	}
	*pos = '\0';
}

static void
listports(void)
{
	ItemCount i, n;
	MIDIEndpointRef ep;
	char desc[256];

	printf("Sources:\n");
	n = MIDIGetNumberOfSources();
	for (i = 0; i < n; ++i) {
		ep = MIDIGetSource(i);
		epdesc(ep, desc, sizeof desc);
		printf("%d\t%s\n", (int)i, desc);
	}

	printf("\nDestinations:\n");
	n = MIDIGetNumberOfDestinations();
	for (i = 0; i < n; ++i) {
		ep = MIDIGetDestination(i);
		epdesc(ep, desc, sizeof desc);
		printf("%d\t%s\n", (int)i, desc);
	}
}

static void
midiread(const MIDIPacketList *list, void *info, void *src)
{
	struct context *ctx;
	UInt32 i;
	UInt16 len;
	ssize_t ret;
	const MIDIPacket *p;
	const Byte *pos;

	ctx = info;
	p = &list->packet[0];
	for (i = 0; i < list->numPackets; ++i) {
		len = p->length;
		pos = p->data;
		while (len > 0) {
			ret = write(ctx->fd, pos, len);
			if (ret < 0)
				fatal("write:");
			pos += ret;
			len -= ret;
		}
		p = MIDIPacketNext(p);
	}
}

static OSStatus
midiwrite(struct context *ctx, MIDIPacketList *list)
{
	if (ctx->port)
		return MIDISend(ctx->port, ctx->ep, list);
	return MIDIReceived(ctx->ep, list);
}

static MIDIPacket *
addpacket(struct context *ctx, MIDIPacketList *list, size_t listlen, MIDIPacket *p, const unsigned char *data, size_t datalen)
{
	int err;

	p = MIDIPacketListAdd(list, listlen, p, 0, datalen, data);
	if (p)
		return p;
	err = midiwrite(ctx, list);
	if (err)
		fatal("MIDISend: %d", err);
	p = MIDIPacketListInit(list);
	p = MIDIPacketListAdd(list, listlen, p, 0, datalen, data);
	if (!p)
		fatal("MIDIPacketListAdd failed");
	return p;
}

static void
handleinput(CFFileDescriptorRef file, CFOptionFlags flags, void *info)
{
	static unsigned char data[2], *datapos, *dataend;
	struct context *ctx;
	ssize_t ret;
	MIDIPacket *p;
	int b, err;
	const unsigned char *pos, *end, *tmp;
	unsigned char buf[1024];
	union {
		MIDIPacketList list;
		unsigned char data[2048];
	} u;

	ctx = info;
	ret = read(ctx->fd, buf, sizeof buf);
	if (ret < 0)
		fatal("read");
	if (ret == 0) {
		CFRunLoopStop(CFRunLoopGetMain());
		return;
	}
	CFFileDescriptorEnableCallBacks(file, kCFFileDescriptorReadCallBack);
	p = MIDIPacketListInit(&u.list);
	pos = buf;
	end = buf + ret;
	if (data[0] == 0xF0) {
	sysex:
		tmp = pos;
		while (++pos != end) {
			if (*pos & 0x80) {
				if (*pos == 0xF7) {
					data[0] = 0;
					++pos;
				}
				break;
			}
		}
		p = addpacket(ctx, &u.list, sizeof u, p, tmp, pos - tmp);
	}
	for (; pos != end; ++pos) {
		b = *pos & 0xFF;
		if (b & 0x80) {
			datapos = data;
			switch (b >> 4 & 7) {
			case 4: case 5:
				dataend = datapos + 2;
				break;
			case 0: case 1: case 2: case 3: case 6:
				dataend = datapos + 3;
				break;
			case 7:
				if (b >= 0xF8) {
					dataend = datapos + 1;
					break;
				}
				switch (b & 7) {
				case 0: dataend = NULL; break;
				case 6:
				case 7: dataend = datapos + 1; break;
				case 1:
				case 3: dataend = datapos + 2; break;
				case 2: dataend = datapos + 3; break;
				case 4:
				case 5: continue;  /* invalid status byte */
				}
				break;
			}
		} else if (!data[0]) {
			continue;  /* invalid (no status byte) */
		} else if (datapos == dataend)  {
			/* running status */
			datapos = data + 1;
		}
		*datapos++ = b;
		if (b == 0xF0)
			goto sysex;
		if (datapos == dataend)
			p = addpacket(ctx, &u.list, sizeof u, p, data, dataend - data);
	}
	if (u.list.numPackets > 0) {
		err = midiwrite(ctx, &u.list);
		if (err)
			fatal("MIDISend: %d", err);
	}
}

static void
initreader(struct context *ctx, MIDIClientRef client, int index, int fd)
{
	int err;

	ctx->fd = fd;
	if (index != -1) {
		ctx->ep = MIDIGetSource(index);
		if (!ctx->ep)
			fatal("MIDIGetSource %d failed", index);
		err = MIDIInputPortCreate(client, CFSTR(""), midiread, ctx, &ctx->port);
		if (err)
			fatal("MIDIInputPortCreate: %d", err);
		err = MIDIPortConnectSource(ctx->port, ctx->ep, NULL);
		if (err)
			fatal("MIDIPortConnectSource: %d", err);
	} else {
		ctx->port = 0;
		err = MIDIDestinationCreate(client, CFSTR("coremidiio"), midiread, ctx, &ctx->ep);
		if (err)
			fatal("MIDIDestinationCreate: %d");
	}
}

static void
initwriter(struct context *ctx, MIDIClientRef client, int index, int fd)
{
	CFFileDescriptorRef file;
	CFFileDescriptorContext filectx;
	CFRunLoopSourceRef source;
	int err;

	ctx->fd = fd;
	if (index != -1) {
		ctx->ep = MIDIGetDestination(index);
		if (!ctx->ep)
			fatal("MIDIGetDestination %d failed", index);
		err = MIDIOutputPortCreate(client, CFSTR(""), &ctx->port);
		if (err)
			fatal("MIDIOutputPortCreate: %d", err);
	} else {
		ctx->port = 0;
		err = MIDISourceCreate(client, CFSTR("coremidiio"), &ctx->ep);
		if (err)
			fatal("MIDISourceCreate: %d", err);
	}
	memset(&filectx, 0, sizeof filectx);
	filectx.info = ctx;
	file = CFFileDescriptorCreate(NULL, ctx->fd, false, handleinput, &filectx);
	if (!file)
		fatal("CFFileDescriptorCreate %d failed", 0);
	CFFileDescriptorEnableCallBacks(file, kCFFileDescriptorReadCallBack);
	source = CFFileDescriptorCreateRunLoopSource(NULL, file, 0);
	if (!source)
		fatal("CFFileDescriptorCreateRunLoopSource failed");
	CFRunLoopAddSource(CFRunLoopGetMain(), source, kCFRunLoopDefaultMode);
}

static void
notify(const struct MIDINotification *n, void *info)
{
	struct context *ctx;
	MIDIObjectRef obj;

	ctx = info;
	if (n->messageID == kMIDIMsgObjectRemoved) {
		obj = ((MIDIObjectAddRemoveNotification *)n)->child;
		if (obj == ctx[0].ep || obj == ctx[1].ep)
			CFRunLoopStop(CFRunLoopGetMain());
	}
}

static void
parseintpair(const char *arg, int num[static 2])
{
	char *end;
	long n;

	num[0] = -1;
	if (*arg != ',') {
		n = strtol(arg, &end, 10);
		if (end == arg || n < 0 || n > INT_MAX)
			usage();
		num[0] = (int)n;
		if (!*end) {
			num[1] = num[0];
			return;
		}
		if (*end != ',')
			usage();
		arg = end + 1;
	}
	num[1] = -1;
	if (*arg) {
		n = strtol(arg, &end, 10);
		if (end == arg || *end || n < 0 || n > INT_MAX)
			usage();
		num[1] = (int)n;
	}
}

int
main(int argc, char *argv[])
{
	MIDIClientRef client;
	OSStatus err;
	int port[2], fd[2];
	char *arg, *end;
	int mode;
	long n;
	struct context ctx[2];

	port[0] = -1;
	port[1] = -1;
	fd[0] = 0;
	fd[1] = 1;
	mode = 0;
	ARGBEGIN {
	case 'l':
		listports();
		return 0;
	case 'p':
		parseintpair(EARGF(usage()), port);
		break;
	case 'f':
		parseintpair(EARGF(usage()), fd);
		break;
	case 'r':
		mode |= READ;
		break;
	case 'w':
		mode |= WRITE;
		break;
	default:
		usage();
	} ARGEND

	if (mode == 0)
		mode = READ | WRITE;
	if (argc)
		spawn(argv[0], argv, mode, fd);

	err = MIDIClientCreate(CFSTR("coremidiio"), notify, ctx, &client);
	if (err)
		fatal("MIDIClientCreate: %d", err);
	if (mode & READ)
		initreader(&ctx[0], client, port[0], fd[1]);
	if (mode & WRITE)
		initwriter(&ctx[1], client, port[1], fd[0]);
	CFRunLoopRun();
}
