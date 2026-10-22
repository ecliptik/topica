/* frontmatter.c: see frontmatter.h. */
#include <string.h>

#include "frontmatter.h"

enum { kPlain, kDouble, kSingle, kFlow, kBlock, kComplex };

/* One line: [start, end) without its line end; next is past the line end. */
typedef struct {
	long start, end, next;
} Line;

static void LineAt(const char *t, long n, long pos, Line *l)
{
	long i = pos;

	while (i < n && t[i] != '\r' && t[i] != '\n')
		i++;
	l->start = pos;
	l->end = i;
	if (i < n && t[i] == '\r' && i + 1 < n && t[i + 1] == '\n')
		i += 2;
	else if (i < n)
		i++;
	l->next = i;
}

/* 1 when the line is s, trailing spaces aside */
static int LineIs(const char *t, const Line *l, const char *s)
{
	long len = (long) strlen(s), e = l->end;

	while (e > l->start && (t[e - 1] == ' ' || t[e - 1] == '\t'))
		e--;
	return e - l->start == len && memcmp(t + l->start, s, (size_t) len) == 0;
}

/* The block: its end, and where its closing line starts (*close) */
static int FindBlock(const char *text, long n, long *close, long *end)
{
	Line l;
	long pos;

	if (n < 3)
		return 0;
	LineAt(text, n, 0, &l);
	if (!LineIs(text, &l, "---"))
		return 0;
	for (pos = l.next; pos < n; pos = l.next) {
		LineAt(text, n, pos, &l);
		if (LineIs(text, &l, "---") || LineIs(text, &l, "...")) {
			*close = l.start;
			*end = l.next;
			return 1;
		}
	}
	return 0;
}

int fm_find(const char *text, long n, long *start, long *end)
{
	long close;

	*start = 0;
	return FindBlock(text, n, &close, end);
}

/* A top-level key's line: its key's length, 0 if it isn't one. */
static long KeyLine(const char *t, const Line *l)
{
	long i;

	if (l->end == l->start)
		return 0;
	if (t[l->start] == ' ' || t[l->start] == '\t' || t[l->start] == '#' || t[l->start] == '-')
		return 0;
	for (i = l->start; i < l->end; i++) {
		if (t[i] == ':' && (i + 1 == l->end || t[i + 1] == ' ' || t[i + 1] == '\t'))
			return i - l->start;
	}
	return 0;
}

/* One key: the key line and its continuation lines (indented, or "- "). */
typedef struct {
	long start, next;       /* bytes of every line, line ends included */
	long keyLen;
	long valStart, valEnd;  /* the inline value, trimmed */
	long contStart;         /* the first continuation line (== next if none) */
	int style;
	long indent;            /* a block list's indent */
} Entry;

static int IsSpace(char c)
{
	return c == ' ' || c == '\t';
}

/* The entry at pos (a key line), ending before blockEnd. */
static void EntryAt(const char *t, long blockEnd, long pos, Entry *e)
{
	Line l, k;
	long v, ve, i;
	int allDashes = 1, hasCont = 0;

	LineAt(t, blockEnd, pos, &k);
	e->start = pos;
	e->keyLen = KeyLine(t, &k);
	v = k.start + e->keyLen + 1;
	while (v < k.end && IsSpace(t[v]))
		v++;
	ve = k.end;
	e->contStart = k.next;
	e->indent = 0;
	for (pos = k.next; pos < blockEnd; pos = l.next) {
		LineAt(t, blockEnd, pos, &l);
		if (l.end == l.start || !(IsSpace(t[l.start]) || t[l.start] == '-'))
			break;
		i = l.start;
		while (i < l.end && IsSpace(t[i]))
			i++;
		if (!hasCont)
			e->indent = i - l.start;
		hasCont = 1;
		if (!(i < l.end && t[i] == '-' && (i + 1 == l.end || IsSpace(t[i + 1]))))
			allDashes = 0;
	}
	e->next = pos;
	/* a quoted value ends at its closing quote (a comment may follow) */
	if (v < ve && (t[v] == '"' || t[v] == '\'')) {
		for (i = v + 1; i < ve; i++) {
			if (t[v] == '"' && t[i] == '\\') {
				i++;
			} else if (t[i] == t[v]) {
				if (t[v] == '\'' && i + 1 < ve && t[i + 1] == '\'') {
					i++;
					continue;
				}
				ve = i + 1;
				break;
			}
		}
	}
	/* a plain value's trailing comment */
	if (v < ve && t[v] != '"' && t[v] != '\'') {
		for (i = v; i < ve; i++)
			if (t[i] == '#' && i > v && IsSpace(t[i - 1])) {
				ve = i;
				break;
			}
	}
	while (ve > v && IsSpace(t[ve - 1]))
		ve--;
	e->valStart = v;
	e->valEnd = ve;
	if (v == ve)
		e->style = !hasCont ? kPlain : allDashes ? kBlock : kComplex;
	else if (hasCont)
		e->style = kComplex;
	else if (t[v] == '"')
		e->style = kDouble;
	else if (t[v] == '\'')
		e->style = kSingle;
	else if (t[v] == '[')
		e->style = (t[ve - 1] == ']') ? kFlow : kComplex;
	else if (strchr("|>{&*!", t[v]) != NULL)
		e->style = kComplex;
	else
		e->style = kPlain;
}

/* Appends to a NUL-terminated string, cut at cap. */
typedef struct {
	char *s;
	long cap, len;
} Str;

static void StrPut(Str *s, char c)
{
	if (s->len + 1 < s->cap)
		s->s[s->len++] = c;
	s->s[s->len] = '\0';
}

/* A quoted or plain scalar t[a, b) into s, unquoted. */
static void Unquote(const char *t, long a, long b, Str *s)
{
	long i;

	if (b - a >= 2 && t[a] == '"' && t[b - 1] == '"') {
		for (i = a + 1; i < b - 1; i++) {
			if (t[i] == '\\' && i + 1 < b - 1) {
				i++;
				StrPut(s, t[i] == 'n' ? ' ' : t[i] == 't' ? '\t' : t[i]);
			} else {
				StrPut(s, t[i]);
			}
		}
	} else if (b - a >= 2 && t[a] == '\'' && t[b - 1] == '\'') {
		for (i = a + 1; i < b - 1; i++) {
			StrPut(s, t[i]);
			if (t[i] == '\'' && i + 1 < b - 1 && t[i + 1] == '\'')
				i++;
		}
	} else {
		for (i = a; i < b; i++)
			StrPut(s, t[i]);
	}
}

static void Trim(const char *t, long *a, long *b)
{
	while (*a < *b && IsSpace(t[*a]))
		(*a)++;
	while (*b > *a && IsSpace(t[*b - 1]))
		(*b)--;
}

static void ValueOf(const char *t, long blockEnd, const Entry *e, Str *s)
{
	long a, b, i;
	Line l;

	switch (e->style) {
	case kPlain:
	case kDouble:
	case kSingle:
		Unquote(t, e->valStart, e->valEnd, s);
		break;
	case kFlow:
		a = e->valStart + 1;
		for (i = a; i <= e->valEnd - 1; i++) {
			if (i == e->valEnd - 1 || t[i] == ',') {
				b = i;
				Trim(t, &a, &b);
				if (b > a) {
					if (s->len > 0)
						StrPut(s, ' ');
					Unquote(t, a, b, s);
				}
				a = i + 1;
			}
		}
		break;
	case kBlock:
		for (i = e->contStart; i < e->next; i = l.next) {
			LineAt(t, blockEnd, i, &l);
			a = l.start;
			while (a < l.end && IsSpace(t[a]))
				a++;
			a++;                        /* the dash */
			b = l.end;
			Trim(t, &a, &b);
			if (b > a) {
				if (s->len > 0)
					StrPut(s, ' ');
				Unquote(t, a, b, s);
			}
		}
		break;
	}
}

static int KeyIs(const char *t, const Entry *e, const char *key)
{
	return (long) strlen(key) == e->keyLen && memcmp(t + e->start, key, (size_t) e->keyLen) == 0;
}

int fm_get(const char *text, long n, const char *key, char *out, long cap)
{
	long close, end, pos;
	Line l;
	Entry e;
	Str s;

	s.s = out;
	s.cap = cap;
	s.len = 0;
	if (cap > 0)
		out[0] = '\0';
	if (!FindBlock(text, n, &close, &end))
		return kFmAbsent;
	LineAt(text, close, 0, &l);
	for (pos = l.next; pos < close; ) {
		LineAt(text, close, pos, &l);
		if (KeyLine(text, &l) == 0) {
			pos = l.next;
			continue;
		}
		EntryAt(text, close, pos, &e);
		if (KeyIs(text, &e, key)) {
			if (e.style == kComplex)
				return kFmComplex;
			ValueOf(text, close, &e, &s);
			return kFmFound;
		}
		pos = e.next;
	}
	return kFmAbsent;
}

int fm_needs_quotes(const char *v)
{
	static const char *const kWords[] = { "true", "false", "yes", "no", "on", "off", "null", "~", NULL };
	size_t len = strlen(v);
	const char *p;
	int i, digits = 0, dots = 0;

	if (len == 0)
		return 0;
	if (IsSpace(v[0]) || IsSpace(v[len - 1]) || strchr("-?:,[]{}#&*!|>'\"%@`", v[0]) != NULL)
		return 1;
	if (strstr(v, ": ") != NULL || strstr(v, " #") != NULL || v[len - 1] == ':' || strchr(v, '\t') != NULL)
		return 1;
	for (i = 0; kWords[i] != NULL; i++) {
		const char *w = kWords[i];
		for (p = v; *p && *w && (*p | 0x20) == *w; p++, w++)
			;
		if (*p == '\0' && *w == '\0')
			return 1;
	}
	for (p = (*v == '+') ? v + 1 : v; *p; p++) {
		if (*p >= '0' && *p <= '9')
			digits++;
		else if (*p == '.')
			dots++;
		else
			return 0;
	}
	return digits > 0 && dots <= 1;     /* a number */
}

/* The block being written */
typedef struct {
	char *buf;
	long cap, len;
	int over;
} Out;

static void Put(Out *o, const char *s, long n)
{
	if (o->buf != NULL) {
		if (o->len + n > o->cap) {
			o->over = 1;
			return;
		}
		memcpy(o->buf + o->len, s, (size_t) n);
	}
	o->len += n;
}

static void PutS(Out *o, const char *s)
{
	Put(o, s, (long) strlen(s));
}

static void PutC(Out *o, char c)
{
	Put(o, &c, 1);
}

/* A value's text, line ends turned to spaces */
static void PutValueChars(Out *o, const char *v, long n, char quote)
{
	long i;

	for (i = 0; i < n; i++) {
		char c = (v[i] == '\r' || v[i] == '\n') ? ' ' : v[i];
		if (quote == '"' && (c == '"' || c == '\\'))
			PutC(o, '\\');
		if (quote == '\'' && c == '\'')
			PutC(o, '\'');
		PutC(o, c);
	}
}

static void PutScalar(Out *o, const char *v, long n, int style)
{
	char item[256];
	char quote = 0;

	if (style == kDouble) {
		quote = '"';
	} else if (style == kSingle) {
		quote = '\'';
	} else {
		long k = n < (long) sizeof(item) - 1 ? n : (long) sizeof(item) - 1;
		memcpy(item, v, (size_t) k);
		item[k] = '\0';
		if (fm_needs_quotes(item))
			quote = '"';
	}
	if (quote)
		PutC(o, quote);
	PutValueChars(o, v, n, quote);
	if (quote)
		PutC(o, quote);
}

/* The items of a list value: words separated by spaces */
static int NextItem(const char *v, long *pos, long *a, long *b)
{
	long i = *pos;

	while (v[i] == ' ' || v[i] == '\t' || v[i] == '\r' || v[i] == '\n')
		i++;
	if (v[i] == '\0')
		return 0;
	*a = i;
	while (v[i] && v[i] != ' ' && v[i] != '\t' && v[i] != '\r' && v[i] != '\n')
		i++;
	*b = i;
	*pos = i;
	return 1;
}

static void PutEntry(Out *o, const char *key, const char *value, int style, long indent, char eol)
{
	long pos = 0, a, b, k;
	int first = 1;

	PutS(o, key);
	PutC(o, ':');
	if (style == kFlow) {
		PutS(o, " [");
		while (NextItem(value, &pos, &a, &b)) {
			if (!first)
				PutS(o, ", ");
			PutScalar(o, value + a, b - a, kPlain);
			first = 0;
		}
		PutC(o, ']');
		PutC(o, eol);
		return;
	}
	if (style == kBlock) {
		PutC(o, eol);
		while (NextItem(value, &pos, &a, &b)) {
			for (k = 0; k < indent; k++)
				PutC(o, ' ');
			PutS(o, "- ");
			PutScalar(o, value + a, b - a, kPlain);
			PutC(o, eol);
		}
		return;
	}
	if (value[0] != '\0') {
		PutC(o, ' ');
		PutScalar(o, value, (long) strlen(value), style);
	}
	PutC(o, eol);
}

long fm_update(const char *text, long n, const FmField *fields, int count, char eol, char *out, long cap)
{
	Out o;
	long close, end, pos;
	Line l;
	Entry e;
	int i, seen[32];

	o.buf = out;
	o.cap = cap;
	o.len = 0;
	o.over = 0;
	if (count > 32)
		count = 32;
	for (i = 0; i < count; i++)
		seen[i] = 0;
	if (!FindBlock(text, n, &close, &end)) {
		int any = 0;
		for (i = 0; i < count; i++)
			any |= fields[i].value[0] != '\0';
		if (!any)
			return 0;
		PutS(&o, "---");
		PutC(&o, eol);
		for (i = 0; i < count; i++)
			if (fields[i].value[0] != '\0')
				PutEntry(&o, fields[i].key, fields[i].value, kPlain, 0, eol);
		PutS(&o, "---");
		PutC(&o, eol);
		return o.over ? -1 : o.len;
	}
	LineAt(text, close, 0, &l);
	Put(&o, text + l.start, l.next - l.start);         /* the opening "---" */
	for (pos = l.next; pos < close; ) {
		LineAt(text, close, pos, &l);
		if (KeyLine(text, &l) == 0) {
			Put(&o, text + l.start, l.next - l.start);
			pos = l.next;
			continue;
		}
		EntryAt(text, close, pos, &e);
		for (i = 0; i < count; i++)
			if (KeyIs(text, &e, fields[i].key))
				break;
		if (i < count && e.style != kComplex) {
			PutEntry(&o, fields[i].key, fields[i].value, e.style, e.indent, eol);
			seen[i] = 1;
		} else {
			if (i < count)
				seen[i] = 1;
			Put(&o, text + e.start, e.next - e.start);
		}
		pos = e.next;
	}
	for (i = 0; i < count; i++)
		if (!seen[i] && fields[i].value[0] != '\0')
			PutEntry(&o, fields[i].key, fields[i].value, kPlain, 0, eol);
	Put(&o, text + pos, end - pos);                   /* the closing line */
	return o.over ? -1 : o.len;
}
