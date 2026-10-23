/* layout.c: see layout.h. */
#include <stddef.h>

#include "layout.h"

static int Verbatim(const MpBlock *b)
{
	return b->kind == MP_CODE || b->kind == MP_TABLE || b->kind == MP_HTML;
}

/* The line's height so far, for one more font */
static void Fit(const LayFonts *f, int font, LayLine *line)
{
	short below = (short) (f->descent[font] + f->leading[font]);

	if (f->ascent[font] > line->ascent)
		line->ascent = f->ascent[font];
	if (line->ascent + below > line->height)
		line->height = (short) (line->ascent + below);
}

int lay_width(const LayFonts *f, const MpBlock *b, const char *text, const unsigned char *sty,
		long start, long len)
{
	long i;
	int w = 0;

	for (i = start; i < start + len; i++)
		w += f->width[mp_font(b, sty[i])][(unsigned char) text[i]];
	return w;
}

long lay_line(const LayFonts *f, const MpBlock *b, const char *text, const unsigned char *sty, long n,
		long start, int w, LayLine *line)
{
	long i, brk = -1, next;
	int x = 0, xBrk = 0, code = Verbatim(b), last = -1;
	short asc, ht, ascBrk = 0, htBrk = 0;   /* so far, and up to the break */
	short ascPrev = 0, htPrev = 0;
	long changed = -1;                      /* where the font last changed */
	const unsigned char *widths = NULL;

	if (!code)
		while (start < n && text[start] == ' ')
			start++;
	line->start = start;
	line->ascent = line->height = 0;
	Fit(f, mp_font(b, 0), line);
	asc = line->ascent;
	ht = line->height;
	for (i = start; i < n; i++) {
		unsigned char c = (unsigned char) text[i];
		int cw;
		if (c == MP_BREAK) {
			next = i + 1;
			break;
		}
		if (sty[i] != last) {
			int font = mp_font(b, sty[i]);
			last = sty[i];
			changed = i;
			ascPrev = asc;
			htPrev = ht;
			widths = f->width[font];
			line->ascent = asc;
			line->height = ht;
			Fit(f, font, line);
			asc = line->ascent;
			ht = line->height;
		}
		cw = widths[c];
		if (c == ' ' && !code) {
			/* a place to break: the line ends before the spaces */
			if (i == start || text[i - 1] != ' ') {
				brk = i;
				xBrk = x;
				ascBrk = asc;
				htBrk = ht;
			}
		} else if (x + cw > w && i > start) {
			if (brk > start) {
				line->len = brk - start;
				line->width = (short) xBrk;
				line->ascent = ascBrk;
				line->height = htBrk;
				next = brk;
				while (next < n && text[next] == ' ')
					next++;
				return next;
			}
			if (changed == i) {
				/* the character that changed the font goes on to the next line */
				asc = ascPrev;
				ht = htPrev;
			}
			next = i;
			break;
		}
		x += cw;
		if (c == '-' || c == '/') {
			/* or after a hyphen or a slash */
			brk = i + 1;
			xBrk = x;
			ascBrk = asc;
			htBrk = ht;
		}
	}
	if (i >= n) {
		next = n;
		if (!code)
			while (i > start && text[i - 1] == ' ') {
				i--;
				x -= f->width[mp_font(b, sty[i])][' '];
			}
	}
	line->len = i - start;
	line->width = (short) x;
	line->ascent = asc;
	line->height = ht;
	return next;
}
