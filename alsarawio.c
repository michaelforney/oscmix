#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sound/asound.h>
#include "arg.h"

static void
usage(void)
{
	fprintf(stderr, "usage: alsarawio cmd [arg...]\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	int fd, ctlfd, ver, card, dev, subdev;
	long val;
	char path[256], *arg, *end;
	struct snd_rawmidi_params params;
	struct snd_rawmidi_info info;

	ARGBEGIN {
	default:
		usage();
	} ARGEND
	if (argc < 2)
		usage();
	arg = argv[0];
	val = strtol(arg, &end, 10);
	if (val < 0 || val > INT_MAX || !*arg || (*end && *end != ','))
		usage();
	card = val;
	dev = 0;
	subdev = 0;
	if (*end == ',') {
		arg = end + 1;
		val = strtol(arg, &end, 10);
		if (val < 0 || val > INT_MAX || !*arg || (*end && *end != ','))
			usage();
		dev = val;
		if (*end == ',') {
			arg = end + 1;
			val = strtol(arg, &end, 10);
			if (val < 0 || val > INT_MAX || !*arg || *end)
				usage();
			subdev = val;
		}
	}

	snprintf(path, sizeof path, "/dev/snd/controlC%d", card);
	ctlfd = open(path, O_RDWR | O_CLOEXEC);
	if (ctlfd < 0) {
		fprintf(stderr, "open %s: %s\n", path, strerror(errno));
		return 1;
	}
	if (ioctl(ctlfd, SNDRV_CTL_IOCTL_RAWMIDI_PREFER_SUBDEVICE, &(int){subdev}) != 0) {
		perror("ioctl SNDRV_CTL_IOCTL_RAWMIDI_PREFER_SUBDEVICE");
		return 1;
	}

	snprintf(path, sizeof path, "/dev/snd/midiC%dD%d", card, dev);
	fd = open(path, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "open %s: %s\n", path, strerror(errno));
		return 1;
	}
	if (ioctl(fd, (int)SNDRV_RAWMIDI_IOCTL_PVERSION, &ver) != 0) {
		perror("ioctl SNDRV_RAWMIDI_IOCTL_PVERSION");
		return 1;
	}
	if (SNDRV_PROTOCOL_INCOMPATIBLE(ver, SNDRV_RAWMIDI_VERSION)) {
		perror("incompatible rawmidi version");
		return 1;
	}
	info.stream = SNDRV_RAWMIDI_STREAM_INPUT;
	if (ioctl(fd, (int)SNDRV_RAWMIDI_IOCTL_INFO, &info) != 0) {
		perror("ioctl SNDRV_RAWMIDI_IOCTL_INFO");
		return 1;
	}
	if (info.subdevice != subdev) {
		fprintf(stderr, "could not open subdevice %d\n", subdev);
		return 1;
	}
	setenv("MIDIPORT", (char *)info.subname, 1);

	memset(&params, 0, sizeof params);
	params.stream = SNDRV_RAWMIDI_STREAM_INPUT;
	params.buffer_size = 8192;
	params.avail_min = 1;
	params.no_active_sensing = 1;
	if (ioctl(fd, (int)SNDRV_RAWMIDI_IOCTL_PARAMS, &params) != 0) {
		perror("ioctl SNDRV_RAWMIDI_IOCTL_PARAMS");
		return 1;
	}
	params.stream = SNDRV_RAWMIDI_STREAM_OUTPUT;
	if (ioctl(fd, (int)SNDRV_RAWMIDI_IOCTL_PARAMS, &params) != 0) {
		perror("ioctl SNDRV_RAWMIDI_IOCTL_PARAMS");
		return 1;
	}

	if (dup2(fd, 6) < 0 || dup2(fd, 7) < 0) {
		perror("dup2");
		return 1;
	}
	execvp(argv[1], argv + 1);
	perror("execvp");
	return 1;
}
