#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include "arg.h"
#include "base64.h"
#include "http.h"
#include "intpack.h"
#include "sha1.h"
#include "socket.h"
#include "util.h"

enum {
	CLOSE = 0x8,
	PING = 0x9,
	PONG = 0xa,
};

static int rfd;
static int wfd;
static bool closing;

static void
usage(void)
{
	fprintf(stderr, "usage: wsdgram [-m] [-s addr] [-r addr]\n");
	exit(1);
}

static void
handshake(FILE *rd, FILE *wr)
{
	static const char response[] =
		"HTTP/1.1 101 Switching Protocols\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n";
	struct http_request req;
	struct http_header hdr;
	bool websocket, upgrade, havekey;
	long version;
	char *token;
	unsigned char sha1[20];
	char accept[(sizeof sha1 + 2) / 3 * 4], buf[2048];

	if (!fgets(buf, sizeof buf, rd)) {
		if (ferror(rd))
			perror("read");
		goto fail;
	}
	if (http_request(buf, strlen(buf), &req) != 0)
		goto fail;
	if (req.method != HTTP_GET)
		goto fail;
	upgrade = false;
	websocket = false;
	havekey = true;
	version = 0;
	for (;;) {
		if (!fgets(buf, sizeof buf, rd)) {
			if (ferror(rd))
				perror("read");
			goto fail;
		}
		if (http_header(buf, strlen(buf), &hdr) != 0)
			goto fail;
		if (!hdr.name)
			break;
		if (strcmp(hdr.name, "Upgrade") == 0) {
			for (token = strtok(hdr.value, " \t,"); token; token = strtok(NULL, " \t,")) {
				if (strcmp(token, "websocket") == 0) {
					websocket = true;
					break;
				}
			}
		} else if (strcmp(hdr.name, "Connection") == 0) {
			for (token = strtok(hdr.value, " \t,"); token; token = strtok(NULL, " \t,")) {
				if (strcmp(token, "Upgrade") == 0) {
					upgrade = true;
					break;
				}
			}
		} else if (strcmp(hdr.name, "Sec-WebSocket-Key") == 0) {
			static const char guid[36] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
			sha1_context ctx;

			if (hdr.value_len != (16 + 2) / 3 * 4)
				goto fail;
			sha1_init(&ctx);
			sha1_update(&ctx, hdr.value, hdr.value_len);
			sha1_update(&ctx, guid, sizeof guid);
			sha1_out(&ctx, sha1);
			base64_encode(accept, sha1, sizeof sha1);
			havekey = true;
		} else if (strcmp(hdr.name, "Sec-WebSocket-Version") == 0) {
			char *end;

			version = strtol(hdr.value, &end, 10);
			if (hdr.value_len == 0 || hdr.value_len != end - hdr.value)
				goto fail;
		}
	}

	if (!upgrade || !websocket || !havekey || version != 13)
		goto fail;
	fwrite(response, 1, sizeof response - 1, wr);
	fprintf(wr, "Sec-WebSocket-Accept: %s\r\n\r\n", accept);
	fflush(wr);
	return;

fail:
	http_error(wr, 400, "Bad Request", NULL, 0);
	exit(1);
}

static void
writeframe(FILE *wr, int op, const void *buf, size_t len)
{
	unsigned char hdr[10];
	int hdrlen;

	hdr[0] = 0x80 | op;
	if (len < 126) {
		hdr[1] = len;
		hdrlen = 2;
	} else if (len <= 0xffff) {
		hdr[1] = 126;
		putbe16(hdr + 2, len);
		hdrlen = 4;
	} else {
		hdr[1] = 127;
		putbe64(hdr + 2, len);
		hdrlen = 10;
	}
	flockfile(wr);
	if (!closing) {
		if (fwrite(hdr, 1, hdrlen, wr) != hdrlen || fwrite(buf, 1, len, wr) != len || fflush(wr) != 0) {
			perror("write:");
			exit(1);
		}
	}
	funlockfile(wr);
}

static void
writeclose(FILE *wr, int code)
{
	unsigned char buf[2];

	if (!closing) {
		putbe16(buf, code);
		flockfile(wr);
		writeframe(wr, CLOSE, buf, sizeof buf);
		closing = true;
		funlockfile(wr);
	}
}

static void
writer(FILE *wr)
{
	unsigned char buf[4096];
	ssize_t ret;

	for (;;) {
		ret = read(rfd, buf, sizeof buf);
		if (ret < 0) {
			perror("read:");
			writeclose(wr, 1011);
			break;
		}
		if (ret == 0) {
			writeclose(wr, 1001);
			break;
		}
		writeframe(wr, 2, buf, ret);
	}
}

static void *
writermain(void *arg)
{
	writer(arg);
	return NULL;
}

static void
unmask(unsigned char *buf, size_t len, unsigned char key[static 4])
{
	size_t i;

	for (i = 0; i < len; ++i)
		buf[i] ^= key[i & 3];
}

static void
reader(FILE *rd, FILE *wr)
{
	unsigned char hdr[2], lenbuf[8], key[4], ctl[125], msg[4096];
	int msglen;
	unsigned long long len;
	bool masked, skip;

	skip = false;
	msglen = 0;
	for (;;) {
		if (fread(hdr, 1, sizeof hdr, rd) != sizeof hdr)
			break;
		len = hdr[1] & 0x7f;
		if (len == 126) {
			if (fread(lenbuf, 1, 2, rd) != 2)
				break;
			len = getbe16(lenbuf);
		} else if (len == 127) {
			if (fread(lenbuf, 1, 8, rd) != 8)
				break;
			len = getbe64(lenbuf);
		}
		masked = hdr[1] & 0x80;
		if (masked && fread(key, 1, sizeof key, rd) != sizeof key)
			break;
		if (hdr[0] & 0x8) {
			/* control frame */
			if ((hdr[0] & 0x80) == 0 || len >= 126) {
				writeclose(wr, 1002);
				exit(1);
			}
			if (fread(ctl, 1, len, rd) != len)
				break;
			if (masked)
				unmask(ctl, len, key);
			switch (hdr[0] & 0xf) {
			case CLOSE:
				flockfile(wr);
				writeframe(wr, CLOSE, ctl, len > 2 ? 2 : len);
				exit(0);
				break;
			case PING:
				writeframe(wr, PONG, ctl, len);
				break;
			case PONG:
				/* ignore */
				break;
			}
		} else {
			if (!skip && sizeof msg - msglen < len) {
				fprintf(stderr, "message is too big\n");
				writeclose(wr, 1009);
				skip = true;
			}
			if (skip) {
				msglen = 0;
				while (len > sizeof msg) {
					if (fread(msg, 1, sizeof msg, rd) != sizeof msg)
						goto done;
					len -= sizeof msg;
				}
			}
			if (fread(msg + msglen, 1, len, rd) != len)
				break;
			msglen += len;
			if (hdr[0] & 0x80) {
				ssize_t ret;

				if (skip) {
					skip = false;
					continue;
				}
				if (masked)
					unmask(msg, msglen, key);
				ret = write(wfd, msg, msglen);
				if (ret <= 0) {
					if (errno != ECONNREFUSED) {
						perror("write:");
						writeclose(wr, 1011);
					}
				} else if (ret != msglen) {
					fprintf(stderr, "write: %zd != %d\n", ret, msglen);
					writeclose(wr, 1011);
				}
				msglen = 0;
			}
		}
	}
done:
	if (ferror(rd)) {
		perror("fread");
		exit(1);
	}
	exit(0);
}

int
main(int argc, char *argv[])
{
	static char mcastaddr[] = "udp!224.0.0.1!8222";
	static char defrecvaddr[] = "udp!127.0.0.1!8222";
	static char defsendaddr[] = "udp!127.0.0.1!7222";
	int err;
	char *recvaddr, *sendaddr;
	pthread_t thread;

	recvaddr = defrecvaddr;
	sendaddr = defsendaddr;

	ARGBEGIN {
	case 'r':
		recvaddr = EARGF(usage());
		break;
	case 's':
		sendaddr = EARGF(usage());
		break;
	case 'm':
		recvaddr = mcastaddr;
		break;
	} ARGEND;

	if (argc != 0)
		usage();

	rfd = sockopen(recvaddr, 1);
	wfd = sockopen(sendaddr, 0);

	handshake(stdin, stdout);
	err = pthread_create(&thread, NULL, writermain, stdout);
	if (err)
		fatal("pthread_create: %s", strerror(errno));
	reader(stdin, stdout);
}
