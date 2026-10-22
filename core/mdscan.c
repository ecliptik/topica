/* mdscan.c: see mdscan.h. */
#include <stddef.h>
#include <string.h>

#include "mdscan.h"

/* Classes that make a character markup or literal: emphasis and links
   don't start or end on them */
#define kTaken (MD_CODE | MD_URL | MD_MARK | MD_META)

/* What a line is, as far as the blocks go */
enum { kText, kFrontDelim, kFront, kFenceDelim, kCode };

static int IsSpace(char c)
{
	return c == ' ' || c == '\t';
}

static int IsEol(char c)
{
	return c == '\r' || c == '\n';
}

/* Letters and digits, Mac Roman's accented letters among them */
static int IsAlnum(char c)
{
	unsigned char u = (unsigned char) c;

	return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || u >= 0x80;
}

static int IsPunct(char c)
{
	return c > ' ' && c < 0x7F && !IsAlnum(c);
}

static long LineEnd(const char *t, long n, long p)
{
	while (p < n && !IsEol(t[p]))
		p++;
	return p;
}

/* After up to three spaces of indent */
static long Indent3(const char *t, long p, long le)
{
	int i;

	for (i = 0; i < 3 && p < le && t[p] == ' '; i++)
		p++;
	return p;
}

/* The line is exactly word, give or take trailing spaces */
static int LineIs(const char *t, long ls, long le, const char *word)
{
	long w = (long) strlen(word);

	while (le > ls && IsSpace(t[le - 1]))
		le--;
	return le - ls == w && memcmp(t + ls, word, (size_t) w) == 0;
}

/* A code fence: three or more ` or ~; its character and length */
static int Fence(const char *t, long ls, long le, char *ch, int *len)
{
	long p = Indent3(t, ls, le), q = p;

	if (p >= le || (t[p] != '`' && t[p] != '~'))
		return 0;
	while (q < le && t[q] == t[p])
		q++;
	if (q - p < 3)
		return 0;
	if (t[p] == '`' && memchr(t + q, '`', (size_t) (le - q)) != NULL)
		return 0;                       /* that's a code span */
	*ch = t[p];
	*len = (int) (q - p);
	return 1;
}

/* Moves the block state over one line and says what the line is */
static int Block(MdState *s, const char *t, long ls, long le)
{
	char ch;
	int len;

	if (s->front == 0) {
		s->front = 2;
		if (LineIs(t, ls, le, "---")) {
			s->front = 1;
			return kFrontDelim;
		}
	} else if (s->front == 1) {
		if (LineIs(t, ls, le, "---") || LineIs(t, ls, le, "...")) {
			s->front = 2;
			return kFrontDelim;
		}
		return kFront;
	}
	if (s->fence) {
		if (Fence(t, ls, le, &ch, &len) && ch == s->fence && len >= s->fenceLen) {
			long p = Indent3(t, ls, le) + len;
			while (p < le && IsSpace(t[p]))
				p++;
			if (p == le) {              /* a closing fence has no info string */
				s->fence = 0;
				return kFenceDelim;
			}
		}
		return kCode;
	}
	if (Fence(t, ls, le, &ch, &len)) {
		s->fence = ch;
		s->fenceLen = len;
		return kFenceDelim;
	}
	return kText;
}

void md_begin(MdState *s)
{
	s->pos = 0;
	s->front = 0;
	s->fence = 0;
	s->fenceLen = 0;
}

long md_line_after(const char *text, long n, long pos)
{
	long le = LineEnd(text, n, pos);

	return le < n ? le + 1 : n;
}

long md_seek(MdState *s, const char *text, long n, long pos)
{
	while (s->pos < n) {
		long le = LineEnd(text, n, s->pos);
		if (le >= n || le + 1 > pos)
			break;
		(void) Block(s, text, s->pos, le);
		s->pos = le + 1;
	}
	return s->pos;
}

/* ---- inline ---- */

static void Fill(unsigned char *c, long from, long to, int cls)
{
	for (; from < to; from++)
		c[from] |= (unsigned char) cls;
}

/* The matching close bracket for the one at p, or -1 */
static long Match(const char *t, const unsigned char *c, long p, long b, char open, char close)
{
	int depth = 0;

	for (; p < b; p++) {
		if (c[p] & (MD_CODE | MD_META))
			continue;
		if (t[p] == '\\') {
			p++;
		} else if (t[p] == open) {
			depth++;
		} else if (t[p] == close && --depth == 0) {
			return p;
		}
	}
	return -1;
}

static long Find(const char *t, long p, long b, const char *s)
{
	long k = (long) strlen(s);

	for (; p + k <= b; p++)
		if (memcmp(t + p, s, (size_t) k) == 0)
			return p;
	return -1;
}

static int StartsWith(const char *t, long p, long b, const char *s)
{
	long k = (long) strlen(s);

	return p + k <= b && memcmp(t + p, s, (size_t) k) == 0;
}

/* Escapes, code spans, Liquid and HTML tags, autolinks: the parts nothing
   else looks inside. c is indexed by text offset. */
static void Literals(const char *t, unsigned char *c, long a, long b)
{
	long i = a, j, k, e;

	while (i < b) {
		if (t[i] == '\\' && i + 1 < b && IsPunct(t[i + 1])) {
			Fill(c, i, i + 2, MD_MARK);
			i += 2;
		} else if (t[i] == '`') {
			for (k = i; k < b && t[k] == '`'; k++)
				;
			k -= i;
			/* the next run of exactly as many backticks */
			for (j = i + k; j < b; j = e) {
				for (e = j; e < b && t[e] == '`'; e++)
					;
				if (e > j && e - j == k)
					break;
				if (e == j)
					e++;
			}
			if (j < b) {
				Fill(c, i, j + k, MD_CODE);
				Fill(c, i, i + k, MD_MARK);
				Fill(c, j, j + k, MD_MARK);
				i = j + k;
			} else {
				i += k;
			}
		} else if (t[i] == '{' && i + 1 < b && (t[i + 1] == '%' || t[i + 1] == '{' || t[i + 1] == ':')) {
			/* Liquid {% %} and {{ }}, kramdown's {: } */
			e = Find(t, i + 2, b, t[i + 1] == '%' ? "%}" : t[i + 1] == '{' ? "}}" : "}");
			if (e < 0) {
				i++;
			} else {
				e += t[i + 1] == ':' ? 1 : 2;
				Fill(c, i, e, MD_META);
				i = e;
			}
		} else if (t[i] == '<' && (StartsWith(t, i + 1, b, "http://") || StartsWith(t, i + 1, b, "https://")
				|| StartsWith(t, i + 1, b, "mailto:"))) {
			for (e = i + 1; e < b && t[e] != '>' && t[e] != ' '; e++)
				;
			if (e < b && t[e] == '>') {
				Fill(c, i + 1, e, MD_URL);
				c[i] |= MD_MARK;
				c[e] |= MD_MARK;
				i = e + 1;
			} else {
				i++;
			}
		} else if (t[i] == '<' && i + 1 < b && (t[i + 1] == '/' || t[i + 1] == '!' || IsAlnum(t[i + 1]))
				&& (unsigned char) t[i + 1] < 0x80) {
			e = StartsWith(t, i + 1, b, "!--") ? Find(t, i + 4, b, "-->") : Find(t, i + 1, b, ">");
			if (e < 0) {
				i++;
			} else {
				e += t[e] == '-' ? 3 : 1;
				Fill(c, i, e, MD_META);
				i = e;
			}
		} else {
			i++;
		}
	}
}

/* Links [text](address), references [text][name], footnotes [^1] and
   images ![description](address) */
static void Links(const char *t, unsigned char *c, long a, long b)
{
	long i, j, k;

	for (i = a; i < b; i++) {
		if (t[i] != '[' || (c[i] & kTaken))
			continue;
		j = Match(t, c, i, b, '[', ']');
		if (j < 0)
			continue;
		if (j + 1 < b && (t[j + 1] == '(' || t[j + 1] == '[')) {
			k = Match(t, c, j + 1, b, t[j + 1], t[j + 1] == '(' ? ')' : ']');
			if (k < 0)
				continue;
			if (i > a && t[i - 1] == '!' && !(c[i - 1] & kTaken))
				c[i - 1] |= MD_MARK;
			Fill(c, i + 1, j, MD_LINK);
			c[i] |= MD_MARK;
			c[j] |= MD_MARK;
			Fill(c, j + 1, k + 1, MD_URL);
		} else if (i + 1 < j && t[i + 1] == '^') {
			Fill(c, i, j + 1, MD_LINK);
			c[i] |= MD_MARK;
			c[j] |= MD_MARK;
		}
		/* carry on inside: an image can be a link's text */
	}
}

/* Unclaimed * or _ in a row from p */
static long Run(const char *t, const unsigned char *c, long p, long b)
{
	long q = p;

	while (q < b && t[q] == t[p] && !(c[q] & kTaken))
		q++;
	return q - p;
}

/* *italic*, **bold**, ***both***, and the same with _ (not inside words) */
static void Emphasis(const char *t, unsigned char *c, long a, long b)
{
	long i = a, j, k, len, close;
	char ch;
	int face;

	while (i < b) {
		ch = t[i];
		if ((ch != '*' && ch != '_') || (c[i] & kTaken)) {
			i++;
			continue;
		}
		k = Run(t, c, i, b);
		if (k > 3 || i + k >= b || IsSpace(t[i + k]) || (ch == '_' && i > a && IsAlnum(t[i - 1]))) {
			i += k;
			continue;
		}
		close = -1;
		for (j = i + k; j < b; j += len) {
			if (t[j] != ch || (c[j] & kTaken)) {
				len = 1;
				continue;
			}
			len = Run(t, c, j, b);
			if (len >= k && !IsSpace(t[j - 1]) && !(ch == '_' && j + len < b && IsAlnum(t[j + len]))) {
				close = j + len - k;    /* the last k of the run: inner ones close first */
				break;
			}
		}
		if (close < 0) {
			i += k;
			continue;
		}
		face = k == 1 ? MD_ITALIC : k == 2 ? MD_BOLD : MD_BOLD | MD_ITALIC;
		Fill(c, i, close + k, face);
		Fill(c, i, i + k, MD_MARK);
		Fill(c, close, close + k, MD_MARK);
		i += k;
	}
}

static void Inline(const char *t, unsigned char *c, long a, long b)
{
	Literals(t, c, a, b);
	Links(t, c, a, b);
	Emphasis(t, c, a, b);
}

/* ---- lines ---- */

/* A heading's level, and where its text starts and ends; 0 if it isn't one */
static int Heading(const char *t, long ls, long le, long *ts, long *te)
{
	long p = Indent3(t, ls, le), q = p, h;

	while (q < le && t[q] == '#')
		q++;
	h = q - p;
	if (h == 0 || h > 6 || (q < le && !IsSpace(t[q])))
		return 0;
	*ts = q;
	while (*ts < le && IsSpace(t[*ts]))
		(*ts)++;
	*te = le;
	while (*te > *ts && IsSpace(t[*te - 1]))
		(*te)--;
	/* a closing run of #s, after a space */
	q = *te;
	while (q > *ts && t[q - 1] == '#')
		q--;
	if (q < *te && (q == *ts || IsSpace(t[q - 1]))) {
		*te = q;
		while (*te > *ts && IsSpace(t[*te - 1]))
			(*te)--;
	}
	return (int) h;
}

/* A thematic break: three or more - * or _, spaces between allowed */
static int Rule(const char *t, long ls, long le)
{
	long p = Indent3(t, ls, le);
	char ch = p < le ? t[p] : 0;
	int count = 0;

	if (ch != '-' && ch != '*' && ch != '_')
		return 0;
	for (; p < le; p++) {
		if (t[p] == ch)
			count++;
		else if (!IsSpace(t[p]))
			return 0;
	}
	return count >= 3;
}

/* A list item's marker: -, *, + or 1. 1) and a space; where the text starts,
   or ls if there's none */
static long ListMarker(const char *t, long ls, long le)
{
	long p = ls, q;

	while (p < le && IsSpace(t[p]))
		p++;
	if (p < le && (t[p] == '-' || t[p] == '*' || t[p] == '+')) {
		q = p + 1;
	} else {
		for (q = p; q < le && q - p < 9 && t[q] >= '0' && t[q] <= '9'; q++)
			;
		if (q == p || q >= le || (t[q] != '.' && t[q] != ')'))
			return ls;
		q++;
	}
	if (q < le && !IsSpace(t[q]))
		return ls;
	while (q < le && IsSpace(t[q]))
		q++;
	/* a task list's box */
	if (q + 2 < le && t[q] == '[' && (t[q + 1] == ' ' || t[q + 1] == 'x' || t[q + 1] == 'X') && t[q + 2] == ']')
		q += 3;
	return q;
}

/* A link reference definition: [name]: address "title" */
static int Definition(const char *t, unsigned char *c, long ls, long le)
{
	long p = Indent3(t, ls, le), j, q;

	if (p >= le || t[p] != '[' || (p + 1 < le && t[p + 1] == '^'))
		return 0;
	j = Match(t, c, p, le, '[', ']');
	if (j < 0 || j + 1 >= le || t[j + 1] != ':')
		return 0;
	Fill(c, p, j + 2, MD_MARK);
	Fill(c, p + 1, j, MD_URL);
	for (q = j + 2; q < le && IsSpace(t[q]); q++)
		;
	for (j = q; j < le && !IsSpace(t[j]); j++)
		;
	Fill(c, q, j, MD_URL);
	return 1;
}

/* One line's classes: c is indexed by text offset */
static void Line(MdState *s, const char *t, long ls, long le, unsigned char *c)
{
	long p, q, ts, te;
	int kind = Block(s, t, ls, le), level;

	switch (kind) {
	case kFrontDelim:
		Fill(c, ls, le, MD_META | MD_MARK);
		return;
	case kFront:
		Fill(c, ls, le, MD_META);
		return;
	case kFenceDelim:
		Fill(c, ls, le, MD_CODE | MD_MARK);
		return;
	case kCode:
		Fill(c, ls, le, MD_CODE);
		return;
	}
	/* block quotes, nested or not */
	p = ls;
	for (;;) {
		q = Indent3(t, p, le);
		if (q >= le || t[q] != '>')
			break;
		c[q] |= MD_MARK;
		p = q + 1;
		if (p < le && t[p] == ' ')
			p++;
		Fill(c, ls, le, MD_ITALIC);
	}
	if ((level = Heading(t, p, le, &ts, &te)) != 0) {
		Fill(c, p, le, MD_HEADING);
		Fill(c, p, ts, MD_MARK);
		Fill(c, te, le, MD_MARK);
		Inline(t, c, ts, te);
		return;
	}
	if (Rule(t, p, le)) {
		Fill(c, p, le, MD_MARK);
		return;
	}
	q = ListMarker(t, p, le);
	if (q > p) {
		Fill(c, p, q, MD_MARK);
		p = q;
	}
	if (Definition(t, c, p, le))
		return;
	Inline(t, c, p, le);
}

void md_classify(MdState *s, const char *text, long n, long end, unsigned char *out)
{
	long start = s->pos, le;

	if (end > n)
		end = n;
	if (end > start)
		memset(out, 0, (size_t) (end - start));
	while (s->pos < end) {
		le = LineEnd(text, n, s->pos);
		/* Line indexes classes by text offset: out - start, kept in range */
		Line(s, text + start, s->pos - start, le - start, out);
		s->pos = le < n ? le + 1 : n;
	}
}

unsigned long md_blocks(const char *text, long n)
{
	MdState s;
	unsigned long h = 5381;
	long le;
	int kind;

	md_begin(&s);
	while (s.pos < n) {
		le = LineEnd(text, n, s.pos);
		kind = Block(&s, text, s.pos, le);
		if (kind == kFrontDelim || kind == kFenceDelim)
			h = h * 33 + (unsigned long) kind * 7 + (unsigned char) s.fence * 131 + (unsigned long) s.fenceLen;
		s.pos = le < n ? le + 1 : n;
	}
	return h * 33 + (unsigned long) s.front;
}

int md_headings(const char *text, long n, MdHeading *out, int max)
{
	MdState s;
	long le, ts, te;
	int count = 0, level;

	md_begin(&s);
	while (s.pos < n) {
		le = LineEnd(text, n, s.pos);
		if (Block(&s, text, s.pos, le) == kText && (level = Heading(text, s.pos, le, &ts, &te)) != 0) {
			if (count < max) {
				out[count].line = s.pos;
				out[count].start = ts;
				out[count].len = te - ts;
				out[count].level = level;
			}
			count++;
		}
		s.pos = le < n ? le + 1 : n;
	}
	return count;
}
