#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>
#include "oscmix.h"
#include "arg.h"
#include "socket.h"
#include "util.h"

extern int dflag;
static int lflag;
static int rfd, wfd;

static void
usage(void)
{
	fprintf(stderr, "usage: oscmix [-dlm] [-r addr] [-s addr]\n");
	exit(1);
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
	return NULL;
}

static void *
oscread(void *arg)
{
	int fd;
	ssize_t ret;
	unsigned char buf[8192];

	fd = *(int *)arg;
	for (;;) {
		ret = read(fd, buf, sizeof buf);
		if (ret < 0) {
			perror("recv");
			break;
		}
		handleosc(buf, ret);
	}
	return NULL;
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

int
main(int argc, char *argv[])
{
	static char defrecvaddr[] = "udp!127.0.0.1!7222";
	static char defsendaddr[] = "udp!127.0.0.1!8222";
	static char mcastaddr[] = "udp!224.0.0.1!8222";
	static const unsigned char refreshosc[] = "/refresh\0\0\0\0,\0\0\0";
	int err, sig;
	char *recvaddr, *sendaddr;
	pthread_t midireader, oscreader;
	struct itimerval it;
	sigset_t set;
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

	sigfillset(&set);
	pthread_sigmask(SIG_SETMASK, &set, NULL);
	err = pthread_create(&midireader, NULL, midiread, &wfd);
	if (err)
		fatal("pthread_create: %s", strerror(err));
	err = pthread_create(&oscreader, NULL, oscread, &rfd);
	if (err)
		fatal("pthread_create: %s", strerror(err));

	sigemptyset(&set);
	sigaddset(&set, SIGALRM);
	pthread_sigmask(SIG_SETMASK, &set, NULL);
	it.it_interval.tv_sec = 0;
	it.it_interval.tv_usec = 100000;
	it.it_value = it.it_interval;
	if (setitimer(ITIMER_REAL, &it, NULL) != 0)
		fatal("setitimer:");

	handleosc(refreshosc, sizeof refreshosc - 1);
	for (;;) {
		sigwait(&set, &sig);
		handletimer(lflag == 0);
	}
}
