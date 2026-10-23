/* vi.c: see vi.h. */
#include <stddef.h>
#include <string.h>

#include "vi.h"

/* How a motion takes text when an operator uses it */
enum { kExcl, kIncl, kLines };

/* jk's two keys within this many ticks */
enum { kJkTicks = 45 };

#define IsEol(c) ((c) == '\r' || (c) == '\n')
#define IsBlank(c) ((c) == ' ' || (c) == '\t')

typedef struct {
	const ViHost *h;
	const char *t;
	long n;
} Buf;

static void Load(Buf *b, const ViHost *h)
{
	b->h = h;
	b->t = h->text(h->ctx, &b->n);
}

static void Beep(const ViHost *h)
{
	if (h->beep != NULL)
		h->beep(h->ctx);
}

/* ---- lines ---- */

static long LineStart(const Buf *b, long p)
{
	while (p > 0 && !IsEol(b->t[p - 1]))
		p--;
	return p;
}

static long LineEnd(const Buf *b, long p)
{
	while (p < b->n && !IsEol(b->t[p]))
		p++;
	return p;
}

/* The start of the line after p's, or n */
static long NextLine(const Buf *b, long p)
{
	p = LineEnd(b, p);
	return p < b->n ? p + 1 : b->n;
}

static long FirstBlank(const Buf *b, long ls)
{
	long le = LineEnd(b, ls);

	while (ls < le && IsBlank(b->t[ls]))
		ls++;
	return ls;
}

static int EmptyLine(const Buf *b, long ls)
{
	return ls >= b->n || IsEol(b->t[ls]);
}

/* Where normal mode's cursor may be: on a character, not a line's end
   (unless the line is empty) */
static long Clamp(const Buf *b, long p)
{
	if (p > b->n)
		p = b->n;
	if (p < 0)
		p = 0;
	if ((p >= b->n || IsEol(b->t[p])) && p > LineStart(b, p))
		p--;
	return p;
}

/* The line's number from 0, and the start of line k */
static long LineOf(const Buf *b, long p)
{
	long i, k = 0;

	for (i = 0; i < p && i < b->n; i++)
		if (IsEol(b->t[i]))
			k++;
	return k;
}

static long LineAt(const Buf *b, long k)
{
	long p = 0;

	while (k-- > 0 && p < b->n)
		p = NextLine(b, p);
	if (p >= b->n && b->n > 0 && !IsEol(b->t[b->n - 1]))
		p = LineStart(b, b->n);
	return p;
}

/* ---- words ---- */

/* 0 blank, 1 word characters (letters, digits, _, Mac Roman's own), 2
   punctuation; big words: anything not blank is 1. A line end is blank. */
static int Kind(const Buf *b, long p, int big)
{
	unsigned char c = (unsigned char) b->t[p];

	if (IsBlank(c) || IsEol(c))
		return 0;
	if (big)
		return 1;
	if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c >= 0x80)
		return 1;
	return 2;
}

/* An empty line, which w and b stop at */
static int Empty(const Buf *b, long p)
{
	return p < b->n && IsEol(b->t[p]) && (p == 0 || IsEol(b->t[p - 1]));
}

static long WordForward(const Buf *b, long p, int big)
{
	int k;

	if (p >= b->n)
		return b->n;
	k = Kind(b, p, big);
	if (k != 0)
		while (p < b->n && Kind(b, p, big) == k)
			p++;
	else if (Empty(b, p))
		p++;
	while (p < b->n && Kind(b, p, big) == 0 && !Empty(b, p))
		p++;
	return p;
}

static long WordEnd(const Buf *b, long p, int big)
{
	int k;

	p++;
	while (p < b->n && Kind(b, p, big) == 0)
		p++;
	if (p >= b->n)
		return b->n > 0 ? b->n - 1 : 0;
	k = Kind(b, p, big);
	while (p + 1 < b->n && Kind(b, p + 1, big) == k)
		p++;
	return p;
}

static long WordBack(const Buf *b, long p, int big)
{
	int k;

	if (p <= 0)
		return 0;
	p--;
	while (p > 0 && Kind(b, p, big) == 0 && !Empty(b, p))
		p--;
	if (Empty(b, p))
		return p;
	k = Kind(b, p, big);
	while (p > 0 && Kind(b, p - 1, big) == k)
		p--;
	return p;
}

/* } and {: the next (or last) empty line after some text */
static long Paragraph(const Buf *b, long p, int forward)
{
	long ls = LineStart(b, p);
	int text = 0;

	if (forward) {
		for (; ls < b->n; ls = NextLine(b, ls)) {
			if (!EmptyLine(b, ls))
				text = 1;
			else if (text)
				return ls;
		}
		return b->n;
	}
	for (;;) {
		if (ls == 0)
			return 0;
		ls = LineStart(b, ls - 1);
		if (!EmptyLine(b, ls))
			text = 1;
		else if (text)
			return ls;
	}
}

/* f F t T: the character's place on the line, or -1 */
static long FindChar(const Buf *b, long p, int cmd, char c, long count)
{
	long ls = LineStart(b, p), le = LineEnd(b, p), q = p;

	if (cmd == 'f' || cmd == 't') {
		if (cmd == 't' && q + 1 < le && b->t[q + 1] == c && count == 1)
			q++;                    /* t again from just before it moves on */
		while (count-- > 0) {
			for (q++; q < le && b->t[q] != c; q++)
				;
			if (q >= le)
				return -1;
		}
		return cmd == 't' ? q - 1 : q;
	}
	if (cmd == 'T' && q - 1 > ls && b->t[q - 1] == c && count == 1)
		q--;
	while (count-- > 0) {
		for (q--; q >= ls && b->t[q] != c; q--)
			;
		if (q < ls)
			return -1;
	}
	return cmd == 'T' ? q + 1 : q;
}

/* ---- motions ---- */

typedef struct {
	long to;                    /* -1: it can't */
	int how;                    /* kExcl, kIncl, kLines */
	int keepGoal;
} Move;

/* A motion key (with the one pending before it, for g and f F t T):
   1 when c is a motion, filling m; op: an operator waits for it */
static int Motion(Vi *v, const Buf *b, int pend, int c, long count, long pos, int op, Move *m)
{
	long n1 = count > 0 ? count : 1, i, p = pos;

	m->to = -1;
	m->how = kExcl;
	m->keepGoal = 0;
	if (pend == 'f' || pend == 'F' || pend == 't' || pend == 'T') {
		v->findCmd = (char) pend;
		v->findCh = (char) c;
		m->to = FindChar(b, pos, pend, (char) c, n1);
		m->how = pend == 'f' || pend == 't' ? kIncl : kExcl;
		return 1;
	}
	if (pend == 'g') {
		if (c == 'g') {
			p = LineAt(b, count > 0 ? count - 1 : 0);
			m->to = op ? p : FirstBlank(b, p);
			m->how = kLines;
			return 1;
		}
		if (c == 'j' || c == 'k')
			pend = 0;                   /* gj gk: j and k move by screen lines already */
		else
			return 0;
	}
	switch (c) {
	case 'h':
	case VI_BACKSPACE:
		for (i = 0; i < n1 && p > LineStart(b, pos); i++)
			p--;
		m->to = p == pos ? -1 : p;
		return 1;
	case 'l':
	case ' ':
		for (i = 0; i < n1 && p < LineEnd(b, pos) - (op ? 0 : 1); i++)
			p++;
		m->to = p == pos && !(op && p < LineEnd(b, pos)) ? -1 : p;
		return 1;
	case 'j':
	case 'k':
		if (op || v->mode == VI_VISUAL_LINE) {
			p = LineStart(b, pos);
			for (i = 0; i < n1; i++) {
				if (c == 'j') {
					if (NextLine(b, p) >= b->n && LineEnd(b, p) >= b->n)
						break;
					p = NextLine(b, p);
				} else {
					if (p == 0)
						break;
					p = LineStart(b, p - 1);
				}
			}
			m->to = i == 0 ? -1 : p;
			m->how = kLines;
			return 1;
		}
		m->to = b->h->vertical(b->h->ctx, pos, (int) (c == 'j' ? n1 : -n1), &v->goal);
		m->keepGoal = 1;
		if (m->to == pos)
			m->to = -1;
		return 1;
	case '+':
	case VI_RETURN:
	case '-':
		p = LineStart(b, pos);
		for (i = 0; i < n1 && p >= 0; i++)
			p = c == '-' ? (p > 0 ? LineStart(b, p - 1) : -1) : (NextLine(b, p) < b->n ? NextLine(b, p) : -1);
		m->to = p < 0 ? -1 : FirstBlank(b, p);
		m->how = kLines;
		return 1;
	case 'w':
	case 'W':
		for (i = 0; i < n1; i++) {
			long q = WordForward(b, p, c == 'W');
			/* an operator stops at the end of the line, as vi's does */
			if (op && i == n1 - 1 && q > LineEnd(b, p) && LineEnd(b, p) > p)
				q = LineEnd(b, p);
			p = q;
		}
		m->to = p;
		return 1;
	case 'b':
	case 'B':
		for (i = 0; i < n1; i++)
			p = WordBack(b, p, c == 'B');
		m->to = p == pos ? -1 : p;
		return 1;
	case 'e':
	case 'E':
		for (i = 0; i < n1; i++)
			p = WordEnd(b, p, c == 'E');
		m->to = p;
		m->how = kIncl;
		return 1;
	case '0':
		m->to = LineStart(b, pos);
		return 1;
	case '^':
		m->to = FirstBlank(b, LineStart(b, pos));
		return 1;
	case '$':
		p = pos;
		for (i = 1; i < n1; i++)
			p = NextLine(b, p);
		p = LineEnd(b, p);
		m->to = op ? p : (p > LineStart(b, p) ? p - 1 : p);
		m->how = op ? kExcl : kIncl;
		m->keepGoal = 0;
		return 1;
	case 'G':
		p = count > 0 ? LineAt(b, count - 1) : LineStart(b, b->n);
		m->to = op ? p : FirstBlank(b, p);
		m->how = kLines;
		return 1;
	case '}':
	case '{':
		for (i = 0; i < n1; i++)
			p = Paragraph(b, p, c == '}');
		m->to = p;
		return 1;
	case ';':
	case ',':
		if (v->findCmd == 0)
			return 1;
		{
			int cmd = v->findCmd;
			if (c == ',')
				cmd = cmd == 'f' ? 'F' : cmd == 'F' ? 'f' : cmd == 't' ? 'T' : 't';
			m->to = FindChar(b, pos, cmd, v->findCh, n1);
			m->how = cmd == 'f' || cmd == 't' ? kIncl : kExcl;
		}
		return 1;
	}
	return 0;
}

/* ---- text objects ---- */

/* iw aw ip ap i" i( and the rest: [*s, *e), *lines when whole lines */
static int Object(const Buf *b, long pos, int inner, int c, long *s, long *e, int *lines)
{
	long p, q, ls, le;
	int k, depth;
	char open = 0, close = 0;

	*lines = 0;
	if (b->n == 0)
		return 0;
	if (pos >= b->n)
		pos = b->n - 1;
	switch (c) {
	case 'w':
	case 'W':
		k = Kind(b, pos, c == 'W');
		ls = LineStart(b, pos);
		le = LineEnd(b, pos);
		for (p = pos; p > ls && Kind(b, p - 1, c == 'W') == k; p--)
			;
		for (q = pos; q < le && Kind(b, q, c == 'W') == k; q++)
			;
		if (!inner) {
			long q2 = q;
			while (q2 < le && IsBlank(b->t[q2]))
				q2++;
			if (q2 > q && k != 0)
				q = q2;
			else
				while (p > ls && IsBlank(b->t[p - 1]))
					p--;
		}
		*s = p;
		*e = q;
		return q > p;
	case 'p':
		ls = LineStart(b, pos);
		k = EmptyLine(b, ls);
		for (p = ls; p > 0 && EmptyLine(b, LineStart(b, p - 1)) == k; p = LineStart(b, p - 1))
			;
		for (q = ls; q < b->n && EmptyLine(b, q) == k; q = NextLine(b, q))
			if (NextLine(b, q) == q)
				break;
		if (!inner)
			while (q < b->n && EmptyLine(b, q) && !k)
				q = NextLine(b, q);
		*s = p;
		*e = q > p ? q - 1 : p;     /* on the last line */
		*lines = 1;
		return 1;
	case '"':
	case '\'':
	case '`':
		ls = LineStart(b, pos);
		le = LineEnd(b, pos);
		/* the quote before the cursor (or under it) and the next one */
		for (p = pos; p >= ls && b->t[p] != c; p--)
			;
		if (p < ls) {
			for (p = pos; p < le && b->t[p] != c; p++)
				;
			if (p >= le)
				return 0;
		}
		for (q = p + 1; q < le && b->t[q] != c; q++)
			;
		if (q >= le) {
			/* the cursor was on the closing quote */
			q = p;
			for (p = q - 1; p >= ls && b->t[p] != c; p--)
				;
			if (p < ls)
				return 0;
		}
		*s = inner ? p + 1 : p;
		*e = inner ? q : q + 1;
		if (!inner)
			while (*e < le && IsBlank(b->t[*e]))
				(*e)++;
		return 1;
	case '(': case ')': case 'b':
		open = '(', close = ')';
		break;
	case '[': case ']':
		open = '[', close = ']';
		break;
	case '{': case '}': case 'B':
		open = '{', close = '}';
		break;
	case '<': case '>':
		open = '<', close = '>';
		break;
	default:
		return 0;
	}
	/* the bracket around the cursor, and its partner */
	depth = 0;
	for (p = pos; p >= 0; p--) {
		if (b->t[p] == close && p != pos)
			depth++;
		else if (b->t[p] == open && depth-- == 0)
			break;
	}
	if (p < 0)
		return 0;
	depth = 0;
	for (q = p + 1; q < b->n; q++) {
		if (b->t[q] == open)
			depth++;
		else if (b->t[q] == close && depth-- == 0)
			break;
	}
	if (q >= b->n)
		return 0;
	*s = inner ? p + 1 : p;
	*e = inner ? q : q + 1;
	if (inner && FirstBlank(b, LineStart(b, q)) == q && LineStart(b, q) - 1 > p + 1)
		*e = LineStart(b, q) - 1;   /* a close on a line of its own keeps its line */
	return 1;
}

/* ---- changes ---- */

static void Select(const ViHost *h, long s, long e)
{
	h->set_sel(h->ctx, s, e);
}

static long Caret(const ViHost *h)
{
	long s, e;

	h->get_sel(h->ctx, &s, &e);
	return s;
}

static void Group(Vi *v, const ViHost *h, int begin)
{
	if (begin == v->grouped)
		return;
	v->grouped = begin;
	if (h->group != NULL)
		h->group(h->ctx, begin);
}

static void Yank(Vi *v, const Buf *b, long s, long e, int lines)
{
	long len = e - s;

	if (len + 1 > v->reg->cap)
		len = v->reg->cap - 1;
	if (len < 0)
		len = 0;
	memcpy(v->reg->buf, b->t + s, (size_t) len);
	if (lines && (len == 0 || !IsEol(v->reg->buf[len - 1])))
		v->reg->buf[len++] = '\r';  /* the last line, which had no line end */
	if (lines && len > 0 && IsEol(v->reg->buf[0]) && s > 0 && e >= b->n) {
		/* the last line, taken with the line end before it */
		len--;
		memmove(v->reg->buf, v->reg->buf + 1, (size_t) len);
	}
	v->reg->len = len;
	v->reg->lines = lines;
}

/* The change the user typed is complete: . will repeat it */
static void Done(Vi *v)
{
	if (v->recOk && !v->replaying && v->recLen > 0) {
		memcpy(v->dot, v->rec, (size_t) v->recLen);
		v->dotLen = v->recLen;
	}
}

static void StartInsert(Vi *v, const ViHost *h, long at, long count)
{
	Group(v, h, 1);
	v->mode = VI_INSERT;
	v->insStart = at;
	v->insCount = count > 0 ? count : 1;
	v->insMoved = 0;
	v->jAt = -1;
	Select(h, at, at);
}

/* The lines from s's to e's (e is on the last line) as an operator takes
   them: with their line ends */
static void LineRange(const Buf *b, long *s, long *e)
{
	long a = LineStart(b, *s), z = LineEnd(b, *e);

	if (z < b->n) {
		z++;
	} else if (a > 0) {
		a--;                        /* the last line: take the line end before it */
	}
	*s = a;
	*e = z;
}

/* An operator on [s, e) */
static void Operate(Vi *v, const ViHost *h, int op, long s, long e, int lines)
{
	Buf b;
	long cur, from = s;

	Load(&b, h);
	if (lines)
		LineRange(&b, &s, &e);
	if (e > b.n)
		e = b.n;
	if (s >= e && !(op == 'c' && s == e)) {
		Beep(h);
		return;
	}
	Yank(v, &b, s, e, lines);
	if (op == 'y') {
		cur = Clamp(&b, lines ? LineStart(&b, from) : s);
		Select(h, cur, cur);
		return;
	}
	if (op == 'c') {
		if (lines) {
			/* keep one empty line to type on */
			long a = s, z = e;
			if (z > a && IsEol(b.t[z - 1]))
				z--;
			else if (a < z && IsEol(b.t[a]))
				a++;                /* the last line: its line end before it stays */
			Group(v, h, 1);
			if (!h->replace(h->ctx, VI_EDIT, a, z, "", 0)) {
				Group(v, h, 0);
				Beep(h);
				return;
			}
			StartInsert(v, h, a, 1);
			return;
		}
		Group(v, h, 1);
		if (e > s && !h->replace(h->ctx, VI_EDIT, s, e, "", 0)) {
			Group(v, h, 0);
			Beep(h);
			return;
		}
		StartInsert(v, h, s, 1);
		return;
	}
	/* d */
	Group(v, h, 1);
	if (!h->replace(h->ctx, VI_EDIT, s, e, "", 0)) {
		Group(v, h, 0);
		Beep(h);
		return;
	}
	Group(v, h, 0);
	Load(&b, h);
	if (lines) {
		cur = s < b.n ? s : LineStart(&b, b.n);
		cur = FirstBlank(&b, LineStart(&b, cur));
	} else {
		cur = s;
	}
	cur = Clamp(&b, cur);
	Select(h, cur, cur);
	Done(v);
}

static void Put(Vi *v, const ViHost *h, int after, long count)
{
	Buf b;
	long pos, at, i, len;
	char *ins;
	int ok;

	if (v->reg->len == 0) {
		Beep(h);
		return;
	}
	Load(&b, h);
	pos = Caret(h);
	if (count < 1)
		count = 1;
	if (v->reg->lines) {
		if (after) {
			at = LineEnd(&b, pos);
			if (at < b.n)
				at++;
		} else {
			at = LineStart(&b, pos);
		}
	} else {
		at = after && pos < b.n && !IsEol(b.t[pos]) ? pos + 1 : pos;
	}
	Group(v, h, 1);
	for (i = 0, ok = 1; ok && i < count; i++) {
		Load(&b, h);
		ins = v->reg->buf;
		len = v->reg->len;
		if (v->reg->lines && after && at >= b.n && (b.n == 0 || !IsEol(b.t[b.n - 1]))) {
			/* after the last line, which has no line end: give it one */
			ok = h->replace(h->ctx, VI_EDIT, at, at, "\r", 1);
			at++;
			ok = ok && h->replace(h->ctx, VI_EDIT, at, at, ins, len - 1);
			len--;
		} else {
			ok = h->replace(h->ctx, VI_EDIT, at, at, ins, len);
		}
		if (ok && i == 0 && v->reg->lines)
			pos = at;
		at += len;
	}
	Group(v, h, 0);
	if (!ok)
		Beep(h);
	Load(&b, h);
	pos = v->reg->lines ? FirstBlank(&b, pos) : Clamp(&b, at - 1);
	Select(h, pos, pos);
	Done(v);
}

static void Join(Vi *v, const ViHost *h, long pos, long count)
{
	Buf b;
	long i, le, next, keep;

	if (count < 2)
		count = 2;
	Group(v, h, 1);
	for (i = 1; i < count; i++) {
		Load(&b, h);
		le = LineEnd(&b, pos);
		if (le >= b.n) {
			if (i == 1)
				Beep(h);
			break;
		}
		next = le + 1;
		while (next < b.n && IsBlank(b.t[next]))
			next++;
		keep = le;
		while (keep > LineStart(&b, pos) && IsBlank(b.t[keep - 1]))
			keep--;
		if (next >= b.n || IsEol(b.t[next]) || b.t[next] == ')' || keep == LineStart(&b, pos))
			(void) h->replace(h->ctx, VI_EDIT, keep, next, "", 0);
		else
			(void) h->replace(h->ctx, VI_EDIT, keep, next, " ", 1);
		pos = keep;
	}
	Group(v, h, 0);
	Load(&b, h);
	pos = Clamp(&b, pos);
	Select(h, pos, pos);
	Done(v);
}

static char Toggle(char c)
{
	if (c >= 'a' && c <= 'z')
		return (char) (c - 32);
	if (c >= 'A' && c <= 'Z')
		return (char) (c + 32);
	return c;
}

/* ~ over [s, e) */
static void ToggleCase(Vi *v, const ViHost *h, long s, long e)
{
	Buf b;
	char buf[256];
	long p, k;

	Group(v, h, 1);
	for (p = s; p < e; p += k) {
		Load(&b, h);
		k = e - p > (long) sizeof(buf) ? (long) sizeof(buf) : e - p;
		memcpy(buf, b.t + p, (size_t) k);
		{
			long i;
			for (i = 0; i < k; i++)
				buf[i] = Toggle(buf[i]);
		}
		(void) h->replace(h->ctx, VI_EDIT, p, p + k, buf, k);
	}
	Group(v, h, 0);
	Done(v);
}

/* ---- modes ---- */

static void ShowVisual(Vi *v, const ViHost *h, long cur)
{
	Buf b;
	long s = v->anchor < cur ? v->anchor : cur, e = v->anchor < cur ? cur : v->anchor;

	Load(&b, h);
	if (v->mode == VI_VISUAL_LINE) {
		s = LineStart(&b, s);
		e = NextLine(&b, e);
	} else {
		e = e < b.n ? e + 1 : b.n;
	}
	Select(h, s, e);
	v->cur = cur;
}

static void ToNormal(Vi *v, const ViHost *h, long pos)
{
	Buf b;

	v->mode = VI_NORMAL;
	v->op = 0;
	v->pend = 0;
	v->count = 0;
	v->opCount = 0;
	Group(v, h, 0);
	Load(&b, h);
	pos = Clamp(&b, pos);
	Select(h, pos, pos);
}

/* Escape in insert mode: a count repeats the text typed */
static void EndInsert(Vi *v, const ViHost *h)
{
	Buf b;
	long pos = Caret(h), i, len;

	if (v->insCount > 1 && !v->insMoved && pos > v->insStart) {
		char buf[VI_KEYS];
		Load(&b, h);
		len = pos - v->insStart;
		if (len <= (long) sizeof(buf)) {
			memcpy(buf, b.t + v->insStart, (size_t) len);
			for (i = 1; i < v->insCount; i++) {
				if (!h->replace(h->ctx, VI_EDIT, pos, pos, buf, len))
					break;
				pos += len;
			}
		}
	}
	Load(&b, h);
	if (pos > LineStart(&b, pos))
		pos--;
	v->insCount = 0;
	ToNormal(v, h, pos);
	if (!v->insMoved)
		Done(v);
}

void vi_init(Vi *v, ViReg *reg)
{
	memset(v, 0, sizeof(*v));
	v->mode = VI_NORMAL;
	v->goal = -1;
	v->jAt = -1;
	v->reg = reg;
}

void vi_clicked(Vi *v, const ViHost *h)
{
	long s, e;

	v->goal = -1;
	if (v->mode == VI_INSERT) {
		v->insMoved = 1;            /* the insert goes on elsewhere */
		v->recOk = 0;
		return;
	}
	h->get_sel(h->ctx, &s, &e);
	if (s == e) {
		ToNormal(v, h, s);
		return;
	}
	/* a mouse selection is a visual one */
	v->op = 0;
	v->pend = 0;
	v->count = v->opCount = 0;
	v->mode = VI_VISUAL;
	v->anchor = s;
	v->cur = e - 1;
}

const char *vi_status(const Vi *v)
{
	if (!v->on)
		return "";
	switch (v->mode) {
	case VI_INSERT: return "-- INSERT --";
	case VI_VISUAL: return "-- VISUAL --";
	case VI_VISUAL_LINE: return "-- VISUAL LINE --";
	}
	return "";
}

static void Record(Vi *v, int c)
{
	if (v->replaying)
		return;
	if (v->recLen < VI_KEYS)
		v->rec[v->recLen++] = (unsigned char) c;
	else
		v->recOk = 0;
}

static int Insert(Vi *v, const ViHost *h, int c, unsigned long ticks)
{
	long s, e;
	char ch = (char) c;

	h->get_sel(h->ctx, &s, &e);
	if (c == VI_ESC) {
		Record(v, c);
		EndInsert(v, h);
		return 1;
	}
	if (v->jk && c == 'k' && v->jAt >= 0 && s == e && s == v->jAt + 1 && ticks - v->jTicks <= kJkTicks) {
		/* jk: the j goes, and so does insert mode */
		(void) h->replace(h->ctx, VI_DELETE, v->jAt, s, "", 0);
		if (v->recLen > 0)
			v->rec[v->recLen - 1] = VI_ESC;
		v->jAt = -1;
		EndInsert(v, h);
		return 1;
	}
	if (c == VI_BACKSPACE) {
		Record(v, c);
		if (s != e)
			(void) h->replace(h->ctx, VI_DELETE, s, e, "", 0);
		else if (s > 0)
			(void) h->replace(h->ctx, VI_DELETE, s - 1, s, "", 0);
		v->jAt = -1;
		return 1;
	}
	if (c == VI_RETURN || c == '\t' || (c >= 0x20 && c != 0x7F)) {
		Record(v, c);
		if (!h->replace(h->ctx, VI_TYPE, s, e, &ch, 1)) {
			Beep(h);
			return 1;
		}
		v->jAt = c == 'j' ? s : -1;
		v->jTicks = ticks;
		return 1;
	}
	/* arrow keys and the like: the app's; the insert can't be repeated */
	v->insMoved = 1;
	v->recOk = 0;
	v->jAt = -1;
	return 0;
}

/* . : the last change again, with a new count if there is one */
static void Repeat(Vi *v, const ViHost *h, long count, unsigned long ticks)
{
	unsigned char keys[VI_KEYS];
	char digits[12];
	int len = v->dotLen, i = 0, k;

	if (len == 0) {
		Beep(h);
		return;
	}
	memcpy(keys, v->dot, (size_t) len);
	if (count > 0)
		while (i < len && keys[i] >= '0' && keys[i] <= '9' && !(i == 0 && keys[i] == '0'))
			i++;
	v->replaying = 1;
	if (count > 0) {
		k = 0;
		do
			digits[k++] = (char) ('0' + count % 10);
		while ((count /= 10) > 0 && k < 11);
		while (k > 0)
			(void) vi_key(v, h, digits[--k], ticks);
	}
	for (; i < len; i++)
		(void) vi_key(v, h, keys[i], ticks);
	if (v->mode == VI_INSERT)
		(void) vi_key(v, h, VI_ESC, ticks);
	v->replaying = 0;
}

static int Visual(Vi *v, const ViHost *h, int c, long count)
{
	Buf b;
	long s, e, cur = v->cur;
	Move m;
	int lines = v->mode == VI_VISUAL_LINE;

	Load(&b, h);
	if (v->pend) {
		int pend = v->pend;
		v->pend = 0;
		if (Motion(v, &b, pend, c, count, cur, 0, &m) && m.to >= 0) {
			if (!m.keepGoal)
				v->goal = -1;
			ShowVisual(v, h, Clamp(&b, m.to));
		} else {
			Beep(h);
		}
		return 1;
	}
	switch (c) {
	case VI_ESC:
		ToNormal(v, h, cur);
		return 1;
	case 'v':
	case 'V':
		if ((c == 'v') == !lines) {
			ToNormal(v, h, cur);
		} else {
			v->mode = c == 'v' ? VI_VISUAL : VI_VISUAL_LINE;
			ShowVisual(v, h, cur);
		}
		return 1;
	case 'o':
		s = v->anchor;
		v->anchor = cur;
		ShowVisual(v, h, s);
		return 1;
	case 'g': case 'f': case 'F': case 't': case 'T':
		v->pend = (char) c;
		return 1;
	}
	if (Motion(v, &b, 0, c, count, cur, 0, &m)) {
		if (m.to < 0) {
			Beep(h);
			return 1;
		}
		if (!m.keepGoal)
			v->goal = -1;
		ShowVisual(v, h, Clamp(&b, m.to));
		return 1;
	}
	s = v->anchor < cur ? v->anchor : cur;
	e = v->anchor < cur ? cur : v->anchor;
	if (!lines || (c != 'd' && c != 'x' && c != 'c' && c != 's' && c != 'y'))
		e++;                        /* charwise: the end is inclusive */
	if (e > b.n)
		e = b.n;
	v->recOk = 0;                   /* . doesn't repeat what visual mode did */
	switch (c) {
	case 'd': case 'x': case 'X': case 'D':
		v->mode = VI_NORMAL;
		Operate(v, h, 'd', s, e, lines || c == 'X' || c == 'D');
		return 1;
	case 'c': case 's': case 'S': case 'C':
		v->mode = VI_NORMAL;
		Operate(v, h, 'c', s, e, lines || c == 'S' || c == 'C');
		return 1;
	case 'y': case 'Y':
		v->mode = VI_NORMAL;
		Operate(v, h, 'y', s, e, lines || c == 'Y');
		return 1;
	case '~':
		if (lines) {
			s = LineStart(&b, s);
			e = LineEnd(&b, e > s ? e - 1 : e);
		}
		ToggleCase(v, h, s, e);
		ToNormal(v, h, s);
		return 1;
	case 'J':
		ToNormal(v, h, s);
		Join(v, h, s, LineOf(&b, e > s ? e - 1 : e) - LineOf(&b, s) + 1);
		return 1;
	}
	Beep(h);
	return 1;
}

/* The count for an operator's motion: 2d3w is 6; 0 when none was typed */
static long Total(const Vi *v, long count)
{
	if (count == 0 && v->opCount == 0)
		return 0;
	return (count > 0 ? count : 1) * (v->opCount > 0 ? v->opCount : 1);
}

int vi_key(Vi *v, const ViHost *h, int c, unsigned long ticks)
{
	Buf b;
	Move m;
	long pos, count, s, e;
	int lines;

	if (!v->on)
		return 0;
	if (v->mode == VI_INSERT)
		return Insert(v, h, c, ticks);
	/* a new command: start recording it for . */
	if (v->op == 0 && v->pend == 0 && v->count == 0 && v->mode == VI_NORMAL) {
		v->recLen = 0;
		v->recOk = 1;
	}
	Record(v, c);
	if (c == VI_ESC && v->mode == VI_NORMAL) {
		/* cancels what's half typed */
		v->op = 0;
		v->pend = 0;
		v->count = v->opCount = 0;
		return 1;
	}
	if (c >= '0' && c <= '9' && (c != '0' || v->count > 0) && v->pend != 'r'
			&& v->pend != 'f' && v->pend != 'F' && v->pend != 't' && v->pend != 'T') {
		v->count = v->count * 10 + (c - '0');
		if (v->count > 99999)
			v->count = 99999;
		return 1;
	}
	count = v->count;
	v->count = 0;
	if (v->mode == VI_VISUAL || v->mode == VI_VISUAL_LINE)
		return Visual(v, h, c, count);
	Load(&b, h);
	pos = Clamp(&b, Caret(h));

	/* the second key of r, i a (objects), g f F t T */
	if (v->pend) {
		int pend = v->pend;
		v->pend = 0;
		if (pend == 'r') {
			long n1 = count > 0 ? count : 1, i;
			char buf[64];
			if (c == VI_ESC)
				return 1;
			if (pos + n1 > LineEnd(&b, pos) || n1 > (long) sizeof(buf)) {
				Beep(h);
				return 1;
			}
			if (c == VI_RETURN) {
				(void) h->replace(h->ctx, VI_EDIT, pos, pos + n1, "\r", 1);
				Load(&b, h);
				pos = pos + 1;
			} else {
				for (i = 0; i < n1; i++)
					buf[i] = (char) c;
				(void) h->replace(h->ctx, VI_EDIT, pos, pos + n1, buf, n1);
				pos += n1 - 1;
			}
			Select(h, pos, pos);
			Done(v);
			return 1;
		}
		if ((pend == 'i' || pend == 'a') && v->op) {
			int op = v->op;
			v->op = 0;
			if (!Object(&b, pos, pend == 'i', c, &s, &e, &lines)) {
				Beep(h);
				return 1;
			}
			Operate(v, h, op, s, e, lines);
			return 1;
		}
		if (Motion(v, &b, pend, c, Total(v, count), pos, v->op != 0, &m)) {
			goto moved;
		}
		v->op = 0;
		Beep(h);
		return 1;
	}

	if (c == 'g' || c == 'f' || c == 'F' || c == 't' || c == 'T' || (c == 'r' && !v->op)
			|| ((c == 'i' || c == 'a') && v->op)) {
		v->pend = (char) c;
		v->count = count;           /* the count waits too */
		return 1;
	}

	if (v->op) {
		int op = v->op;
		if (c == op || (op == 'c' && c == 'c')) {
			/* dd cc yy: count lines */
			long n1 = (count > 0 ? count : 1) * (v->opCount > 0 ? v->opCount : 1), i;
			s = LineStart(&b, pos);
			e = s;
			for (i = 1; i < n1 && NextLine(&b, e) < b.n; i++)
				e = NextLine(&b, e);
			v->op = 0;
			Operate(v, h, op, s, LineEnd(&b, e), 1);
			return 1;
		}
		if (Motion(v, &b, 0, c, Total(v, count), pos, 1, &m)) {
			goto moved;
		}
		v->op = 0;
		Beep(h);
		return 1;
	}

	if (Motion(v, &b, 0, c, count, pos, 0, &m))
		goto moved;

	switch (c) {
	case 'd': case 'c': case 'y':
		v->op = (char) c;
		v->opCount = count;
		v->count = 0;
		/* the next key's count multiplies this one */
		return 1;
	case 'x':
		if (EmptyLine(&b, pos)) {
			Beep(h);
			return 1;
		}
		e = pos + (count > 0 ? count : 1);
		if (e > LineEnd(&b, pos))
			e = LineEnd(&b, pos);
		Operate(v, h, 'd', pos, e, 0);
		return 1;
	case 'X':
		s = pos - (count > 0 ? count : 1);
		if (s < LineStart(&b, pos))
			s = LineStart(&b, pos);
		Operate(v, h, 'd', s, pos, 0);
		return 1;
	case 'D':
	case 'C':
		e = LineEnd(&b, pos);
		if (c == 'D' && e == pos) {
			Beep(h);
			return 1;
		}
		Operate(v, h, c == 'D' ? 'd' : 'c', pos, e, 0);
		return 1;
	case 's':
		e = pos + (count > 0 ? count : 1);
		if (e > LineEnd(&b, pos))
			e = LineEnd(&b, pos);
		Operate(v, h, 'c', pos, e, 0);
		return 1;
	case 'S':
		s = LineStart(&b, pos);
		e = s;
		{
			long i;
			for (i = 1; i < count && NextLine(&b, e) < b.n; i++)
				e = NextLine(&b, e);
		}
		Operate(v, h, 'c', s, LineEnd(&b, e), 1);
		return 1;
	case 'Y':
		s = LineStart(&b, pos);
		Operate(v, h, 'y', s, LineEnd(&b, s), 1);
		return 1;
	case 'p':
	case 'P':
		Put(v, h, c == 'p', count);
		return 1;
	case 'J':
		Join(v, h, pos, count);
		return 1;
	case '~':
		e = pos + (count > 0 ? count : 1);
		if (e > LineEnd(&b, pos))
			e = LineEnd(&b, pos);
		if (e == pos) {
			Beep(h);
			return 1;
		}
		ToggleCase(v, h, pos, e);
		Load(&b, h);
		pos = Clamp(&b, e);
		Select(h, pos, pos);
		return 1;
	case 'i':
		StartInsert(v, h, pos, count);
		return 1;
	case 'a':
		StartInsert(v, h, EmptyLine(&b, pos) ? pos : pos + 1, count);
		return 1;
	case 'I':
		StartInsert(v, h, FirstBlank(&b, LineStart(&b, pos)), count);
		return 1;
	case 'A':
		StartInsert(v, h, LineEnd(&b, pos), count);
		return 1;
	case 'o':
		s = LineEnd(&b, pos);
		Group(v, h, 1);
		if (!h->replace(h->ctx, VI_EDIT, s, s, "\r", 1)) {
			Beep(h);
			return 1;
		}
		StartInsert(v, h, s + 1, count);
		v->insStart = s;            /* a count repeats the line end too */
		return 1;
	case 'O':
		s = LineStart(&b, pos);
		Group(v, h, 1);
		if (!h->replace(h->ctx, VI_EDIT, s, s, "\r", 1)) {
			Beep(h);
			return 1;
		}
		StartInsert(v, h, s, count);
		return 1;
	case 'v':
	case 'V':
		v->mode = c == 'v' ? VI_VISUAL : VI_VISUAL_LINE;
		v->anchor = pos;
		v->recOk = 0;
		ShowVisual(v, h, pos);
		return 1;
	case 'u':
	case 'U':
	case 0x12:                      /* Control-R */
		{
			long i, n1 = count > 0 ? count : 1;
			for (i = 0; i < n1; i++)
				h->undo(h->ctx, c != 'u');
			Load(&b, h);
			pos = Clamp(&b, Caret(h));
			Select(h, pos, pos);
			v->recOk = 0;
		}
		return 1;
	case '.':
		v->recOk = 0;
		Repeat(v, h, count, ticks);
		return 1;
	case '/':
	case '?':
	case 'n':
	case 'N':
		v->recOk = 0;
		if (h->find == NULL) {
			Beep(h);
			return 1;
		}
		h->find(h->ctx, c == 'n' ? VI_FIND_NEXT : c == 'N' ? VI_FIND_PREVIOUS : VI_FIND_ASK);
		Load(&b, h);
		pos = Clamp(&b, Caret(h));
		Select(h, pos, pos);
		return 1;
	}
	v->recOk = 0;
	Beep(h);
	return 1;

moved:
	if (m.to < 0) {
		v->op = 0;
		Beep(h);
		return 1;
	}
	if (!m.keepGoal)
		v->goal = -1;
	if (v->op) {
		int op = v->op;
		v->op = 0;
		s = pos < m.to ? pos : m.to;
		e = pos < m.to ? m.to : pos;
		if (m.how == kIncl && e < b.n)
			e++;
		/* cw on a word is ce (on a word's last letter, just that letter) */
		if (op == 'c' && (c == 'w' || c == 'W') && Kind(&b, pos, c == 'W') != 0) {
			int big = c == 'W', k = Kind(&b, pos, big);
			long n1 = Total(v, count), i, q = pos;
			int atEnd = pos + 1 >= b.n || Kind(&b, pos + 1, big) != k;
			if (n1 < 1)
				n1 = 1;
			for (i = 0; i < n1; i++)
				q = i == 0 && atEnd ? pos : WordEnd(&b, q, big);
			s = pos;
			e = q + 1;
		}
		Operate(v, h, op, s, e, m.how == kLines);
		return 1;
	}
	v->recOk = 0;
	Load(&b, h);
	pos = Clamp(&b, m.to);
	Select(h, pos, pos);
	return 1;
}
