/* imgpath.c: see imgpath.h. */
#include <string.h>

#include "imgpath.h"
#include "textconv.h"

enum { kMaxParts = 32, kMaxName = 31, kMaxPath = 255 };

static int Hex(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

int ip_path(const char *url, long n, char *out, int cap)
{
	char dec[512], mac[512];
	const char *s;
	long len, i, k;
	int kind, ups = 0, parts = 0, o;
	long start[kMaxParts], plen[kMaxParts];

	if (cap > 0)
		out[0] = '\0';
	while (n > 0 && (*url == ' ' || *url == '<'))
		url++, n--;
	while (n > 0 && (url[n - 1] == ' ' || url[n - 1] == '>'))
		n--;
	if (n <= 0 || (n >= 2 && url[0] == '/' && url[1] == '/'))
		return IP_NONE;
	for (i = 0; i < n && url[i] != '/'; i++)
		if (url[i] == ':')
			return IP_NONE;         /* a scheme: http:, data:, mailto: */
	/* without the query and fragment, %-escapes decoded */
	for (i = k = 0; i < n && url[i] != '?' && url[i] != '#'; i++) {
		if (k >= (long) sizeof(dec) - 1)
			return IP_NONE;
		if (url[i] == '%' && i + 2 < n && Hex(url[i + 1]) >= 0 && Hex(url[i + 2]) >= 0) {
			dec[k++] = (char) (Hex(url[i + 1]) * 16 + Hex(url[i + 2]));
			i += 2;
		} else {
			dec[k++] = url[i];
		}
	}
	len = k;
	s = dec;
	for (i = 0; i < len && !(dec[i] & 0x80); i++)
		;
	if (i < len && tc_is_utf8(dec, len)) {
		len = tc_to_mac(dec, len, mac, (long) sizeof(mac), 1);
		if (len < 0)
			return IP_NONE;
		s = mac;
	}
	kind = len > 0 && s[0] == '/' ? IP_SITE : IP_HERE;
	/* the parts between slashes, . and .. worked out */
	for (i = 0; i <= len; ) {
		long e = i;
		while (e < len && s[e] != '/')
			e++;
		if (e - i == 2 && s[i] == '.' && s[i + 1] == '.') {
			if (parts > 0)
				parts--;
			else if (kind == IP_HERE)
				ups++;
		} else if (e > i && !(e - i == 1 && s[i] == '.')) {
			if (e - i > kMaxName || memchr(s + i, ':', (size_t) (e - i)) != NULL || parts == kMaxParts)
				return IP_NONE;
			start[parts] = i;
			plen[parts++] = e - i;
		}
		i = e + 1;
	}
	if (parts == 0)
		return IP_NONE;
	o = 1 + ups;
	for (k = 0; k < parts; k++)
		o += (int) plen[k] + (k > 0);
	if (o > kMaxPath || o >= cap)
		return IP_NONE;
	o = 0;
	out[o++] = ':';
	while (ups-- > 0)
		out[o++] = ':';
	for (k = 0; k < parts; k++) {
		if (k > 0)
			out[o++] = ':';
		memcpy(out + o, s + start[k], (size_t) plen[k]);
		o += (int) plen[k];
	}
	out[o] = '\0';
	return kind;
}

void ip_leaf(const char *path, char *out, int cap)
{
	const char *leaf = strrchr(path, ':');
	int len;

	leaf = leaf != NULL ? leaf + 1 : path;
	len = (int) strlen(leaf);
	if (cap < 2) {
		if (cap > 0)
			out[0] = '\0';
		return;
	}
	if (len > cap - 2)
		len = cap - 2;
	out[0] = ':';
	memcpy(out + 1, leaf, (size_t) len);
	out[len + 1] = '\0';
}

static int Lower(int c)
{
	return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

unsigned long ip_type(const char *path)
{
	static const struct { const char *ext; const char *type; } kTypes[] = {
		{ "jpg", "JPEG" }, { "jpeg", "JPEG" }, { "jpe", "JPEG" }, { "gif", "GIFf" },
		{ "png", "PNGf" }, { "pct", "PICT" }, { "pict", "PICT" }, { "pic", "PICT" },
		{ "bmp", "BMPf" }, { "tif", "TIFF" }, { "tiff", "TIFF" }, { "psd", "8BPS" }
	};
	const char *dot = strrchr(path, '.');
	char ext[8];
	int i, k;

	if (dot == NULL || strchr(dot, ':') != NULL || strlen(dot + 1) >= sizeof(ext))
		return 0;
	for (i = 0; dot[1 + i]; i++)
		ext[i] = (char) Lower((unsigned char) dot[1 + i]);
	ext[i] = '\0';
	for (k = 0; k < (int) (sizeof(kTypes) / sizeof(kTypes[0])); k++)
		if (strcmp(ext, kTypes[k].ext) == 0) {
			const unsigned char *t = (const unsigned char *) kTypes[k].type;
			return ((unsigned long) t[0] << 24) | ((unsigned long) t[1] << 16) | ((unsigned long) t[2] << 8) | t[3];
		}
	return 0;
}
