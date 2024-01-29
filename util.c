#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "util.h"

void
fatal(const char *msg, ...)
{
	va_list ap;

	va_start(ap, msg);
	vfprintf(stderr, msg, ap);
	va_end(ap);
	if (!msg) {
		perror(NULL);
	} else if (*msg && msg[strlen(msg) - 1] == ':') {
		fputc(' ', stderr);
		perror(NULL);
	} else {
		fputc('\n', stderr);
	}
	exit(1);
}
