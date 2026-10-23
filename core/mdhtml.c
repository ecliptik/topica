/* mdhtml.c: see mdhtml.h. */
#include <stddef.h>
#include <string.h>

#include "frontmatter.h"
#include "mdhtml.h"
#include "mdpage.h"

typedef struct {
	char *out;
	long cap, len;
} Out;

static void Put(Out *o, char c)
{
	if (o->out != NULL && o->len < o->cap)
		o->out[o->len] = c;
	o->len++;
}

static void PutN(Out *o, const char *s, long n)
{
	while (n-- > 0)
		Put(o, *s++);
}

static void PutS(Out *o, const char *s)
{
	PutN(o, s, (long) strlen(s));
}

static void PutNum(Out *o, long v)
{
	char buf[12];
	int i = 0;

	if (v < 0)
		v = 0;
	do
		buf[i++] = (char) ('0' + v % 10);
	while ((v /= 10) > 0);
	while (i > 0)
		Put(o, buf[--i]);
}

static int IsEol(char c)
{
	return c == '\r' || c == '\n';
}

static int IsSpace(char c)
{
	return c == ' ' || c == '\t';
}

static int IsAlnum(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
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

static long NextLine(const char *t, long n, long p)
{
	p = LineEnd(t, n, p);
	return p < n ? p + 1 : n;
}

/* &name; &#123; &#x1F; at p: it stays as it is */
static int Entity(const char *t, long p, long le)
{
	long q = p + 1;

	if (q < le && t[q] == '#') {
		q++;
		if (q < le && (t[q] == 'x' || t[q] == 'X'))
			q++;
	}
	if (q >= le || !IsAlnum(t[q]))
		return 0;
	while (q < le && q - p < 12 && IsAlnum(t[q]))
		q++;
	return q < le && t[q] == ';';
}

/* Text, with & < > " escaped (an entity's & stays when keep is set) */
static void Esc(Out *o, const char *t, long p, long e, int keep)
{
	for (; p < e; p++) {
		char c = t[p];
		if (c == '&' && !(keep && Entity(t, p, e)))
			PutS(o, "&amp;");
		else if (c == '<')
			PutS(o, "&lt;");
		else if (c == '>')
			PutS(o, "&gt;");
		else if (c == '"')
			PutS(o, "&quot;");
		else
			Put(o, c);
	}
}

static void EscS(Out *o, const char *s)
{
	Esc(o, s, 0, (long) strlen(s), 1);
}

/* ---- inline ---- */

enum { kStrong = 1, kEm = 2, kCode = 4 };

/* Closes the faces that are open, and opens want's */
static void Faces(Out *o, int *open, int want)
{
	if (*open == want)
		return;
	if (*open & kCode)
		PutS(o, "</code>");
	if (*open & kEm)
		PutS(o, "</em>");
	if (*open & kStrong)
		PutS(o, "</strong>");
	if (want & kStrong)
		PutS(o, "<strong>");
	if (want & kEm)
		PutS(o, "<em>");
	if (want & kCode)
		PutS(o, "<code>");
	*open = want;
}

static int Want(int cls)
{
	return (cls & MD_BOLD ? kStrong : 0) | (cls & MD_ITALIC ? kEm : 0) | (cls & MD_CODE ? kCode : 0);
}

/* The ] or ) for the bracket at p, or -1 */
static long Close(const char *t, long p, long le, char open, char close)
{
	int depth = 0;

	for (; p < le; p++) {
		if (t[p] == '\\')
			p++;
		else if (t[p] == open)
			depth++;
		else if (t[p] == close && --depth == 0)
			return p;
	}
	return -1;
}

/* After a link's or image's ] at j: past its (address) or [name] */
static long PastTarget(const char *t, long j, long le)
{
	long k = -1;

	if (j + 1 < le && t[j + 1] == '(')
		k = Close(t, j + 1, le, '(', ')');
	else if (j + 1 < le && t[j + 1] == '[')
		k = Close(t, j + 1, le, '[', ']');
	return k < 0 ? j + 1 : k + 1;
}

static void Href(Out *o, const char *t, long n, long at)
{
	char url[512];

	(void) mp_link_url(t, n, at, url, sizeof(url));
	PutS(o, "<a href=\"");
	EscS(o, url);
	PutS(o, "\">");
}

/* [cs, le) of a line as HTML; c[0] is cs's class */
static void Inline(Out *o, const char *t, long n, long cs, long le, const unsigned char *c)
{
	char buf[512];
	long i = cs, e, j;
	int open = 0, links = 0;

	while (i < le) {
		int cl = c[i - cs];
		char ch = t[i];
		if (cl & MD_META) {
			for (e = i; e < le && (c[e - cs] & MD_META); e++)
				;
			if (ch != '{')
				PutN(o, t + i, e - i);  /* HTML stays HTML */
			else if (t[i + 1] != ':')
				Esc(o, t, i, e, 0);     /* Liquid shows as written */
			i = e;
		} else if (cl & MD_MARK) {
			if (ch == '\\' && i + 1 < le && IsPunct(t[i + 1]) && (c[i + 1 - cs] & MD_MARK)) {
				Faces(o, &open, Want(c[i + 1 - cs]));
				Esc(o, t, i + 1, i + 2, 0);
				i += 2;
			} else if (ch == '!' && i + 1 < le && t[i + 1] == '[' && (j = Close(t, i + 1, le, '[', ']')) > 0) {
				(void) mp_link_url(t, n, i, buf, sizeof(buf));
				PutS(o, "<img src=\"");
				EscS(o, buf);
				PutS(o, "\" alt=\"");
				(void) mp_link_text(t, n, i, buf, sizeof(buf));
				EscS(o, buf);
				PutS(o, "\">");
				i = PastTarget(t, j, le);
			} else if (ch == '[' && i + 1 < le && t[i + 1] == '^' && (j = Close(t, i, le, '[', ']')) > 0) {
				Faces(o, &open, 0);
				PutS(o, "<sup>");
				Esc(o, t, i + 2, j, 1);
				PutS(o, "</sup>");
				i = j + 1;
			} else if (ch == '[') {
				Faces(o, &open, 0);
				Href(o, t, n, i);
				links++;
				i++;
			} else if (ch == ']' && links > 0) {
				Faces(o, &open, 0);
				PutS(o, "</a>");
				links--;
				for (i++; i < le && (c[i - cs] & MD_URL); i++)
					;
			} else if (ch == '<' && i + 1 < le && (c[i + 1 - cs] & MD_URL)) {
				for (e = i + 1; e < le && (c[e - cs] & MD_URL); e++)
					;
				Faces(o, &open, 0);
				Href(o, t, n, i);
				Esc(o, t, i + 1, e, 0);
				PutS(o, "</a>");
				i = e < le ? e + 1 : e;
			} else {
				i++;
			}
		} else if (cl & MD_URL) {
			i++;
		} else {
			Faces(o, &open, Want(cl));
			if (ch == '&' && !(cl & MD_CODE) && Entity(t, i, le)) {
				/* an entity stays as it is */
				for (; t[i] != ';'; i++)
					Put(o, t[i]);
				Put(o, ';');
			} else {
				Esc(o, t, i, i + 1, 0);
			}
			i++;
		}
	}
	Faces(o, &open, 0);
	while (links-- > 0)
		PutS(o, "</a>");
}

/* The block's lines as HTML, joined by line ends or <br> */
static void Lines(Out *o, const char *t, long n, const MpBlock *b, const unsigned char *cls)
{
	long ls, cs, le;
	int hard;

	for (ls = b->start; ls < b->end; ls = NextLine(t, n, ls)) {
		cs = mp_line_text(t, n, b, ls, cls + (ls - b->start), &le, &hard);
		if (ls > b->start)
			Put(o, '\n');
		Inline(o, t, n, cs, le, cls + (cs - b->start));
		if (hard && NextLine(t, n, ls) < b->end)
			PutS(o, "<br>");
	}
}

/* ---- blocks ---- */

static void Code(Out *o, const char *t, long n, const MpBlock *b)
{
	long ls, p;

	PutS(o, "<pre><code");
	if (b->start > 0) {
		/* the fence's info string: its language */
		for (ls = b->start - 1; ls > 0 && !IsEol(t[ls - 1]); ls--)
			;
		for (p = ls; p < b->start && (IsSpace(t[p]) || t[p] == '`' || t[p] == '~'); p++)
			;
		ls = p;
		while (p < b->start && (IsAlnum(t[p]) || t[p] == '-' || t[p] == '+' || t[p] == '_'))
			p++;
		if (p > ls) {
			PutS(o, " class=\"language-");
			PutN(o, t + ls, p - ls);
			Put(o, '"');
		}
	}
	Put(o, '>');
	for (ls = b->start; ls < b->end; ls = NextLine(t, n, ls)) {
		Esc(o, t, ls, LineEnd(t, n, ls), 0);
		Put(o, '\n');
	}
	PutS(o, "</code></pre>\n");
}

/* A table's delimiter row: | --- | :-: | */
static int DelimiterRow(const char *t, long p, long le)
{
	int dash = 0;

	for (; p < le; p++) {
		if (t[p] == '-')
			dash = 1;
		else if (t[p] != '|' && t[p] != ':' && !IsSpace(t[p]))
			return 0;
	}
	return dash;
}

static void Row(Out *o, const char *t, long n, long cs, long le, const unsigned char *c, const char *cell)
{
	long p = cs, q, a, e;

	if (p < le && t[p] == '|')
		p++;
	if (le > p && t[le - 1] == '|')
		le--;
	PutS(o, "<tr>");
	while (p <= le) {
		for (q = p; q < le && !(t[q] == '|' && !(c[q - cs] & MD_CODE) && !(q > p && t[q - 1] == '\\')); q++)
			;
		for (a = p; a < q && IsSpace(t[a]); a++)
			;
		for (e = q; e > a && IsSpace(t[e - 1]); e--)
			;
		PutS(o, "<");
		PutS(o, cell);
		PutS(o, ">");
		Inline(o, t, n, a, e, c + (a - cs));
		PutS(o, "</");
		PutS(o, cell);
		PutS(o, ">");
		p = q + 1;
	}
	PutS(o, "</tr>\n");
}

static void Table(Out *o, const char *t, long n, const MpBlock *b, const unsigned char *cls)
{
	long ls, cs, le, second = NextLine(t, n, b->start);
	int hard, head;

	cs = mp_line_text(t, n, b, second, cls + (second - b->start), &le, &hard);
	if (second >= b->end || !DelimiterRow(t, cs, le)) {
		PutS(o, "<p>");
		Lines(o, t, n, b, cls);
		PutS(o, "</p>\n");
		return;
	}
	PutS(o, "<table>\n");
	for (ls = b->start; ls < b->end; ls = NextLine(t, n, ls)) {
		if (ls == second)
			continue;
		head = ls == b->start;
		cs = mp_line_text(t, n, b, ls, cls + (ls - b->start), &le, &hard);
		Row(o, t, n, cs, le, cls + (cs - b->start), head ? "th" : "td");
	}
	PutS(o, "</table>\n");
}

typedef struct {
	int quote;                  /* <blockquote>s open */
	int lists;                  /* lists open */
	char kind[8];               /* each one's: 'u' or 'o' */
} Nesting;

static void CloseLists(Out *o, Nesting *s, int depth)
{
	while (s->lists > depth) {
		s->lists--;
		PutS(o, s->kind[s->lists] == 'o' ? "</li>\n</ol>\n" : "</li>\n</ul>\n");
	}
}

static void OpenList(Out *o, Nesting *s, const MpBlock *b)
{
	char kind = b->number > 0 ? 'o' : 'u';

	if (s->lists >= 8)
		return;
	s->kind[s->lists++] = kind;
	if (kind == 'u') {
		PutS(o, "<ul>\n");
	} else if (b->number == 1) {
		PutS(o, "<ol>\n");
	} else {
		PutS(o, "<ol start=\"");
		PutNum(o, b->number);
		PutS(o, "\">\n");
	}
}

/* The quotes and lists the block is in */
static void Nest(Out *o, Nesting *s, const MpBlock *b)
{
	char kind = b->number > 0 ? 'o' : 'u';

	if (b->quote != s->quote) {
		CloseLists(o, s, 0);
		for (; s->quote > b->quote; s->quote--)
			PutS(o, "</blockquote>\n");
		for (; s->quote < b->quote; s->quote++)
			PutS(o, "<blockquote>\n");
	}
	if (b->kind != MP_ITEM) {
		CloseLists(o, s, b->depth);
		return;
	}
	CloseLists(o, s, b->depth);
	if (s->lists == b->depth && s->kind[s->lists - 1] != kind)
		CloseLists(o, s, b->depth - 1);
	if (s->lists == b->depth) {
		PutS(o, "</li>\n<li>");
		return;
	}
	/* a list in an item; one that skips a level gets an empty item */
	while (s->lists < b->depth && s->lists < 8) {
		OpenList(o, s, b);
		if (s->lists < b->depth)
			PutS(o, "<li>");
	}
	PutS(o, "<li>");
}

static void Classify(const char *t, long n, const MpBlock *b, unsigned char *cls)
{
	MdState s;

	md_begin(&s);
	s.front = 2;
	s.quoteFace = 0;
	s.pos = b->start;
	md_classify(&s, t, n, b->end, cls);
}

static const char kStyle[] =
	"body{max-width:40em;margin:2em auto;padding:0 1em;font-family:Georgia,serif;line-height:1.5}"
	"pre,code{font-family:Monaco,Menlo,monospace;font-size:90%}pre{background:#f4f4f4;padding:.5em;overflow:auto}"
	"blockquote{border-left:3px solid #ccc;margin-left:0;padding-left:1em;color:#555}"
	"img{max-width:100%}table{border-collapse:collapse}th,td{border:1px solid #ccc;padding:.2em .5em}"
	".post-meta{color:#777}";

long mh_page(const char *text, long n, unsigned char *cls, char *out, long cap)
{
	Out o;
	MpScan p;
	MpBlock b;
	Nesting s;
	char buf[256];
	long len;
	int header = 0, body = 0;

	o.out = out;
	o.cap = cap;
	o.len = 0;
	memset(&s, 0, sizeof(s));
	PutS(&o, "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n"
		"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n<title>");
	if (fm_get(text, n, "title", buf, sizeof(buf)) == kFmFound)
		EscS(&o, buf);
	PutS(&o, "</title>\n<style>");
	PutS(&o, kStyle);
	PutS(&o, "</style>\n</head>\n<body>\n<article class=\"post\">\n");
	mp_begin(&p, text, n);
	while (mp_next(&p, &b)) {
		if (b.kind <= MP_LEAD) {
			if (!header) {
				PutS(&o, "<header class=\"post-header\">\n");
				header = 1;
			}
			len = mp_text(text, n, &b, buf, NULL, sizeof(buf) - 1, cls);
			buf[len < (long) sizeof(buf) - 1 ? len : (long) sizeof(buf) - 1] = '\0';
			PutS(&o, b.kind == MP_TITLE ? "<h1 class=\"post-title\">" : b.kind == MP_BYLINE
				? "<p class=\"post-meta\">" : "<p class=\"post-description\"><em>");
			EscS(&o, buf);
			PutS(&o, b.kind == MP_TITLE ? "</h1>\n" : b.kind == MP_BYLINE ? "</p>\n" : "</em></p>\n");
			continue;
		}
		if (!body) {
			if (header)
				PutS(&o, "</header>\n");
			PutS(&o, "<div class=\"post-content\">\n");
			body = 1;
		}
		Nest(&o, &s, &b);
		if (b.kind != MP_CODE && b.kind != MP_RULE)
			Classify(text, n, &b, cls);
		switch (b.kind) {
		case MP_HEADING:
			PutS(&o, "<h");
			PutNum(&o, b.level);
			Put(&o, '>');
			Lines(&o, text, n, &b, cls);
			PutS(&o, "</h");
			PutNum(&o, b.level);
			PutS(&o, ">\n");
			break;
		case MP_ITEM:
			if (b.task)
				PutS(&o, b.task == 'x' ? "<input type=\"checkbox\" disabled checked> "
					: "<input type=\"checkbox\" disabled> ");
			Lines(&o, text, n, &b, cls);
			break;
		case MP_CODE:
			Code(&o, text, n, &b);
			break;
		case MP_TABLE:
			Table(&o, text, n, &b, cls);
			break;
		case MP_HTML: {
			long ls, le;
			for (ls = b.start; ls < b.end; ls = NextLine(text, n, ls)) {
				le = LineEnd(text, n, ls);
				PutN(&o, text + ls, le - ls);
				Put(&o, '\n');
			}
			break;
		}
		case MP_RULE:
			PutS(&o, "<hr>\n");
			break;
		default:
			PutS(&o, "<p>");
			Lines(&o, text, n, &b, cls);
			PutS(&o, "</p>\n");
			break;
		}
	}
	b.quote = 0;
	b.depth = 0;
	b.kind = MP_PARA;
	Nest(&o, &s, &b);
	if (header && !body)
		PutS(&o, "</header>\n");
	if (body)
		PutS(&o, "</div>\n");
	PutS(&o, "</article>\n</body>\n</html>\n");
	return o.out != NULL && o.len > cap ? -1 : o.len;
}
