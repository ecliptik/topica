/* find.c: see find.h. */
#include <stddef.h>
#include <string.h>

#include "find.h"

/* Mac Roman's capital letters above 0x7F and their lowercase */
static const unsigned char kUpper[] = {
	0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0xAE, 0xAF, 0xCB, 0xCC, 0xCD, 0xCE, 0xD9,
	0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF1, 0xF2, 0xF3, 0xF4, 0
};
static const unsigned char kLower[] = {
	0x8A, 0x8C, 0x8D, 0x8E, 0x96, 0x9A, 0x9F, 0xBE, 0xBF, 0x88, 0x8B, 0x9B, 0xCF, 0xD8,
	0x89, 0x90, 0x87, 0x91, 0x8F, 0x92, 0x94, 0x95, 0x93, 0x97, 0x99, 0x98, 0x9C, 0x9E, 0x9D, 0
};

unsigned char find_lower(unsigned char c)
{
	int i;

	if (c >= 'A' && c <= 'Z')
		return (unsigned char) (c + 32);
	if (c < 0x80)
		return c;
	for (i = 0; kUpper[i] != 0; i++)
		if (kUpper[i] == c)
			return kLower[i];
	return c;
}

unsigned char find_upper(unsigned char c)
{
	int i;

	if (c >= 'a' && c <= 'z')
		return (unsigned char) (c - 32);
	if (c < 0x80)
		return c;
	for (i = 0; kLower[i] != 0; i++)
		if (kLower[i] == c)
			return kUpper[i];
	return c;
}

int find_is_word_char(unsigned char c)
{
	int i;

	if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')
		return 1;
	if (c >= 0x80 && c <= 0x9F)                 /* accented letters */
		return 1;
	if (c == 0xA7 || c == 0xBE || c == 0xBF || c == 0xCF || c == 0xD8 || c == 0xDE || c == 0xDF || c == 0xF5)
		return 1;
	for (i = 0; kUpper[i] != 0; i++)
		if (kUpper[i] == c)
			return 1;
	return 0;
}

int find_is_match(const char *text, long n, const char *pat, long plen, long at, int flags)
{
	const unsigned char *t = (const unsigned char *) text, *p = (const unsigned char *) pat;
	long i;

	if (plen <= 0 || at < 0 || at + plen > n)
		return 0;
	if (flags & kFindCase) {
		if (memcmp(t + at, p, (size_t) plen) != 0)
			return 0;
	} else {
		for (i = 0; i < plen; i++)
			if (find_lower(t[at + i]) != find_lower(p[i]))
				return 0;
	}
	if (flags & kFindWord) {
		if (at > 0 && find_is_word_char(t[at - 1]) && find_is_word_char(p[0]))
			return 0;
		if (at + plen < n && find_is_word_char(t[at + plen]) && find_is_word_char(p[plen - 1]))
			return 0;
	}
	return 1;
}

long find_next(const char *text, long n, const char *pat, long plen, long from, int flags)
{
	long i;

	if (plen <= 0 || plen > n)
		return -1;
	if (from < 0 || from > n)
		from = 0;
	for (i = from; i + plen <= n; i++)
		if (find_is_match(text, n, pat, plen, i, flags))
			return i;
	for (i = 0; i < from && i + plen <= n; i++)
		if (find_is_match(text, n, pat, plen, i, flags))
			return i;
	return -1;
}

long find_prev(const char *text, long n, const char *pat, long plen, long from, int flags)
{
	long i;

	if (plen <= 0 || plen > n)
		return -1;
	if (from < 0 || from > n)
		from = n;
	for (i = from - plen; i >= 0; i--)
		if (find_is_match(text, n, pat, plen, i, flags))
			return i;
	for (i = n - plen; i > from - plen && i >= 0; i--)
		if (find_is_match(text, n, pat, plen, i, flags))
			return i;
	return -1;
}

long find_replace_all(const char *text, long n, const char *pat, long plen, const char *rep, long rlen,
	int flags, char *out, long cap, long *count)
{
	long i = 0, o = 0;

	*count = 0;
	while (i < n) {
		if (plen > 0 && find_is_match(text, n, pat, plen, i, flags)) {
			if (out != NULL) {
				if (o + rlen > cap)
					return -1;
				memcpy(out + o, rep, (size_t) rlen);
			}
			o += rlen;
			i += plen;
			(*count)++;
		} else {
			if (out != NULL) {
				if (o + 1 > cap)
					return -1;
				out[o] = text[i];
			}
			o++;
			i++;
		}
	}
	return o;
}
