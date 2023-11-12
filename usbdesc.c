#include <stdio.h>
#include <stdlib.h>
#include <libusb.h>

int
main(int argc, char *argv[])
{
	libusb_device_handle *dev;
	char str[1024];
	int ret;

	ret = libusb_init(NULL);
	if (ret) {
		fprintf(stderr, "libusb_init: %s\n", libusb_strerror(ret));
		return 1;
	}
	dev = libusb_open_device_with_vid_pid(NULL, 0x2a39, 0x3fd9);
	if (!dev) {
		fprintf(stderr, "libusb_open failed\n");
		return 1;
	}
	ret = libusb_get_string_descriptor_ascii(dev, strtoul(argv[1], NULL, 0), str, sizeof str);
	if (ret < 0) {
		fprintf(stderr, "libusb_get_string_descriptor %d: %s\n", 6, libusb_strerror(ret));
		return 1;
	}
	printf("%.*s\n", ret, str);
}
