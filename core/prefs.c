/* prefs.c: see prefs.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "prefs.h"

void prefs_default(TopicaPrefs *p)
{
	memset(p, 0, sizeof(*p));
	strcpy(p->font, "Monaco");
	p->size = 9;
	strcpy(p->imagePath, "/assets/images/posts/");
	strcpy(p->layout, "post");
	p->profile = kProfileJekyll;
	p->utf8 = 1;
	p->lf = 1;
	p->autosaveMinutes = 2;
	p->format = 1;
}

static void Copy(char *dst, size_t cap, const char *src, long n)
{
	if (n > (long) cap - 1)
		n = (long) cap - 1;
	memcpy(dst, src, (size_t) n);
	dst[n] = '\0';
}

static int Is(const char *s, long n, const char *word)
{
	return (long) strlen(word) == n && memcmp(s, word, (size_t) n) == 0;
}

static short Number(const char *s, long n, short lo, short hi, short dflt)
{
	char buf[16];
	long v;
	char *end;

	if (n <= 0 || n >= (long) sizeof(buf))
		return dflt;
	memcpy(buf, s, (size_t) n);
	buf[n] = '\0';
	v = strtol(buf, &end, 10);
	if (*end != '\0' || v < lo || v > hi)
		return dflt;
	return (short) v;
}

void prefs_read(TopicaPrefs *p, const char *text, long n)
{
	long i = 0;

	while (i < n) {
		long ls = i, le, ks, ke, vs, ve;
		while (i < n && text[i] != '\r' && text[i] != '\n')
			i++;
		le = i;
		while (i < n && (text[i] == '\r' || text[i] == '\n'))
			i++;
		for (ke = ls; ke < le && text[ke] != ':'; ke++)
			;
		if (ke == le || text[ls] == '#')
			continue;
		ks = ls;
		vs = ke + 1;
		while (vs < le && text[vs] == ' ')
			vs++;
		ve = le;
		while (ve > vs && text[ve - 1] == ' ')
			ve--;
		if (Is(text + ks, ke - ks, "font") && ve > vs)
			Copy(p->font, sizeof(p->font), text + vs, ve - vs);
		else if (Is(text + ks, ke - ks, "size"))
			p->size = Number(text + vs, ve - vs, 6, 24, p->size);
		else if (Is(text + ks, ke - ks, "image-folder"))
			Copy(p->imagePath, sizeof(p->imagePath), text + vs, ve - vs);
		else if (Is(text + ks, ke - ks, "layout"))
			Copy(p->layout, sizeof(p->layout), text + vs, ve - vs);
		else if (Is(text + ks, ke - ks, "blog"))
			p->profile = Is(text + vs, ve - vs, "hugo") ? kProfileHugo : kProfileJekyll;
		else if (Is(text + ks, ke - ks, "encoding"))
			p->utf8 = !Is(text + vs, ve - vs, "mac-roman");
		else if (Is(text + ks, ke - ks, "line-ends"))
			p->lf = !Is(text + vs, ve - vs, "cr");
		else if (Is(text + ks, ke - ks, "autosave-minutes"))
			p->autosaveMinutes = Number(text + vs, ve - vs, 0, 60, p->autosaveMinutes);
		else if (Is(text + ks, ke - ks, "formatting"))
			p->format = !Is(text + vs, ve - vs, "off");
		else if (Is(text + ks, ke - ks, "vi"))
			p->vi = Is(text + vs, ve - vs, "on");
		else if (Is(text + ks, ke - ks, "vi-jk"))
			p->viJk = Is(text + vs, ve - vs, "on");
		else if (Is(text + ks, ke - ks, "publish-bridge"))
			Copy(p->bridge, sizeof(p->bridge), text + vs, ve - vs);
		else if (Is(text + ks, ke - ks, "publish-key"))
			Copy(p->bridgeKey, sizeof(p->bridgeKey), text + vs, ve - vs);
	}
}

long prefs_write(const TopicaPrefs *p, char eol, char *out, long cap)
{
	char buf[768];
	long n;

	n = sprintf(buf,
		"# Topica Preferences%c"
		"font: %s%c"
		"size: %d%c"
		"image-folder: %s%c"
		"layout: %s%c"
		"blog: %s%c"
		"encoding: %s%c"
		"line-ends: %s%c"
		"autosave-minutes: %d%c"
		"formatting: %s%c"
		"vi: %s%c"
		"vi-jk: %s%c"
		"publish-bridge: %s%c"
		"publish-key: %s%c",
		eol, p->font, eol, p->size, eol, p->imagePath, eol, p->layout, eol,
		p->profile == kProfileHugo ? "hugo" : "jekyll", eol,
		p->utf8 ? "utf-8" : "mac-roman", eol, p->lf ? "lf" : "cr", eol, p->autosaveMinutes, eol,
		p->format ? "on" : "off", eol, p->vi ? "on" : "off", eol, p->viJk ? "on" : "off", eol,
		p->bridge, eol, p->bridgeKey, eol);
	if (out != NULL) {
		if (n > cap)
			return -1;
		memcpy(out, buf, (size_t) n);
	}
	return n;
}
