/* mdpage.c: see mdpage.h. */
#include <stddef.h>
#include <string.h>

#include "frontmatter.h"
#include "mdpage.h"
#include "textconv.h"

/* Mac Roman's lozenge, for a character it doesn't have */
#define kMissing ((char) 0xD7)
/* between the date and the category: a bullet, which every Mac font has
   (System 6's Geneva and Times have no middle dot) */
#define kBylineDot ((char) 0xA5)

#define IsSpace(c) ((c) == ' ' || (c) == '\t')
#define IsEol(c) ((c) == '\r' || (c) == '\n')

static int IsAlpha(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int IsDigit(char c)
{
	return c >= '0' && c <= '9';
}

static int IsPunct(char c)
{
	return c > ' ' && c < 0x7F && !IsAlpha(c) && !IsDigit(c);
}

static long LineEnd(const char *t, long n, long p)
{
	while (p < n && !IsEol(t[p]))
		p++;
	return p;
}

static long NextLine(const char *t, long n, long p)
{
	p = LineEnd(t, n, p);
	return p < n ? p + 1 : n;
}

static long SkipSpace(const char *t, long p, long le)
{
	while (p < le && IsSpace(t[p]))
		p++;
	return p;
}

/* How many > a line starts with; *cs is where its content starts */
static int Quotes(const char *t, long ls, long le, long *cs)
{
	long p = ls, q;
	int depth = 0, i;

	for (;;) {
		q = p;
		for (i = 0; i < 3 && q < le && t[q] == ' '; i++)
			q++;
		if (q >= le || t[q] != '>')
			break;
		depth++;
		p = q + 1;
		if (p < le && t[p] == ' ')
			p++;
	}
	*cs = p;
	return depth;
}

/* The indent before p's first non-space, in columns (tabs are 4) */
static int Indent(const char *t, long p, long le)
{
	int col = 0;

	for (; p < le && IsSpace(t[p]); p++)
		col = t[p] == '\t' ? (col + 4) & ~3 : col + 1;
	return col;
}

static int Blank(const char *t, long p, long le)
{
	return SkipSpace(t, p, le) == le;
}

static int StartsWith(const char *t, long p, long le, const char *s)
{
	long k = (long) strlen(s);

	return p + k <= le && memcmp(t + p, s, (size_t) k) == 0;
}

/* The ] for the [ at p, or -1: on the same line, before le */
static long Close(const char *t, long p, long le, char open, char close)
{
	int depth = 0;

	for (; p < le && !IsEol(t[p]); p++) {
		if (t[p] == '\\')
			p++;
		else if (t[p] == open)
			depth++;
		else if (t[p] == close && --depth == 0)
			return p;
	}
	return -1;
}

/* [name]: address (not a footnote's [^1]:) */
static int Definition(const char *t, long p, long le)
{
	long j;

	if (Indent(t, p, le) > 3)
		return 0;
	p = SkipSpace(t, p, le);
	if (p >= le || t[p] != '[' || (p + 1 < le && t[p + 1] == '^'))
		return 0;
	j = Close(t, p, le, '[', ']');
	return j > 0 && j + 1 < le && t[j + 1] == ':';
}

/* The line is ![description](address) and nothing else */
static int ImageLine(const char *t, long p, long le)
{
	long j;

	p = SkipSpace(t, p, le);
	if (!StartsWith(t, p, le, "!["))
		return 0;
	j = Close(t, p + 1, le, '[', ']');
	if (j < 0 || j + 1 >= le || t[j + 1] != '(')
		return 0;
	j = Close(t, j + 1, le, '(', ')');
	return j >= 0 && Blank(t, j + 1, le);
}

/* The HTML elements that start a block of their own */
static int HtmlBlock(const char *t, long p, long le)
{
	static const char *const kTags[] = {
		"address", "article", "aside", "audio", "blockquote", "center", "details", "div", "dl",
		"figure", "footer", "form", "h1", "h2", "h3", "h4", "h5", "h6", "header", "hr", "iframe",
		"img", "main", "nav", "ol", "p", "picture", "pre", "script", "section", "style", "table",
		"ul", "video", NULL
	};
	long q, k;
	int i;

	p = SkipSpace(t, p, le);
	if (StartsWith(t, p, le, "<!--"))
		return 1;
	if (StartsWith(t, p, le, "{%")) {
		/* a Liquid tag on a line of its own */
		while (le > p && IsSpace(t[le - 1]))
			le--;
		return le - p >= 4 && t[le - 2] == '%' && t[le - 1] == '}';
	}
	if (p >= le || t[p] != '<')
		return 0;
	p++;
	if (p < le && t[p] == '/')
		p++;
	for (q = p; q < le && (IsAlpha(t[q]) || IsDigit(t[q])); q++)
		;
	for (i = 0; kTags[i] != NULL; i++) {
		k = (long) strlen(kTags[i]);
		if (q - p == k) {
			long m;
			for (m = 0; m < k && (t[p + m] | 0x20) == kTags[i][m]; m++)
				;
			if (m == k)
				return 1;
		}
	}
	return 0;
}

/* A line that starts a block of its own, ending a paragraph before it */
static int Starts(const char *t, long cs, long le)
{
	long ts, te;

	return md_heading(t, cs, le, &ts, &te) || md_rule(t, cs, le) || md_list_marker(t, cs, le) > cs
		|| t[SkipSpace(t, cs, le)] == '|' || HtmlBlock(t, cs, le) || Definition(t, cs, le);
}

/* ---- the head ---- */

static const char *const kMonths[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/* "2026-10-22 10:00" as "Oct 22, 2026"; anything else as it is */
static void FormatDate(const char *in, char *out, long cap)
{
	int y, m, d, i;

	for (i = 0; i < 10; i++)
		if (in[i] == 0 || (i == 4 || i == 7 ? in[i] != '-' : !IsDigit(in[i])))
			break;
	if (i == 10 && (in[10] == 0 || in[10] == ' ' || in[10] == 'T')) {
		y = (in[0] - '0') * 1000 + (in[1] - '0') * 100 + (in[2] - '0') * 10 + (in[3] - '0');
		m = (in[5] - '0') * 10 + (in[6] - '0');
		d = (in[8] - '0') * 10 + (in[9] - '0');
		if (m >= 1 && m <= 12 && d >= 1 && cap >= 14) {
			char *o = out;
			memcpy(o, kMonths[m - 1], 3);
			o += 3;
			*o++ = ' ';
			if (d >= 10)
				*o++ = (char) ('0' + d / 10);
			*o++ = (char) ('0' + d % 10);
			*o++ = ',';
			*o++ = ' ';
			*o++ = (char) ('0' + y / 1000);
			*o++ = (char) ('0' + y / 100 % 10);
			*o++ = (char) ('0' + y / 10 % 10);
			*o++ = (char) ('0' + y % 10);
			*o = 0;
			return;
		}
	}
	strncpy(out, in, (size_t) cap - 1);
	out[cap - 1] = 0;
}

/* The head block's text (NUL-ended) into buf; "" when there's none */
static void HeadText(const char *t, long n, int kind, char *buf, long cap)
{
	char date[64], cat[128];

	buf[0] = 0;
	switch (kind) {
	case MP_TITLE:
		(void) fm_get(t, n, "title", buf, cap);
		break;
	case MP_BYLINE:
		(void) fm_get(t, n, "date", buf, sizeof(date));
		FormatDate(buf, date, sizeof(date));
		if (fm_get(t, n, "categories", cat, sizeof(cat)) != kFmFound || cat[0] == 0)
			(void) fm_get(t, n, "category", cat, sizeof(cat));
		buf[0] = 0;
		if ((long) (strlen(date) + strlen(cat) + 4) < cap) {
			strcpy(buf, date);
			if (date[0] && cat[0]) {
				long k = (long) strlen(buf);
				buf[k] = ' ';
				buf[k + 1] = kBylineDot;
				buf[k + 2] = ' ';
				buf[k + 3] = 0;
			}
			strcat(buf, cat);
		}
		break;
	case MP_LEAD:
		(void) fm_get(t, n, "description", buf, cap);
		break;
	}
}

/* ---- blocks ---- */

void mp_begin(MpScan *p, const char *text, long n)
{
	memset(p, 0, sizeof(*p));
	p->text = text;
	p->n = n;
	md_begin(&p->s);
	p->s.quoteFace = 0;
	p->head = MP_TITLE;
}

/* An item's place in the open lists: its depth */
static int Nest(MpScan *p, int indent, int quote)
{
	if (quote != p->listQuote) {
		p->depth = 0;
		p->listQuote = quote;
	}
	while (p->depth > 0 && p->indents[p->depth - 1] > indent)
		p->depth--;
	if (p->depth > 0 && p->indents[p->depth - 1] == indent)
		return p->depth;
	if (p->depth > 0 && indent < p->indents[p->depth - 1] + 2)
		return p->depth;            /* not indented enough to nest: a sibling */
	if (p->depth < 8)
		p->indents[p->depth++] = indent;
	return p->depth;
}

/* Lines that carry on the block: each one passes more() */
static void Gather(MpScan *p, MpBlock *b, int quote, int (*more)(const char *, long, long))
{
	const char *t = p->text;
	MdState save;
	long ls, le, cs;

	for (;;) {
		save = p->s;
		ls = p->s.pos;
		if (ls >= p->n)
			break;
		if (md_line(&p->s, t, p->n, &le) != MD_LINE_TEXT || Quotes(t, ls, le, &cs) != quote
				|| Blank(t, cs, le) || !more(t, cs, le)) {
			p->s = save;
			break;
		}
		b->end = p->s.pos;
	}
}

static int ParaMore(const char *t, long cs, long le)
{
	return !Starts(t, cs, le);
}

static int TableMore(const char *t, long cs, long le)
{
	return t[SkipSpace(t, cs, le)] == '|';
}

static int HtmlMore(const char *t, long cs, long le)
{
	(void) t;
	(void) cs;
	(void) le;
	return 1;                       /* up to a blank line */
}

int mp_next(MpScan *p, MpBlock *b)
{
	const char *t = p->text;
	char buf[256];
	long ls, le, cs, ts, te, m;
	int kind, quote, indent;

	memset(b, 0, sizeof(*b));
	while (p->head <= MP_LEAD) {
		kind = p->head++;
		HeadText(t, p->n, kind, buf, sizeof(buf));
		if (buf[0]) {
			b->kind = (unsigned char) kind;
			return 1;
		}
	}
	for (;;) {
		ls = p->s.pos;
		if (ls >= p->n)
			return 0;
		kind = md_line(&p->s, t, p->n, &le);
		b->start = ls;
		b->end = p->s.pos;
		if (kind == MD_LINE_FRONT_DELIM || kind == MD_LINE_FRONT)
			continue;
		indent = Indent(t, ls, le);
		if (kind == MD_LINE_FENCE) {
			/* the code runs to the closing fence, or the end */
			b->kind = MP_CODE;
			b->start = b->end;
			for (;;) {
				ls = p->s.pos;
				if (ls >= p->n || md_line(&p->s, t, p->n, &le) == MD_LINE_FENCE)
					break;
			}
			b->end = ls;
			if (indent == 0 || p->depth == 0)
				p->depth = 0;
			else
				b->depth = (unsigned char) p->depth;
			p->blank = 0;
			return 1;
		}
		quote = Quotes(t, ls, le, &cs);
		if (Blank(t, cs, le)) {
			p->blank = 1;
			continue;
		}
		if (Definition(t, cs, le))
			continue;
		b->quote = (unsigned char) quote;
		indent = Indent(t, cs, le);
		b->at = SkipSpace(t, cs, le);
		if (md_heading(t, cs, le, &ts, &te)) {
			b->kind = MP_HEADING;
			b->level = (unsigned char) md_heading(t, cs, le, &ts, &te);
			p->depth = 0;
		} else if (md_rule(t, cs, le)) {
			b->kind = MP_RULE;
			p->depth = 0;
		} else if ((m = md_list_marker(t, cs, le)) > cs) {
			long q = SkipSpace(t, cs, le);
			b->kind = MP_ITEM;
			b->depth = (unsigned char) Nest(p, indent, quote);
			for (; q < le && IsDigit(t[q]); q++)
				b->number = b->number * 10 + (t[q] - '0');
			if (m - cs >= 3 && t[m - 1] == ']' && t[m - 3] == '[')
				b->task = (char) (t[m - 2] == ' ' ? ' ' : 'x');
			else if (m < le && t[m - 1] == ']')
				b->task = 0;
			b->at = m;
			Gather(p, b, quote, ParaMore);
		} else if (t[b->at] == '|') {
			b->kind = MP_TABLE;
			Gather(p, b, quote, TableMore);
		} else if (HtmlBlock(t, cs, le)) {
			b->kind = MP_HTML;
			Gather(p, b, quote, HtmlMore);
		} else {
			b->kind = MP_PARA;
			if (p->depth > 0 && (indent >= 2 || !p->blank) && quote == p->listQuote)
				b->depth = (unsigned char) p->depth;
			else
				p->depth = 0;
			Gather(p, b, quote, ParaMore);
			if (t[b->at] == '!' && NextLine(t, p->n, ls) == b->end && ImageLine(t, cs, le))
				b->kind = MP_IMAGE;
		}
		p->blank = 0;
		return 1;
	}
}

/* ---- display text ---- */

typedef struct {
	char *out;
	unsigned char *sty;
	long cap, len;
	char last;                  /* the last character put */
} Sink;

/* Puts one character (c is read once) */
#define Put(k, c, style) do { \
	Sink *k_ = (k); \
	char c_ = (c); \
	if (k_->len < k_->cap) { \
		if (k_->out != NULL) \
			k_->out[k_->len] = c_; \
		if (k_->sty != NULL) \
			k_->sty[k_->len] = (unsigned char) (style); \
	} \
	k_->last = c_; \
	k_->len++; \
} while (0)

static void PutStr(Sink *k, const char *s, int style)
{
	while (*s)
		Put(k, *s++, style);
}

/* The last character put, or 0 */
static char Last(const Sink *k)
{
	return k->len > 0 ? k->last : 0;
}

/* Takes back trailing spaces */
static void Trim(Sink *k)
{
	while (k->len > 0 && k->last == ' ') {
		k->len--;
		k->last = k->len > 0 && k->len <= k->cap && k->out != NULL ? k->out[k->len - 1] : 0;
	}
}

static const struct {
	const char *name;
	unsigned long cp;
} kEntities[] = {
	{ "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' },
	{ "nbsp", 0xA0 }, { "copy", 0xA9 }, { "reg", 0xAE }, { "trade", 0x2122 },
	{ "mdash", 0x2014 }, { "ndash", 0x2013 }, { "hellip", 0x2026 }, { "lsquo", 0x2018 },
	{ "rsquo", 0x2019 }, { "ldquo", 0x201C }, { "rdquo", 0x201D }, { "deg", 0xB0 },
	{ "middot", 0xB7 }, { "bull", 0x2022 }, { "eacute", 0xE9 }, { "times", 0xD7 },
	{ NULL, 0 }
};

/* An entity at p (&name; &#123; &#x1F600;): its length, and the
   character in *c; 0 if it isn't one */
static long Entity(const char *t, long p, long le, char *c)
{
	unsigned long cp = 0;
	long q = p + 1, e;
	int i, r;

	for (e = q; e < le && e - p < 12 && t[e] != ';'; e++)
		;
	if (e >= le || t[e] != ';' || e == q)
		return 0;
	if (t[q] == '#') {
		q++;
		if (q < e && (t[q] == 'x' || t[q] == 'X')) {
			for (q++; q < e; q++) {
				char h = t[q];
				int v = IsDigit(h) ? h - '0' : (h | 0x20) >= 'a' && (h | 0x20) <= 'f' ? (h | 0x20) - 'a' + 10 : -1;
				if (v < 0)
					return 0;
				cp = cp * 16 + (unsigned long) v;
			}
		} else {
			for (; q < e; q++) {
				if (!IsDigit(t[q]))
					return 0;
				cp = cp * 10 + (unsigned long) (t[q] - '0');
			}
		}
	} else {
		for (i = 0; kEntities[i].name != NULL; i++)
			if ((long) strlen(kEntities[i].name) == e - q && memcmp(t + q, kEntities[i].name, (size_t) (e - q)) == 0)
				break;
		if (kEntities[i].name == NULL)
			return 0;
		cp = kEntities[i].cp;
	}
	r = tc_roman_from_cp(cp);
	*c = r < 0 ? kMissing : (char) r;
	return e + 1 - p;
}

#define StyleOf(cls) (((cls) & (MD_BOLD | MD_ITALIC)) | ((cls) & MD_CODE ? MS_CODE : 0) \
	| ((cls) & MD_LINK ? MS_LINK : 0))

/* One line's inline text [cs, le); c is the line's classes, c[0] for cs */
static void Inline(Sink *k, const char *t, long cs, long le, const unsigned char *c, MpLinkFn fn, void *ctx)
{
	long i = cs, e;
	char ch;
	int note = 0;

	while (i < le) {
		int cl = c[i - cs];
		ch = t[i];
		if (cl & MD_META) {
			for (e = i; e < le && (c[e - cs] & MD_META); e++)
				;
			if (ch == '{' && t[i + 1] != ':') {
				for (; i < e; i++)
					Put(k, t[i], MS_META);
			} else {
				for (; i < e; i++)
					if (StartsWith(t, i, e, "<br") || StartsWith(t, i, e, "<BR"))
						Put(k, MP_BREAK, 0);
			}
			i = e;
		} else if (cl & MD_MARK) {
			if (ch == '\\' && i + 1 < le && IsPunct(t[i + 1]) && (c[i + 1 - cs] & MD_MARK)) {
				Put(k, t[i + 1], StyleOf(c[i + 1 - cs] & ~MD_MARK));
				i += 2;
				continue;
			}
			if (ch == '[' && i + 1 < le && t[i + 1] == '^')
				note = 1;
			else if (ch == ']')
				note = 0;
			else if (fn != NULL && ((ch == '!' && i + 1 < le && t[i + 1] == '[')
					|| (ch == '[' && !(i > cs && t[i - 1] == '!' && (c[i - 1 - cs] & MD_MARK)))))
				fn(ctx, i);
			i++;
		} else if (cl & MD_URL) {
			for (e = i; e < le && (c[e - cs] & MD_URL); e++)
				;
			if (i > cs && t[i - 1] == '<') {
				/* an autolink shows its address */
				if (fn != NULL)
					fn(ctx, i - 1);
				for (; i < e; i++)
					Put(k, t[i], MS_LINK | StyleOf(cl & ~MD_LINK));
			}
			i = e;
		} else if (note) {
			if (ch != '^')
				Put(k, ch, MS_NOTE);
			i++;
		} else if (IsSpace(ch) && !(cl & MD_CODE)) {
			if (Last(k) != ' ' && Last(k) != MP_BREAK)
				Put(k, ' ', StyleOf(cl));
			i++;
		} else if (ch == '&' && !(cl & MD_CODE) && (e = Entity(t, i, le, &ch)) > 0) {
			Put(k, ch, StyleOf(cl));
			i += e;
		} else {
			Put(k, ch == '\t' ? ' ' : ch, StyleOf(cl));
			i++;
		}
	}
}

/* Lines as they are, joined by breaks, tabs to 4 columns */
static void Verbatim(Sink *k, const char *t, long n, const MpBlock *b, int style)
{
	long ls, le, p, cs;
	int col;

	for (ls = b->start; ls < b->end; ls = NextLine(t, n, ls)) {
		le = LineEnd(t, n, ls);
		if (b->kind != MP_CODE)
			(void) Quotes(t, ls, le, &cs);
		else
			cs = ls;
		if (ls > b->start)
			Put(k, MP_BREAK, 0);
		for (col = 0, p = cs; p < le; p++) {
			if (t[p] == '\t') {
				do
					Put(k, ' ', style);
				while (++col & 3);
			} else {
				Put(k, t[p], style);
				col++;
			}
		}
	}
}

long mp_line_text(const char *text, long n, const MpBlock *b, long ls, const unsigned char *cls,
		long *end, int *hard)
{
	long le = LineEnd(text, n, ls), cs, ts, te;

	if (ls == b->start && b->kind == MP_HEADING) {
		(void) Quotes(text, ls, le, &cs);
		(void) md_heading(text, cs, le, &ts, &te);
		cs = ts;
		le = te;
	} else if (ls == b->start && b->kind == MP_ITEM) {
		cs = b->at;
	} else {
		(void) Quotes(text, ls, le, &cs);
	}
	cs = SkipSpace(text, cs, le);
	/* two spaces or a backslash at the end: a line break */
	*hard = (le - cs >= 2 && text[le - 1] == ' ' && text[le - 2] == ' ')
		|| (le > cs && text[le - 1] == '\\' && !(cls[le - 1 - ls] & MD_MARK));
	while (le > cs && IsSpace(text[le - 1]))
		le--;
	if (*hard && le > cs && text[le - 1] == '\\')
		le--;
	*end = le;
	return cs;
}

long mp_text_links(const char *text, long n, const MpBlock *b, char *out, unsigned char *sty, long cap,
		unsigned char *cls, MpLinkFn fn, void *ctx)
{
	Sink k;
	MdState s;
	long ls, le, cs;
	int hard;

	k.out = out;
	k.sty = sty;
	k.cap = out == NULL && sty == NULL ? 0 : cap;
	k.len = 0;
	k.last = 0;
	switch (b->kind) {
	case MP_TITLE:
	case MP_BYLINE:
	case MP_LEAD: {
		char buf[256];
		HeadText(text, n, b->kind, buf, sizeof(buf));
		PutStr(&k, buf, 0);
		return k.len;
	}
	case MP_CODE:
	case MP_TABLE:
		Verbatim(&k, text, n, b, MS_CODE);
		return k.len;
	case MP_HTML:
		Verbatim(&k, text, n, b, MS_META);
		return k.len;
	case MP_RULE:
		return 0;
	}
	md_begin(&s);
	s.front = 2;
	s.quoteFace = 0;
	s.pos = b->start;
	md_classify(&s, text, n, b->end, cls);
	for (ls = b->start; ls < b->end; ls = NextLine(text, n, ls)) {
		cs = mp_line_text(text, n, b, ls, cls + (ls - b->start), &le, &hard);
		if (ls > b->start && k.len > 0 && Last(&k) != ' ' && Last(&k) != MP_BREAK)
			Put(&k, ' ', 0);
		Inline(&k, text, cs, le, cls + (cs - b->start), fn, ctx);
		if (hard && NextLine(text, n, ls) < b->end)
			Put(&k, MP_BREAK, 0);
	}
	Trim(&k);
	return k.len;
}

long mp_text(const char *text, long n, const MpBlock *b, char *out, unsigned char *sty, long cap,
		unsigned char *cls)
{
	return mp_text_links(text, n, b, out, sty, cap, cls, NULL, NULL);
}

/* ---- links ---- */

static void Copy(char *out, long cap, const char *s, long len)
{
	if (cap <= 0)
		return;
	if (len > cap - 1)
		len = cap - 1;
	memcpy(out, s, (size_t) len);
	out[len] = 0;
}

/* A destination from p (just inside the parenthesis): *len and its start */
static long Destination(const char *t, long p, long le, long *len)
{
	long q;
	int depth = 0;

	p = SkipSpace(t, p, le);
	if (p < le && t[p] == '<') {
		for (q = p + 1; q < le && t[q] != '>' && !IsEol(t[q]); q++)
			;
		*len = q - p - 1;
		return p + 1;
	}
	for (q = p; q < le && !IsSpace(t[q]) && !IsEol(t[q]); q++) {
		if (t[q] == '(')
			depth++;
		else if (t[q] == ')' && depth-- == 0)
			break;
	}
	*len = q - p;
	return p;
}

static int SameName(const char *a, const char *b, long len)
{
	long i;

	for (i = 0; i < len; i++) {
		char x = a[i], y = b[i];
		if (x >= 'A' && x <= 'Z')
			x = (char) (x + 32);
		if (y >= 'A' && y <= 'Z')
			y = (char) (y + 32);
		if (x != y)
			return 0;
	}
	return 1;
}

/* A reference's definition: the address's start and *len, or -1 */
static long Lookup(const char *t, long n, const char *name, long len, long *dlen)
{
	long ls, le, p, j;

	for (ls = 0; ls < n; ls = NextLine(t, n, ls)) {
		le = LineEnd(t, n, ls);
		if (!Definition(t, ls, le))
			continue;
		p = SkipSpace(t, ls, le);
		j = Close(t, p, le, '[', ']');
		if (j - p - 1 == len && SameName(t + p + 1, name, len))
			return Destination(t, j + 2, le, dlen);
	}
	return -1;
}

long mp_link_url(const char *text, long n, long at, char *url, long cap)
{
	long le = n, j, k, d, len = 0;     /* Close and Destination stop at the line's end */

	if (cap > 0)
		url[0] = 0;
	if (at >= le || IsEol(text[at]))
		return 0;
	if (text[at] == '<') {
		for (j = at + 1; j < le && text[j] != '>' && !IsEol(text[j]); j++)
			;
		Copy(url, cap, text + at + 1, j - at - 1);
		return j - at - 1;
	}
	if (text[at] == '!')
		at++;
	if (at >= le || text[at] != '[')
		return 0;
	j = Close(text, at, le, '[', ']');
	if (j < 0 || j + 1 >= le)
		return 0;
	if (text[j + 1] == '(') {
		d = Destination(text, j + 2, le, &len);
	} else if (text[j + 1] == '[') {
		k = Close(text, j + 1, le, '[', ']');
		if (k < 0)
			return 0;
		if (k == j + 2)             /* [text][]: the text is the name */
			d = Lookup(text, n, text + at + 1, j - at - 1, &len);
		else
			d = Lookup(text, n, text + j + 2, k - j - 2, &len);
		if (d < 0)
			return 0;
	} else {
		return 0;
	}
	Copy(url, cap, text + d, len);
	return len;
}

long mp_link_text(const char *text, long n, long at, char *out, long cap)
{
	long le = n, j, i, len = 0;        /* Close stops at the line's end */

	if (cap > 0)
		out[0] = 0;
	if (at >= le || IsEol(text[at]))
		return 0;
	if (text[at] == '<')
		return mp_link_url(text, n, at, out, cap);
	if (text[at] == '!')
		at++;
	j = Close(text, at, le, '[', ']');
	if (j < 0)
		return 0;
	for (i = at + 1; i < j; i++) {
		char c = text[i];
		if (c == '\\' && i + 1 < j)
			c = text[++i];
		else if (c == '*' || c == '_' || c == '`')
			continue;
		if (len < cap - 1)
			out[len] = c;
		len++;
	}
	if (cap > 0)
		out[len < cap - 1 ? len : cap - 1] = 0;
	return len;
}

int mp_font(const MpBlock *b, int style)
{
	switch (b->kind) {
	case MP_TITLE:
		return MF_TITLE;
	case MP_BYLINE:
		return MF_SMALL;
	case MP_CODE:
	case MP_TABLE:
	case MP_HTML:
		return MF_CODE;
	case MP_HEADING:
		if (style & (MS_CODE | MS_META))
			return MF_CODE;
		return b->level == 1 ? MF_H1 : b->level == 2 ? MF_H2 : b->level == 3 ? MF_H3 : MF_BOLD;
	case MP_LEAD:
		style |= MS_ITALIC;
		break;
	}
	if (style & MS_NOTE)
		return MF_SMALL;
	if (style & (MS_CODE | MS_META))
		return MF_CODE;
	if ((style & (MS_BOLD | MS_ITALIC)) == (MS_BOLD | MS_ITALIC))
		return MF_BOLD_ITALIC;
	if (style & MS_BOLD)
		return MF_BOLD;
	if (style & MS_ITALIC)
		return MF_ITALIC;
	return MF_BODY;
}
