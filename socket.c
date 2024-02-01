#define _GNU_SOURCE
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <unistd.h>
#include "socket.h"
#include "util.h"

int
sockopen(char *addr, int passive)
{
	struct addrinfo hint;
	char *type, *port, *sep, *end;
	struct addrinfo *ais, *ai;
	int err, sock;
	long val;

	val = strtol(addr, &end, 0);
	if (*addr && !*end && val >= 0 && val < INT_MAX)
		return val;

	type = addr;
	addr = NULL;
	port = NULL;
	sep = strchr(type, '!');
	if (sep) {
		*sep = '\0';
		addr = sep + 1;
		sep = strchr(addr, '!');
		if (sep) {
			*sep = '\0';
			port = sep + 1;
			if (*port == '\0')
				port = NULL;
		}
		if (*addr == '\0')
			addr = NULL;
	}
	sock = -1;
	if (strcmp(type, "udp") == 0) {
		memset(&hint, 0, sizeof hint);
		hint.ai_flags = passive ? AI_PASSIVE : 0;
		hint.ai_family = AF_UNSPEC;
		hint.ai_socktype = SOCK_DGRAM;
		hint.ai_protocol = IPPROTO_UDP;
		err = getaddrinfo(addr, port, &hint, &ais);
		if (err != 0)
			fatal("getaddrinfo: %s", gai_strerror(err));
		for (ai = ais; ai; ai = ai->ai_next) {
			sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
			if (sock >= 0) {
				if (passive) {
					union {
						struct ip_mreq v4;
						struct ipv6_mreq v6;
					} mreq;
					bool multicast;

					switch (ai->ai_family) {
					case AF_INET:
						mreq.v4.imr_multiaddr = ((struct sockaddr_in *)ai->ai_addr)->sin_addr;
						if ((((unsigned char *)&mreq.v4.imr_multiaddr)[0] & 0xf0) == 0xe0) {
							mreq.v4.imr_interface.s_addr = INADDR_ANY;
							if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq.v4, sizeof mreq.v4) != 0)
								fatal("setsockopt IP_ADD_MEMBERSHIP:");
							multicast = true;
						}
						break;
					case AF_INET6:
						mreq.v6.ipv6mr_multiaddr = ((struct sockaddr_in6 *)addr)->sin6_addr;
						if (mreq.v6.ipv6mr_multiaddr.s6_addr[0] == 0xff) {
							mreq.v6.ipv6mr_interface = 0;
							if (setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq.v6, sizeof mreq.v6) != 0)
								fatal("setsockopt IPV6_ADD_MEMBERSHIP:");
							multicast = true;
						}
						break;
					}

					if (multicast && setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) != 0)
						fatal("setsockopt SO_REUSEADDR:");
					if (bind(sock, ai->ai_addr, ai->ai_addrlen) == 0)
						break;
				} else if (connect(sock, ai->ai_addr, ai->ai_addrlen) == 0) {
					break;
				}
				close(sock);
				sock = -1;
			}
		}
		freeaddrinfo(ais);
		if (sock == -1)
			fatal("connect:");
	} else {
		fatal("unsupported address type '%s'", type);
	}
	return sock;
}
