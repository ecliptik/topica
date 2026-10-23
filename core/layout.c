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

/* The height of the line's own characters (not ones that went on to the
   next line) */
static void Height(const LayFonts *f, const MpBlock *b, const unsigned char *sty, LayLine *line)
{
	long i;

	line->ascent = line->height = 0;
	Fit(f, mp_font(b, 0), line);
	for (i = line->start; i < line->start + line->len; i++)
		Fit(f, mp_font(b, sty[i]), line);
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
	int x = 0, xBrk = 0, code = Verbatim(b);

	line->width = 0;
	if (!code)
		while (start < n && text[start] == ' ')
			start++;
	line->start = start;
	for (i = start; i < n; i++) {
		unsigned char c = (unsigned char) text[i];
		int cw;
		if (c == MP_BREAK) {
			line->len = i - start;
			line->width = (short) x;
			Height(f, b, sty, line);
			return i + 1;
		}
		cw = f->width[mp_font(b, sty[i])][c];
		if (c == ' ' && !code) {
			/* a place to break: the line ends before the spaces */
			if (i == start || text[i - 1] != ' ') {
				brk = i;
				xBrk = x;
			}
		} else if (x + cw > w && i > start) {
			if (brk > start) {
				line->len = brk - start;
				line->width = (short) xBrk;
				Height(f, b, sty, line);
				next = brk;
				while (next < n && text[next] == ' ')
					next++;
				return next;
			}
			line->len = i - start;
			line->width = (short) x;
			Height(f, b, sty, line);
			return i;
		}
		x += cw;
		if (c == '-' || c == '/') {
			/* or after a hyphen or a slash, when the next character fits on */
			brk = i + 1;
			xBrk = x;
		}
	}
	line->len = n - start;
	if (!code)
		while (line->len > 0 && text[start + line->len - 1] == ' ') {
			line->len--;
			x -= f->width[mp_font(b, sty[start + line->len])][' '];
		}
	line->width = (short) x;
	Height(f, b, sty, line);
	return n;
}
