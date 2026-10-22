/* textconv.c: see textconv.h. */
#include <stddef.h>

#include "textconv.h"

/* Mac Roman 0x80-0xFF as Unicode: Apple's table, with 0xDB as the currency
   sign (U+00A4) as System 6 and 7's fonts draw it (the euro came in 8.5) */
static const unsigned short kRoman[128] = {
	0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1,  /* 80 */
	0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8,  /* 88 */
	0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3,  /* 90 */
	0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,  /* 98 */
	0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF,  /* A0 */
	0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,  /* A8 */
	0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211,  /* B0 */
	0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8,  /* B8 */
	0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB,  /* C0 */
	0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,  /* C8 */
	0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA,  /* D0 */
	0x00FF, 0x0178, 0x2044, 0x00A4, 0x2039, 0x203A, 0xFB01, 0xFB02,  /* D8 */
	0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1,  /* E0 */
	0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,  /* E8 */
	0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC,  /* F0 */
	0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7   /* F8 */
};

static const char kHex[] = "0123456789ABCDEF";

int tc_roman_from_cp(unsigned long cp)
{
	int i;

	if (cp < 0x80)
		return (int) cp;
	for (i = 0; i < 128; i++)
		if (kRoman[i] == cp)
			return 0x80 + i;
	return -1;
}

unsigned long tc_cp_from_roman(unsigned char c)
{
	return c < 0x80 ? c : kRoman[c - 0x80];
}

/* Decodes one UTF-8 sequence at in[0..n); returns its length, 0 if invalid. */
static int Utf8Next(const unsigned char *in, long n, unsigned long *cp)
{
	unsigned char c = in[0];
	int len, k;
	unsigned long v, min;

	if (c < 0x80) {
		*cp = c;
		return 1;
	}
	if (c >= 0xC2 && c <= 0xDF) {
		len = 2; v = c & 0x1F; min = 0x80;
	} else if (c >= 0xE0 && c <= 0xEF) {
		len = 3; v = c & 0x0F; min = 0x800;
	} else if (c >= 0xF0 && c <= 0xF4) {
		len = 4; v = c & 0x07; min = 0x10000;
	} else {
		return 0;
	}
	if (n < len)
		return 0;
	for (k = 1; k < len; k++) {
		if ((in[k] & 0xC0) != 0x80)
			return 0;
		v = (v << 6) | (in[k] & 0x3F);
	}
	if (v < min || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF))
		return 0;
	*cp = v;
	return len;
}

int tc_is_utf8(const char *in, long n)
{
	const unsigned char *p = (const unsigned char *) in;
	long i = 0;
	unsigned long cp;

	while (i < n) {
		int len = Utf8Next(p + i, n - i, &cp);
		if (len == 0)
			return 0;
		i += len;
	}
	return 1;
}

/* Appends one byte, or only counts it when out is NULL; -1 on overflow. */
#define PUT(b) do { if (out != NULL) { if (o >= cap) return -1; out[o] = (char) (b); } o++; } while (0)

long tc_to_mac(const char *in, long n, char *out, long cap, int utf8)
{
	const unsigned char *p = (const unsigned char *) in;
	long i = 0, o = 0;

	while (i < n) {
		unsigned long cp;
		int len, r, d;

		if (p[i] == '\r') {
			PUT('\r');
			i += (i + 1 < n && p[i + 1] == '\n') ? 2 : 1;
			continue;
		}
		if (p[i] == '\n') {
			PUT('\r');
			i++;
			continue;
		}
		if (!utf8 || p[i] < 0x80) {
			PUT(p[i]);
			i++;
			continue;
		}
		len = Utf8Next(p + i, n - i, &cp);
		if (len == 0) {                 /* the caller said UTF-8; keep the byte */
			PUT(p[i]);
			i++;
			continue;
		}
		i += len;
		if (cp == 0xFEFF && i == len)   /* a byte-order mark at the start */
			continue;
		r = tc_roman_from_cp(cp);
		if (r >= 0) {
			PUT(r);
			continue;
		}
		PUT('&'); PUT('#'); PUT('x');
		for (d = (cp > 0xFFFF) ? 20 : 12; d >= 0; d -= 4)
			PUT(kHex[(cp >> d) & 0xF]);
		PUT(';');
	}
	return o;
}

static int HexVal(unsigned char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return -1;
}

/* An entity "&#xHHHH;" at p[0..n) for a character Mac Roman lacks: its
   length, with the code point in *cp; 0 if it isn't one. */
static int GuardEntity(const unsigned char *p, long n, unsigned long *cp)
{
	unsigned long v = 0;
	int k = 3, h;

	if (n < 5 || p[0] != '&' || p[1] != '#' || (p[2] != 'x' && p[2] != 'X'))
		return 0;
	while (k < n && k < 3 + 6 && (h = HexVal(p[k])) >= 0) {
		v = (v << 4) | (unsigned long) h;
		k++;
	}
	if (k == 3 || k >= n || p[k] != ';')
		return 0;
	if (v < 0x80 || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF) || tc_roman_from_cp(v) >= 0)
		return 0;
	*cp = v;
	return k + 1;
}

long tc_to_disk(const char *in, long n, char *out, long cap, int utf8, int lf)
{
	const unsigned char *p = (const unsigned char *) in;
	long i = 0, o = 0;

	while (i < n) {
		unsigned long cp;
		int len;

		if (p[i] == '\r') {
			PUT(lf ? '\n' : '\r');
			i++;
			continue;
		}
		if (!utf8) {
			PUT(p[i]);
			i++;
			continue;
		}
		if (p[i] == '&' && (len = GuardEntity(p + i, n - i, &cp)) > 0) {
			i += len;
		} else {
			cp = tc_cp_from_roman(p[i]);
			i++;
		}
		if (cp < 0x80) {
			PUT(cp);
		} else if (cp < 0x800) {
			PUT(0xC0 | (cp >> 6));
			PUT(0x80 | (cp & 0x3F));
		} else if (cp < 0x10000) {
			PUT(0xE0 | (cp >> 12));
			PUT(0x80 | ((cp >> 6) & 0x3F));
			PUT(0x80 | (cp & 0x3F));
		} else {
			PUT(0xF0 | (cp >> 18));
			PUT(0x80 | ((cp >> 12) & 0x3F));
			PUT(0x80 | ((cp >> 6) & 0x3F));
			PUT(0x80 | (cp & 0x3F));
		}
	}
	return o;
}
