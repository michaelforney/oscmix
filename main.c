#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/time.h>
#include "arg.h"
#include "socket.h"
#include "util.h"

void init(void);
void *midiread(void *);
void *oscread(void *);
void timer(void);
void refresh(void);
extern int dflag, lflag;

static void
usage(void)
{
	fprintf(stderr, "usage: oscmix [-dlm] [-r addr] [-s addr]\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	static char defrecvaddr[] = "udp!127.0.0.1!7222";
	static char defsendaddr[] = "udp!127.0.0.1!8222";
	static char mcastaddr[] = "udp!224.0.0.1!8222";
	int err, sig, rfd, wfd;
	char *recvaddr, *sendaddr;
	pthread_t midireader, oscreader;
	struct itimerval it;
	sigset_t set;

	if (fcntl(6, F_GETFD) < 0)
		fatal("fcntl 6:");
	if (fcntl(7, F_GETFD) < 0)
		fatal("fcntl 7:");

	recvaddr = defrecvaddr;
	sendaddr = defsendaddr;

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
	default:
		usage();
		break;
	} ARGEND

	rfd = sockopen(recvaddr, 1);
	wfd = sockopen(sendaddr, 0);

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

	refresh();
	for (;;) {
		sigwait(&set, &sig);
		timer();
	}
}
