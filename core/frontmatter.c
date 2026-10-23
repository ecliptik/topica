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

/* The block: its end, where its closing line starts (*close), and whether
   it's TOML ("+++", Hugo's) rather than YAML ("---") */
static int FindBlock(const char *text, long n, long *close, long *end, int *toml)
{
	Line l;
	long pos;

	if (n < 3)
		return 0;
	LineAt(text, n, 0, &l);
	if (LineIs(text, &l, "---"))
		*toml = 0;
	else if (LineIs(text, &l, "+++"))
		*toml = 1;
	else
		return 0;
	for (pos = l.next; pos < n; pos = l.next) {
		LineAt(text, n, pos, &l);
		if (*toml ? LineIs(text, &l, "+++") : (LineIs(text, &l, "---") || LineIs(text, &l, "..."))) {
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
	int toml;

	*start = 0;
	return FindBlock(text, n, &close, end, &toml);
}

int fm_is_toml(const char *text, long n)
{
	long close, end;
	int toml;

	return FindBlock(text, n, &close, &end, &toml) && toml;
}

static int IsSpace(char c)
{
	return c == ' ' || c == '\t';
}

/* A TOML table's header ("[params]", "[[x]]"): the top-level keys end there */
static int IsTable(const char *t, const Line *l)
{
	long i = l->start;

	while (i < l->end && IsSpace(t[i]))
		i++;
	return i < l->end && t[i] == '[';
}

/* A top-level key's line: its key's length, 0 if it isn't one. */
static long KeyLine(const char *t, const Line *l, int toml)
{
	long i;

	if (l->end == l->start)
		return 0;
	if (t[l->start] == ' ' || t[l->start] == '\t' || t[l->start] == '#' || t[l->start] == '-')
		return 0;
	if (toml) {                     /* a bare key, then "=" */
		for (i = l->start; i < l->end; i++) {
			char c = t[i];
			if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
				break;
		}
		if (i == l->start)
			return 0;
		{
			long k = i;
			while (i < l->end && IsSpace(t[i]))
				i++;
			return i < l->end && t[i] == '=' ? k - l->start : 0;
		}
	}
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

/* A TOML entry at pos: one line, or a value that goes on (a multi-line
   array or string), which Post Info leaves alone */
static void TomlEntryAt(const char *t, long blockEnd, long pos, Entry *e)
{
	Line l, k;
	long v, ve, i;

	LineAt(t, blockEnd, pos, &k);
	e->start = pos;
	e->keyLen = KeyLine(t, &k, 1);
	v = k.start + e->keyLen;
	while (v < k.end && t[v] != '=')
		v++;
	v++;
	while (v < k.end && IsSpace(t[v]))
		v++;
	ve = k.end;
	e->contStart = e->next = k.next;
	e->indent = 0;
	if (ve - v >= 3 && (memcmp(t + v, "\"\"\"", 3) == 0 || memcmp(t + v, "\'\'\'", 3) == 0)) {
		const char *q = t[v] == '"' ? "\"\"\"" : "\'\'\'";
		long from = v + 3;
		for (pos = k.start; pos < blockEnd; pos = l.next) {
			LineAt(t, blockEnd, pos, &l);
			for (i = pos == k.start ? from : l.start; i + 3 <= l.end; i++)
				if (memcmp(t + i, q, 3) == 0)
					break;
			if (i + 3 <= l.end)
				break;
		}
		e->next = pos < blockEnd ? l.next : blockEnd;
		e->valStart = v;
		e->valEnd = ve;
		e->style = kComplex;
		return;
	}
	if (v < ve && t[v] == '"') {            /* a basic string: \ escapes */
		for (i = v + 1; i < ve; i++) {
			if (t[i] == '\\')
				i++;
			else if (t[i] == '"') {
				ve = i + 1;
				break;
			}
		}
	} else if (v < ve && t[v] == '\'') {    /* a literal string: none */
		for (i = v + 1; i < ve; i++)
			if (t[i] == '\'') {
				ve = i + 1;
				break;
			}
	} else {
		for (i = v; i < ve; i++)
			if (t[i] == '#') {
				ve = i;
				break;
			}
	}
	while (ve > v && IsSpace(t[ve - 1]))
		ve--;
	e->valStart = v;
	e->valEnd = ve;
	if (v < ve && t[v] == '[' && t[ve - 1] != ']') {
		/* an array over several lines: up to its "]" */
		for (pos = k.next; pos < blockEnd; pos = l.next) {
			LineAt(t, blockEnd, pos, &l);
			if (memchr(t + l.start, ']', (size_t) (l.end - l.start)) != NULL) {
				pos = l.next;
				break;
			}
		}
		e->next = pos;
		e->style = kComplex;
	} else if (v == ve || t[v] == '{') {
		e->style = kComplex;
	} else if (t[v] == '"') {
		e->style = kDouble;
	} else if (t[v] == '\'') {
		e->style = kSingle;
	} else if (t[v] == '[') {
		e->style = kFlow;
	} else {
		e->style = kPlain;
	}
}

/* The entry at pos (a key line), ending before blockEnd. */
static void EntryAt(const char *t, long blockEnd, long pos, Entry *e, int toml)
{
	Line l, k;
	long v, ve, i;
	int allDashes = 1, hasCont = 0;

	if (toml) {
		TomlEntryAt(t, blockEnd, pos, e);
		return;
	}
	LineAt(t, blockEnd, pos, &k);
	e->start = pos;
	e->keyLen = KeyLine(t, &k, 0);
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
	int toml;

	s.s = out;
	s.cap = cap;
	s.len = 0;
	if (cap > 0)
		out[0] = '\0';
	if (!FindBlock(text, n, &close, &end, &toml))
		return kFmAbsent;
	LineAt(text, close, 0, &l);
	for (pos = l.next; pos < close; ) {
		LineAt(text, close, pos, &l);
		if (toml && IsTable(text, &l))
			break;
		if (KeyLine(text, &l, toml) == 0) {
			pos = l.next;
			continue;
		}
		EntryAt(text, close, pos, &e, toml);
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

static int Digits(const char *v, int n)
{
	int i;

	for (i = 0; i < n; i++)
		if (v[i] < '0' || v[i] > '9')
			return 0;
	return 1;
}

/* 1 when TOML reads v bare as what it is: a boolean, a number, a date or a
   date and time with seconds ("2026-10-23", "2026-10-23T09:00:00-07:00");
   anything else is a string and must be quoted */
static int TomlBare(const char *v)
{
	size_t len = strlen(v), i;
	const char *p = v;
	int digits = 0, dots = 0;

	if (strcmp(v, "true") == 0 || strcmp(v, "false") == 0)
		return 1;
	if (len >= 10 && Digits(v, 4) && v[4] == '-' && Digits(v + 5, 2) && v[7] == '-' && Digits(v + 8, 2)) {
		if (len == 10)
			return 1;
		p = v + 10;
		if (!((*p == 'T' || *p == ' ') && Digits(p + 1, 2) && p[3] == ':' && Digits(p + 4, 2) && p[6] == ':'
				&& Digits(p + 7, 2)))
			return 0;
		p += 9;
		if (*p == '.')
			for (p++; *p >= '0' && *p <= '9'; p++)
				;
		if (*p == 'Z')
			p++;
		else if ((*p == '+' || *p == '-') && Digits(p + 1, 2) && p[3] == ':' && Digits(p + 4, 2))
			p += 6;
		return *p == '\0';
	}
	for (i = (*v == '+' || *v == '-') ? 1 : 0; i < len; i++) {
		if (v[i] >= '0' && v[i] <= '9')
			digits++;
		else if (v[i] == '.')
			dots++;
		else
			return 0;
	}
	return digits > 0 && dots <= 1;
}

static void PutScalar(Out *o, const char *v, long n, int style, int toml)
{
	char item[256];
	char quote = 0;
	long k = n < (long) sizeof(item) - 1 ? n : (long) sizeof(item) - 1;

	memcpy(item, v, (size_t) k);
	item[k] = '\0';
	if (style == kDouble) {
		quote = '"';
	} else if (style == kSingle) {
		/* a TOML literal string can't hold its own quote, nor a line end */
		quote = (toml && (memchr(v, '\'', (size_t) n) != NULL || memchr(v, '\r', (size_t) n) != NULL
				|| memchr(v, '\n', (size_t) n) != NULL)) ? '"' : '\'';
	} else if (toml ? !TomlBare(item) : fm_needs_quotes(item)) {
		quote = '"';
	}
	if (toml && quote == '\'') {
		PutC(o, quote);
		Put(o, v, n);
		PutC(o, quote);
		return;
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

/* Keys that Jekyll and Hugo read as true or false */
static int IsBooleanKey(const char *key)
{
	return strcmp(key, "draft") == 0 || strcmp(key, "published") == 0;
}

static int IsBoolean(const char *v)
{
	return strcmp(v, "true") == 0 || strcmp(v, "false") == 0;
}

static void PutEntry(Out *o, const char *key, const char *value, int style, long indent, char eol, int toml)
{
	long pos = 0, a, b, k;
	int first = 1;

	PutS(o, key);
	PutS(o, toml ? " =" : ":");
	if (style == kFlow) {
		PutS(o, " [");
		while (NextItem(value, &pos, &a, &b)) {
			if (!first)
				PutS(o, ", ");
			PutScalar(o, value + a, b - a, toml ? kDouble : kPlain, toml);   /* TOML: an array of strings */
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
			PutScalar(o, value + a, b - a, kPlain, 0);
			PutC(o, eol);
		}
		return;
	}
	if (value[0] != '\0') {
		PutC(o, ' ');
		if (IsBooleanKey(key) && IsBoolean(value))
			PutS(o, value);             /* draft: true stays a boolean */
		else
			PutScalar(o, value, (long) strlen(value), toml && style == kPlain && !TomlBare(value) ? kDouble : style,
					toml);
	} else if (toml) {
		PutS(o, " \"\"");               /* TOML has no empty value */
	}
	PutC(o, eol);
}

long fm_update(const char *text, long n, const FmField *fields, int count, char eol, char *out, long cap)
{
	Out o;
	long close, end, pos;
	Line l;
	Entry e;
	int i, seen[32], toml;

	o.buf = out;
	o.cap = cap;
	o.len = 0;
	o.over = 0;
	if (count > 32)
		count = 32;
	for (i = 0; i < count; i++)
		seen[i] = 0;
	if (!FindBlock(text, n, &close, &end, &toml)) {
		int any = 0;
		for (i = 0; i < count; i++)
			any |= fields[i].value[0] != '\0';
		if (!any)
			return 0;
		PutS(&o, "---");
		PutC(&o, eol);
		for (i = 0; i < count; i++)
			if (fields[i].value[0] != '\0')
				PutEntry(&o, fields[i].key, fields[i].value, kPlain, 0, eol, 0);
		PutS(&o, "---");
		PutC(&o, eol);
		return o.over ? -1 : o.len;
	}
	LineAt(text, close, 0, &l);
	Put(&o, text + l.start, l.next - l.start);         /* the opening "---" or "+++" */
	for (pos = l.next; pos < close; ) {
		LineAt(text, close, pos, &l);
		if (toml && IsTable(text, &l))
			break;                  /* new keys go before the first table, or they'd be in it */
		if (KeyLine(text, &l, toml) == 0) {
			Put(&o, text + l.start, l.next - l.start);
			pos = l.next;
			continue;
		}
		EntryAt(text, close, pos, &e, toml);
		for (i = 0; i < count; i++)
			if (KeyIs(text, &e, fields[i].key))
				break;
		if (i < count && e.style != kComplex) {
			PutEntry(&o, fields[i].key, fields[i].value, e.style, e.indent, eol, toml);
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
			PutEntry(&o, fields[i].key, fields[i].value, kPlain, 0, eol, toml);
	Put(&o, text + pos, end - pos);                   /* any tables, and the closing line */
	return o.over ? -1 : o.len;
}
