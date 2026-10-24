/* post.c: see post.h. */
#include <stdio.h>
#include <string.h>

#include "fmt.h"
#include "frontmatter.h"
#include "post.h"

/* Mac Roman 0x80-0xFF folded to ASCII for slugs ("" = a separator) */
static const char *const kFold[128] = {
	"a", "a", "c", "e", "n", "o", "u", "a", "a", "a", "a", "a", "a", "c", "e", "e",   /* 80 */
	"e", "e", "i", "i", "i", "i", "n", "o", "o", "o", "o", "o", "u", "u", "u", "u",   /* 90 */
	"", "", "", "", "", "", "", "ss", "", "", "", "", "", "", "ae", "o",              /* A0 */
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "ae", "o",                /* B0 */
	"", "", "", "", "f", "", "", "", "", "", "", "a", "a", "o", "oe", "oe",           /* C0 */
	"", "", "", "", "", "", "", "", "y", "y", "", "", "", "", "fi", "fl",             /* D0 */
	"", "", "", "", "", "a", "e", "a", "e", "e", "i", "i", "i", "i", "o", "o",        /* E0 */
	"", "o", "u", "u", "u", "i", "", "", "", "", "", "", "", "", "", ""               /* F0 */
};

void post_slug(const char *title, char *out, int cap)
{
	const unsigned char *p = (const unsigned char *) title;
	int n = 0, lastHyphen = -1, pendingSep = 0;

	if (cap <= 0)
		return;
	for (; *p; p++) {
		char buf[2];
		const char *add;
		unsigned char c = *p;

		if (c >= 'A' && c <= 'Z')
			c = (unsigned char) (c + 32);
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
			buf[0] = (char) c;
			buf[1] = '\0';
			add = buf;
		} else if (c == '\'' || c == 0xD5 || c == 0xD4) {
			continue;                   /* "don't" -> "dont" */
		} else if (c >= 0x80 && kFold[c - 0x80][0] != '\0') {
			add = kFold[c - 0x80];
		} else {
			pendingSep = n > 0;
			continue;
		}
		if (pendingSep) {
			if (n + 1 >= cap)
				break;
			lastHyphen = n;
			out[n++] = '-';
			pendingSep = 0;
		}
		if (n + (int) strlen(add) >= cap) {
			if (lastHyphen > 0)
				n = lastHyphen;         /* cut at the last whole word */
			break;
		}
		while (*add)
			out[n++] = *add++;
	}
	out[n] = '\0';
}

void post_filename(int year, int month, int day, const char *title, int maxLen, char *out, int cap)
{
	char slug[64];
	int room = (int) sizeof(slug);

	if (maxLen > 0) {
		room = maxLen - (year > 0 ? 11 : 0) - 3 + 1;    /* "YYYY-MM-DD-" and ".md" */
		if (room > (int) sizeof(slug))
			room = (int) sizeof(slug);
		if (room < 2)
			room = 2;
	}
	post_slug(title, slug, room);
	if (slug[0] == '\0') {
		strncpy(slug, "untitled", (size_t) room - 1);
		slug[room - 1] = '\0';
	}
	if (cap > 0) {
		char name[96];
		if (year > 0)
			tp_sprintf(name, "%04d-%02d-%02d-%s.md", year % 10000, month % 100, day % 100, slug);
		else
			tp_sprintf(name, "%s.md", slug);
		strncpy(out, name, (size_t) cap - 1);
		out[cap - 1] = '\0';
	}
}

long post_template(int hugo, const char *layout, const char *date, char eol, char *out, long cap)
{
	char buf[256];
	long n = 0;

	n += tp_sprintf(buf + n, "---%c", eol);
	if (hugo) {
		n += tp_sprintf(buf + n, "title:%c", eol);
		if (date != NULL && date[0] != '\0' && strlen(date) < 32)
			n += tp_sprintf(buf + n, "date: %s%c", date, eol);
		else
			n += tp_sprintf(buf + n, "date:%c", eol);
		n += tp_sprintf(buf + n, "draft: true%cdescription:%ccategories:%ctags:%c", eol, eol, eol, eol);
	} else {
		if (layout != NULL && layout[0] != '\0' && strlen(layout) < 64)
			n += tp_sprintf(buf + n, "layout: %s%c", layout, eol);
		n += tp_sprintf(buf + n, "title:%cdescription:%ccategory:%ctags:%c", eol, eol, eol, eol);
	}
	n += tp_sprintf(buf + n, "---%c%c", eol, eol);
	if (out != NULL) {
		if (n > cap)
			return -1;
		memcpy(out, buf, (size_t) n);
	}
	return n;
}

long post_fill(const char *tpl, long n, const char *date, char *out, long cap)
{
	static const char kDate[] = "{{date}}";
	long i = 0, len = 0, k = (long) sizeof(kDate) - 1, dl = (long) strlen(date);

	while (i < n) {
		const char *s = tpl + i;
		long sl = 1;
		if (n - i >= k && memcmp(s, kDate, (size_t) k) == 0) {
			s = date;
			sl = dl;
			i += k;
		} else {
			i++;
		}
		if (out != NULL) {
			if (len + sl > cap)
				return -1;
			memcpy(out + len, s, (size_t) sl);
		}
		len += sl;
	}
	return len;
}

/* A letter or digit in Mac Roman */
static int IsWordChar(unsigned char c)
{
	if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
		return 1;
	if (c < 0x80)
		return 0;
	return kFold[c - 0x80][0] != '\0';
}

long post_words(const char *text, long n)
{
	long start = 0, end = 0, i, words = 0;
	int in = 0;

	if (fm_find(text, n, &start, &end))
		start = end;
	for (i = start; i < n; i++) {
		unsigned char c = (unsigned char) text[i];
		int word = IsWordChar(c);
		if (word && !in)
			words++;
		in = word || (in && (c == '\'' || c == 0xD5 || c == '-'));
	}
	return words;
}

long post_minutes(long words)
{
	return words == 0 ? 0 : (words + 199) / 200;
}
