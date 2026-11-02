/* publish.c: see publish.h. */
#include <stdio.h>
#include <string.h>

#include "fmt.h"
#include "post.h"
#include "publish.h"

#define IS_DIGIT(c) ((c) >= '0' && (c) <= '9')

int pub_address(const char *s, unsigned long *ip, unsigned short *port)
{
	unsigned long addr = 0, v;
	int part, digits;

	if (strncmp(s, "http://", 7) == 0)
		s += 7;
	for (part = 0; part < 4; part++) {
		v = 0;
		digits = 0;
		while (IS_DIGIT(*s)) {
			v = v * 10 + (unsigned long) (*s - '0');
			if (++digits > 3 || v > 255)
				return 0;
			s++;
		}
		if (digits == 0)
			return 0;
		addr = (addr << 8) | v;
		if (part < 3 && *s++ != '.')
			return 0;
	}
	*port = PUB_PORT;
	if (*s == ':') {
		s++;
		v = 0;
		digits = 0;
		while (IS_DIGIT(*s)) {
			v = v * 10 + (unsigned long) (*s - '0');
			if (++digits > 5 || v > 65535)
				return 0;
			s++;
		}
		if (digits == 0 || v == 0)
			return 0;
		*port = (unsigned short) v;
	}
	if (*s == '/')
		s++;
	if (*s != '\0')
		return 0;
	*ip = addr;
	return 1;
}

int pub_bridge(const char *s, char *host, int cap, unsigned long *ip, unsigned short *port)
{
	const char *h, *e;
	unsigned long v = 0;
	int digits = 0, letters = 0, label = 0;

	if (pub_address(s, ip, port))
		return 1;
	if (strncmp(s, "http://", 7) == 0)
		s += 7;
	/* a host name: labels of letters, digits and hyphens, with dots between */
	for (h = s; *s && *s != ':' && *s != '/'; s++) {
		if (*s == '.') {
			if (label == 0)
				return 0;
			label = 0;
			continue;
		}
		if (!IS_DIGIT(*s) && *s != '-' && !((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z')))
			return 0;
		if (!IS_DIGIT(*s) && *s != '-')
			letters = 1;
		if (++label > 63)
			return 0;
	}
	e = s;
	if (label == 0 && e > h && e[-1] != '.')
		return 0;
	if (e == h || !letters || e - h >= cap)
		return 0;
	*port = PUB_PORT;
	if (*s == ':') {
		for (s++; IS_DIGIT(*s); s++) {
			v = v * 10 + (unsigned long) (*s - '0');
			if (++digits > 5 || v > 65535)
				return 0;
		}
		if (digits == 0 || v == 0)
			return 0;
		*port = (unsigned short) v;
	}
	if (*s == '/')
		s++;
	if (*s != '\0')
		return 0;
	memcpy(host, h, (size_t) (e - h));
	host[e - h] = '\0';
	if (host[e - h - 1] == '.')
		host[e - h - 1] = '\0';         /* a name ending in a dot is the same name */
	*ip = 0;
	return 2;
}

/* The bridge's rule for a name: [A-Za-z0-9][A-Za-z0-9._-]*.md (or .markdown), no ".." */
static int NameOk(const char *n)
{
	size_t len = strlen(n), i;
	const char *dot;

	if (len == 0 || len > 128 || strstr(n, "..") != NULL)
		return 0;
	for (i = 0; i < len; i++) {
		char c = n[i];
		int alnum = IS_DIGIT(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
		if (!alnum && (i == 0 || (c != '.' && c != '_' && c != '-')))
			return 0;
	}
	dot = strrchr(n, '.');
	return dot != NULL && dot != n && (strcmp(dot, ".md") == 0 || strcmp(dot, ".markdown") == 0);
}

/* "2026-10-23" at the start of s */
static int Ymd(const char *s)
{
	int i;

	for (i = 0; i < 10; i++)
		if (i == 4 || i == 7 ? s[i] != '-' : !IS_DIGIT(s[i]))
			return 0;
	return !IS_DIGIT(s[10]);
}

/* The n digits at s */
static int Number(const char *s, int n)
{
	int v = 0;

	while (n-- > 0)
		v = v * 10 + (*s++ - '0');
	return v;
}

/* A Jekyll post's name: "2026-10-23-..." */
static int Dated(const char *n)
{
	return Ymd(n) && n[10] == '-';
}

int pub_path_years(const char *path)
{
	int i;

	if (strncmp(path, "_posts/", 7) != 0)
		return 0;
	for (i = 7; i < 11; i++)
		if (!IS_DIGIT(path[i]))
			return 0;
	return path[11] == '/' && Ymd(path + 12) && strncmp(path + 7, path + 12, 4) == 0;
}

int pub_path_ok(const char *path)
{
	static const char *const dirs[] = { "_posts/", "_drafts/", "content/posts/" };
	int i;

	if (pub_path_years(path))
		return strchr(path + 12, '/') == NULL && NameOk(path + 12) && Dated(path + 12);
	for (i = 0; i < 3; i++) {
		size_t k = strlen(dirs[i]);
		if (strncmp(path, dirs[i], k) == 0)
			return strchr(path + k, '/') == NULL && NameOk(path + k) && (i != 0 || Dated(path + k));
	}
	return 0;
}

void pub_path(int hugo, int draft, int years, const char *name, const char *title, const char *date, char *out,
		int cap)
{
	const char *dir = hugo ? "content/posts/" : draft ? "_drafts/" : "_posts/";
	char file[96], year[8];
	int y = 0, m = 0, d = 0;

	if (NameOk(name) && strlen(name) < sizeof(file) && (hugo || draft || Dated(name))) {
		strcpy(file, name);
		if (draft && !hugo && Dated(file))
			memmove(file, file + 11, strlen(file + 11) + 1);
	} else {
		if (!draft && !hugo && Ymd(date)) {
			y = Number(date, 4);
			m = Number(date + 5, 2);
			d = Number(date + 8, 2);
		}
		post_filename(y, m, d, title, 0, file, (int) sizeof(file));
	}
	year[0] = '\0';
	if (years && !hugo && !draft && Dated(file)) {
		memcpy(year, file, 4);
		strcpy(year + 4, "/");
	}
	if (cap > 0) {
		strncpy(out, dir, (size_t) cap - 1);
		out[cap - 1] = '\0';
		strncat(out, year, (size_t) cap - 1 - strlen(out));
		strncat(out, file, (size_t) cap - 1 - strlen(out));
	}
}

long pub_request(char *out, long cap, const char *host, unsigned short port, const char *key, const char *path,
		long bodyLen)
{
	char head[512];
	long n;

	if (strlen(host) + strlen(key) + strlen(path) > sizeof(head) - 160)
		return -1;
	tp_sprintf(head, "POST /publish HTTP/1.0\r\nHost: %s:%u\r\nUser-Agent: Topica\r\nConnection: close\r\n"
			"Content-Type: text/markdown; charset=utf-8\r\nX-Topica-Key: %s\r\nX-Topica-Path: %s\r\n"
			"Content-Length: %ld\r\n\r\n", host, (unsigned) port, key, path, bodyLen);
	n = (long) strlen(head);
	if (n >= cap)
		return -1;
	memcpy(out, head, (size_t) n + 1);
	return n;
}

static int Lower(int c)
{
	return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

/* A header line's value, if the line in [p, e) is "name: value" */
static const char *Header(const char *p, const char *e, const char *name)
{
	size_t k = strlen(name), i;

	if ((size_t) (e - p) <= k || p[k] != ':')
		return NULL;
	for (i = 0; i < k; i++)
		if (Lower((unsigned char) p[i]) != name[i])
			return NULL;
	p += k + 1;
	while (p < e && *p == ' ')
		p++;
	return p;
}

int pub_reply(const char *buf, long len, int closed, int *status, char *msg, int cap)
{
	const char *p = buf, *end = buf + len, *line, *body = NULL, *reason;
	long clen = -1;
	int i, n;

	if (len >= 5 && strncmp(buf, "HTTP/", 5) != 0)
		return -1;
	/* the head ends at a blank line (CRLF or LF) */
	for (line = buf; line < end; line++)
		if (*line == '\n' && ((line + 1 < end && line[1] == '\n') ||
				(line + 2 < end && line[1] == '\r' && line[2] == '\n'))) {
			body = line + (line[1] == '\n' ? 2 : 3);
			break;
		}
	if (body == NULL)
		return closed ? -1 : 0;
	/* "HTTP/1.0 200 OK" */
	while (p < body && *p != ' ')
		p++;
	if (p + 4 > body || !IS_DIGIT(p[1]) || !IS_DIGIT(p[2]) || !IS_DIGIT(p[3]))
		return -1;
	*status = (p[1] - '0') * 100 + (p[2] - '0') * 10 + (p[3] - '0');
	reason = p + 4 < body && p[4] == ' ' ? p + 5 : p + 4;
	for (p = buf; p < body; p = line + 1) {
		const char *v;
		for (line = p; line < body && *line != '\n'; line++)
			;
		if ((v = Header(p, line, "content-length")) != NULL && IS_DIGIT(*v)) {
			clen = 0;
			while (IS_DIGIT(*v) && clen < 10000000L)
				clen = clen * 10 + (*v++ - '0');
		}
	}
	if (!closed && (clen < 0 || end - body < clen))
		return 0;
	if (clen >= 0 && end - body > clen)
		end = body + clen;
	/* the text's first line, or else the reason phrase */
	if (body == end || *body == '\r' || *body == '\n')
		body = reason;
	n = 0;
	for (i = 0; body + i < end && body[i] != '\r' && body[i] != '\n' && n < cap - 1; i++)
		msg[n++] = (char) ((unsigned char) body[i] >= 32 && (unsigned char) body[i] < 127 ? body[i] : '?');
	if (cap > 0)
		msg[n] = '\0';
	return 1;
}
