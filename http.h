/* SPDX-License-Identifier: ISC */
#ifndef HTTP_H
#define HTTP_H

struct http_request {
	enum {
		HTTP_GET,
		HTTP_POST,
		HTTP_MSEARCH,  /* M-SEARCH, used for SSDP */
	} method;
	char *uri;
};

struct http_header {
	char *name;
	size_t name_len;
	char *value;
	size_t value_len;
};

int http_request(char *, size_t, struct http_request *);
int http_header(char *, size_t, struct http_header *);
void http_error(FILE *, int, const char *, const char *[], size_t);

#endif
