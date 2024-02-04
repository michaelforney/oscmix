/* SPDX-License-Identifier: ISC */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "http.h"

int
http_request(char *buf, size_t len, struct http_request *req)
{
	char *method;

	if (len < 2 || buf[len - 2] != '\r' || buf[len - 1] != '\n')
		return -1;
	method = buf;
	buf = strchr(buf, ' ');
	if (!buf)
		return -1;
	*buf++ = '\0';
	if (strcmp(method, "GET") == 0) {
		req->method = HTTP_GET;
	} else if (strcmp(method, "POST") == 0) {
		req->method = HTTP_POST;
	} else if (strcmp(method, "M-SEARCH") == 0) {
		req->method = HTTP_MSEARCH;
	} else {
		return -1;
	}
	req->uri = buf;
	buf = strchr(buf, ' ');
	if (!buf)
		return -1;
	*buf++ = '\0';
	return 0;
}

int
http_header(char *buf, size_t len, struct http_header *hdr)
{
	char *end;

	if (len < 2 || buf[len - 2] != '\r' || buf[len - 1] != '\n')
		return -1;
	if (len == 2) {
		hdr->name = NULL;
		hdr->value = NULL;
		return 0;
	}
	end = buf + (len - 2);
	buf[len - 2] = '\0';
	hdr->name = buf;
	buf = strchr(buf, ':');
	if (!buf)
		return -1;
	hdr->name_len = buf - hdr->name;
	*buf = '\0';
	do ++buf;
	while (*buf == ' ' || *buf == '\t');
	hdr->value = buf;
	hdr->value_len = end - hdr->value;
	return 0;
}

void
http_error(FILE *fp, int code, const char *text, const char *hdr[], size_t hdr_len)
{
	assert(code >= 100 && code < 1000);
	fprintf(fp, "HTTP/1.1 %d %s\r\nContent-Type:text/plain\r\nContent-Length:%zu\r\n", code, text, 5 + strlen(text));
	while (hdr_len > 0) {
		--hdr_len;
		fprintf(fp, "%s\r\n", *hdr);
	}
	fprintf(fp, "\r\n%d %s\n", code, text);
}
