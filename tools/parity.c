#include <stdlib.h>
#include <stdio.h>

static int
parity(unsigned long x)
{
	unsigned p;
	p = x ^ x >> 16;
	p ^= p >> 8;
	p ^= p >> 4;
	p ^= p >> 2;
	p ^= p >> 1;
	return (~p & 1) << 31 | x;
}

int
main(int argc, char *argv[])
{
	unsigned long x;

	x = strtoul(argv[1], NULL, 0);
	printf("%#.8lx\n", parity(x));
}
