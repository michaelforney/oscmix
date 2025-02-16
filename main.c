#define _XOPEN_SOURCE 700  /* needed for SA_RESTART on FreeBSD */
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/time.h>
#include <unistd.h>
#include "oscmix.h"
#include "arg.h"
#include "socket.h"
#include "util.h"

extern int dflag;
static int lflag;
static int rfd, wfd;
static volatile sig_atomic_t timeout;

static void
usage(void)
{
	fprintf(stderr, "usage: oscmix [-dlm] [-r addr] [-s addr]\n");
	exit(1);
}

static void
midiread(int fd)
{
	static unsigned char data[8192], *dataend = data;
	unsigned char *datapos, *nextpos;
	uint_least32_t payload[sizeof data / 4];
	ssize_t ret;

	ret = read(fd, dataend, (data + sizeof data) - dataend);
	if (ret < 0)
		fatal("read %d:", fd);
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
		handlesysex(datapos, nextpos - datapos, payload);
		datapos = nextpos;
	}
}

static void
oscread(int fd)
{
	unsigned char buf[8192];
	ssize_t ret;

	ret = read(fd, buf, sizeof buf);
	if (ret < 0) {
		perror("recv");
		return;
	}
	handleosc(buf, ret);
}

void
writemidi(const void *buf, size_t len)
{
	const unsigned char *pos;
	ssize_t ret;

	pos = buf;
	while (len > 0) {
		ret = write(7, pos, len);
		if (ret < 0)
			fatal("write 7:");
		pos += ret;
		len -= ret;
	}
}

void
writeosc(const void *buf, size_t len)
{
	ssize_t ret;

	ret = write(wfd, buf, len);
	if (ret < 0) {
		if (errno != ECONNREFUSED)
			perror("write");
	} else if (ret != len) {
		fprintf(stderr, "write: %zd != %zu", ret, len);
	}
}

static void
sighandler(int sig)
{
	timeout = 1;
}

int
main(int argc, char *argv[])
{
	static char defrecvaddr[] = "udp!127.0.0.1!7222";
	static char defsendaddr[] = "udp!127.0.0.1!8222";
	static char mcastaddr[] = "udp!224.0.0.1!8222";
	static const unsigned char refreshosc[] = "/refresh\0\0\0\0,\0\0\0";
	char *recvaddr, *sendaddr;
	struct itimerval it;
	struct sigaction sa;
	struct pollfd pfd[2];
	const char *port;

	if (fcntl(6, F_GETFD) < 0)
		fatal("fcntl 6:");
	if (fcntl(7, F_GETFD) < 0)
		fatal("fcntl 7:");

	recvaddr = defrecvaddr;
	sendaddr = defsendaddr;
	port = NULL;

	ARGBEGIN {
	case 'd':
		dflag = 1;
		break;
	case 'l':
		lflag = 1;
		break;
	case 'r':
		recvaddr = EARGF(usage());
		break;
	case 's':
		sendaddr = EARGF(usage());
		break;
	case 'm':
		sendaddr = mcastaddr;
		break;
	case 'p':
		port = EARGF(usage());
		break;
	default:
		usage();
		break;
	} ARGEND

	rfd = sockopen(recvaddr, 1);
	wfd = sockopen(sendaddr, 0);

	if (!port) {
		port = getenv("MIDIPORT");
		if (!port)
			fatal("device is not specified; pass -p or set MIDIPORT");
	}
	if (init(port) != 0)
		return 1;

	memset(&sa, 0, sizeof sa);
	sa.sa_handler = sighandler;
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGALRM, &sa, NULL) != 0)
		fatal("sigaction:");
	it.it_interval.tv_sec = 0;
	it.it_interval.tv_usec = 100000;
	it.it_value = it.it_interval;
	if (setitimer(ITIMER_REAL, &it, NULL) != 0)
		fatal("setitimer:");

	pfd[0].fd = 6;
	pfd[0].events = POLLIN;
	pfd[1].fd = rfd;
	pfd[1].events = POLLIN;
	handleosc(refreshosc, sizeof refreshosc - 1);
	for (;;) {
		if (poll(pfd, 2, -1) < 0 && errno != EINTR)
			fatal("poll:");
		if (pfd[0].revents & POLLIN)
			midiread(6);
		if (pfd[1].revents & POLLIN)
			oscread(rfd);
		if (timeout) {
			timeout = 0;
			handletimer(lflag == 0);
		}
	}
}
