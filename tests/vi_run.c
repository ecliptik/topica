/*
 * vi_run: core/vi.c over a plain buffer, for tests/vi_oracle.py, which
 * compares it with vim. Reads the text from a file, a cursor offset and
 * keys; prints the cursor offset, a line with the mode, then the text.
 *
 *     vi_run TEXTFILE OFFSET KEYSFILE
 *
 * j and k move by lines here (the app moves by the lines on the screen).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/undo.h"
#include "../core/vi.h"

static char gText[65536];
static long gLen, gSelStart, gSelEnd;
static UndoStack gUndo;
static char gUndoBuf[65536];

static const char *Text(void *ctx, long *len)
{
	(void) ctx;
	*len = gLen;
	return gText;
}

static void GetSel(void *ctx, long *s, long *e)
{
	(void) ctx;
	*s = gSelStart;
	*e = gSelEnd;
}

static void SetSel(void *ctx, long s, long e)
{
	(void) ctx;
	gSelStart = s;
	gSelEnd = e;
}

static void Apply(long s, long e, const char *ins, long len)
{
	memmove(gText + s + len, gText + e, (size_t) (gLen - e));
	memcpy(gText + s, ins, (size_t) len);
	gLen += len - (e - s);
}

static int Replace(void *ctx, int kind, long s, long e, const char *ins, long len)
{
	(void) ctx;
	if (gLen - (e - s) + len > (long) sizeof(gText))
		return 0;
	undo_record(&gUndo, kind == VI_TYPE ? kUndoTyping : kind == VI_DELETE ? kUndoDelete : kUndoReplace,
		s, gText + s, e - s, ins, len);
	Apply(s, e, ins, len);
	gSelStart = gSelEnd = s + len;
	return 1;
}

static long LineStart(long p)
{
	while (p > 0 && gText[p - 1] != '\n' && gText[p - 1] != '\r')
		p--;
	return p;
}

static long LineEnd(long p)
{
	while (p < gLen && gText[p] != '\n' && gText[p] != '\r')
		p++;
	return p;
}

/* Lines are screen lines here: the goal is a column */
static long Vertical(void *ctx, long pos, int lines, long *goal)
{
	long ls = LineStart(pos), col;

	(void) ctx;
	if (*goal < 0)
		*goal = pos - ls;
	col = *goal;
	while (lines > 0 && LineEnd(ls) < gLen) {
		ls = LineEnd(ls) + 1;
		lines--;
	}
	while (lines < 0 && ls > 0) {
		ls = LineStart(ls - 1);
		lines++;
	}
	return ls + col < LineEnd(ls) ? ls + col : LineEnd(ls) > ls ? LineEnd(ls) - 1 : ls;
}

static void Undo(void *ctx, int redo)
{
	UndoApply a;

	(void) ctx;
	do {
		if (!undo_step(&gUndo, redo, &a))
			return;
		Apply(a.pos, a.pos + a.removeLen, a.insert, a.insertLen);
		gSelStart = gSelEnd = a.selStart;
	} while (a.more);
}

static void Group(void *ctx, int begin)
{
	(void) ctx;
	undo_group(&gUndo, begin);
}

static void Beep(void *ctx)
{
	(void) ctx;
}

/* Registers a to z, 4K each */
static ViReg *Named(void *ctx, int name)
{
	static ViReg regs[26];
	static char bufs[26][4096];
	ViReg *r = &regs[name - 'a'];

	(void) ctx;
	if (r->buf == NULL) {
		r->buf = bufs[name - 'a'];
		r->cap = sizeof(bufs[0]);
	}
	return r;
}

int main(int argc, char **argv)
{
	static char keys[4096], regBuf[65536];
	ViReg reg;
	FILE *f;
	long nk, i;
	Vi v;
	ViHost h;

	if (argc != 4)
		return 2;
	f = fopen(argv[1], "rb");
	gLen = (long) fread(gText, 1, sizeof(gText) / 2, f);
	fclose(f);
	gSelStart = gSelEnd = atol(argv[2]);
	f = fopen(argv[3], "rb");
	nk = (long) fread(keys, 1, sizeof(keys), f);
	fclose(f);
	undo_init(&gUndo, gUndoBuf, sizeof(gUndoBuf));
	memset(&h, 0, sizeof(h));
	h.text = Text;
	h.get_sel = GetSel;
	h.set_sel = SetSel;
	h.replace = Replace;
	h.vertical = Vertical;
	h.undo = Undo;
	h.group = Group;
	h.beep = Beep;
	h.named = Named;
	reg.buf = regBuf;
	reg.cap = sizeof(regBuf);
	reg.len = 0;
	reg.lines = 0;
	vi_init(&v, &reg);
	v.on = 1;
	for (i = 0; i < nk; i++)
		(void) vi_key(&v, &h, (unsigned char) keys[i], (unsigned long) i * 100);
	printf("%ld\n%d\n", gSelStart, v.mode);
	fwrite(gText, 1, (size_t) gLen, stdout);
	return 0;
}
