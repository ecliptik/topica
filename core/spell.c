/* spell.c: see spell.h. */
#include <stddef.h>
#include <string.h>

#include "dict.h"
#include "find.h"
#include "frontmatter.h"
#include "mdscan.h"
#include "spell.h"

#define kCurly ((char) 0xD5)            /* Mac Roman's right single quote: an apostrophe */
#define kSkip (MD_CODE | MD_URL | MD_META | MD_MARK)

static int IsDigit(unsigned char c)
{
	return c >= '0' && c <= '9';
}

/* A letter (Mac Roman's accented ones too) or a digit */
static int IsWordChar(unsigned char c)
{
	return find_is_word_char(c) && c != '_' && c != 0xA7;
}

static int IsUpper(unsigned char c)
{
	return find_lower(c) != c;
}

static int IsApostrophe(char c)
{
	return c == '\'' || c == kCurly;
}

static int InDicts(const Speller *s, const char *w, int len)
{
	return (s->user != NULL && s->user(s->ctx, w, len)) || (s->common != NULL && dict_has(s->common, w, len))
		|| (s->main != NULL && dict_has(s->main, w, len));
}

/* Right as it is, lowercase at a sentence's start, or all capitals */
static int Known(const Speller *s, const char *w, int len)
{
	char buf[DICT_WORD];
	int i, upper = 0;

	if (InDicts(s, w, len))
		return 1;
	for (i = 0; i < len; i++)
		if (IsUpper((unsigned char) w[i]))
			upper++;
	if (upper == 0)
		return 0;
	for (i = 0; i < len; i++)
		buf[i] = (char) find_lower((unsigned char) w[i]);
	if (upper == 1 && IsUpper((unsigned char) w[0]))
		return InDicts(s, buf, len);
	if (upper == len) {
		if (InDicts(s, buf, len))
			return 1;
		buf[0] = (char) find_upper((unsigned char) buf[0]);
		return InDicts(s, buf, len);
	}
	return 0;
}

int sp_correct(const Speller *s, const char *word, int len)
{
	char buf[DICT_WORD];
	int i;

	if (len <= 0 || len >= DICT_WORD)
		return len >= DICT_WORD;        /* too long to have been listed: leave it */
	for (i = 0; i < len; i++)
		buf[i] = word[i] == kCurly ? '\'' : word[i];
	if (Known(s, buf, len))
		return 1;
	/* a possessive */
	if (len > 2 && buf[len - 2] == '\'' && (buf[len - 1] == 's' || buf[len - 1] == 'S'))
		return Known(s, buf, len - 2);
	if (len > 1 && buf[len - 1] == '\'')
		return Known(s, buf, len - 1);  /* the writers' */
	return 0;
}

/* ---- suggestions ---- */

enum { kTranspose, kDelete, kReplace, kInsert, kSplit, kCase };

typedef struct {
	char word[DICT_WORD];
	int rank;
} Hit;

typedef struct {
	const Speller *s;
	int proper;                 /* the word had a capital: a proper noun can be meant */
	Hit hits[24];
	int count;
} Found;

static int Slot(unsigned char c)
{
	if (c >= 'A' && c <= 'Z')
		c = (unsigned char) (c + 'a' - 'A');
	if (c >= 'a' && c <= 'z')
		return c - 'a';
	return c == '\'' ? 26 : 27;
}

int sp_may_exist(const Speller *s, const char *word, int len)
{
	long k;

	if (s->prefixes == NULL || len < 2)
		return 1;
	k = Slot((unsigned char) word[0]) * 28 + Slot((unsigned char) word[1]);
	if (len == 2)
		return (s->prefixes[k >> 3] & (0x80 >> (k & 7))) != 0;
	k = k * 28 + Slot((unsigned char) word[2]);
	return (s->prefixes[98 + (k >> 3)] & (0x80 >> (k & 7))) != 0;
}

/* A word it might have been; known: it's from the dictionary already */
static void Found1(Found *f, const char *word, int len, int how, int known)
{
	char buf[DICT_WORD];
	const char *w = word;
	int i, common;

	if (len <= 0 || len >= DICT_WORD)
		return;
	/* the main section has every word: the common one only ranks them */
	if (!known && !(f->s->user != NULL && f->s->user(f->s->ctx, w, len))
			&& !(sp_may_exist(f->s, w, len)
				&& (f->s->main != NULL ? dict_has(f->s->main, w, len) : InDicts(f->s, w, len)))) {
		if (how != kCase || !f->proper)
			return;
		memcpy(buf, word, (size_t) len);
		buf[0] = (char) find_upper((unsigned char) buf[0]);
		w = buf;
		if (!InDicts(f->s, w, len))
			return;
	}
	for (i = 0; i < f->count; i++)
		if ((int) strlen(f->hits[i].word) == len && memcmp(f->hits[i].word, w, (size_t) len) == 0)
			return;
	if (f->count == (int) (sizeof(f->hits) / sizeof(f->hits[0])))
		return;
	common = f->s->common != NULL && dict_has(f->s->common, w, len);
	memcpy(f->hits[f->count].word, w, (size_t) len);
	f->hits[f->count].word[len] = '\0';
	/* the word itself with a capital before anything (macintosh: Macintosh,
	   not mackintosh); then common words, then by the kind of slip */
	f->hits[f->count].rank = how == kCase ? -1 : (common ? 0 : 10) + how;
	f->count++;
}

static void Add(Found *f, const char *word, int len, int how)
{
	Found1(f, word, len, how, 0);
}

/* How a is one slip away from b: kDelete (b has a letter more), kInsert
   (one less), kReplace, kTranspose; -1 if it isn't */
static int OneSlip(const char *a, int alen, const char *b, int blen)
{
	int i = 0, j;

	while (i < alen && i < blen && a[i] == b[i])
		i++;
	if (alen == blen) {
		if (i == alen)
			return -1;                  /* the same */
		if (memcmp(a + i + 1, b + i + 1, (size_t) (alen - i - 1)) == 0)
			return kReplace;
		if (i + 1 < alen && a[i] == b[i + 1] && a[i + 1] == b[i]
				&& memcmp(a + i + 2, b + i + 2, (size_t) (alen - i - 2)) == 0)
			return kTranspose;
		return -1;
	}
	if (alen + 1 == blen) {
		for (j = i; j < alen && a[j] == b[j + 1]; j++)
			;
		return j == alen ? kDelete : -1;
	}
	if (alen == blen + 1) {
		for (j = i; j < blen && a[j + 1] == b[j]; j++)
			;
		return j == blen ? kInsert : -1;
	}
	return -1;
}

typedef struct {
	Found *f;
	const char *w;                  /* the word, lowercase */
	int len;
	int capital;                    /* the scan is of capitalized words */
} Scan;

static void Scanned(void *ctx, const char *word, int len)
{
	Scan *sc = (Scan *) ctx;
	char buf[DICT_WORD];
	int how;

	if (len < sc->len - 1 || len > sc->len + 1 || len >= DICT_WORD)
		return;
	memcpy(buf, word, (size_t) len);
	if (sc->capital)
		buf[0] = (char) find_lower((unsigned char) buf[0]);
	how = OneSlip(buf, len, sc->w, sc->len);
	if (how >= 0)
		Found1(sc->f, word, len, how, 1);
}

int sp_suggest(const Speller *s, const char *word, int len, char out[][DICT_WORD], int max)
{
	static const char kLetters[] = "abcdefghijklmnopqrstuvwxyz'";
	char w[DICT_WORD], c[DICT_WORD + 1];
	Found f;
	int i, j, k, upper = 0, caps, cap, n;

	if (len <= 0 || len >= DICT_WORD - 1)
		return 0;
	for (i = 0; i < len; i++) {
		char ch = word[i] == kCurly ? '\'' : word[i];
		if (IsUpper((unsigned char) ch))
			upper++;
		w[i] = (char) find_lower((unsigned char) ch);
	}
	caps = upper == len && len > 1;
	cap = !caps && upper > 0 && IsUpper((unsigned char) word[0]);
	f.s = s;
	f.proper = cap;
	f.count = 0;
	/* a proper noun without its capital */
	memcpy(c, w, (size_t) len);
	c[0] = (char) find_upper((unsigned char) c[0]);
	Add(&f, c, len, kCase);
	/* slips in the first two letters, one at a time; after them, the words
	   that start with the same two, a block after another */
	for (i = 0; i <= len && (i < 2 || len < 3); i++) {
		if (i < len) {
			/* a letter too many */
			memcpy(c, w, (size_t) i);
			memcpy(c + i, w + i + 1, (size_t) (len - i - 1));
			Add(&f, c, len - 1, kDelete);
		}
		if (i + 1 < len) {
			/* two the wrong way round */
			memcpy(c, w, (size_t) len);
			c[i] = w[i + 1];
			c[i + 1] = w[i];
			Add(&f, c, len, kTranspose);
		}
		for (k = 0; kLetters[k]; k++) {
			if (i < len && kLetters[k] != w[i]) {
				/* the wrong letter */
				memcpy(c, w, (size_t) len);
				c[i] = kLetters[k];
				Add(&f, c, len, kReplace);
			}
			/* a letter missing */
			memcpy(c, w, (size_t) i);
			c[i] = kLetters[k];
			memcpy(c + i + 1, w + i, (size_t) (len - i));
			Add(&f, c, len + 1, kInsert);
		}
	}
	if (len >= 3 && s->main != NULL) {
		Scan sc;
		sc.f = &f;
		sc.w = w;
		sc.len = len;
		sc.capital = 0;
		dict_scan(s->main, w, 2, Scanned, &sc);
		if (cap) {
			memcpy(c, w, 2);
			c[0] = (char) find_upper((unsigned char) c[0]);
			sc.capital = 1;
			dict_scan(s->main, c, 2, Scanned, &sc);
		}
	}
	/* two words run together: when there's little else to offer, or both
	   are common words */
	for (i = 1, k = f.count; i < len && f.count < 24 && f.count < k + 2; i++) {
		int both = s->common != NULL && dict_has(s->common, w, i) && dict_has(s->common, w + i, len - i);
		if ((i > 1 || w[0] == 'a') && len - i > 1 && (both || f.count < 3)
				&& InDicts(s, w, i) && InDicts(s, w + i, len - i)) {
			memcpy(c, w, (size_t) i);
			c[i] = ' ';
			memcpy(c + i + 1, w + i, (size_t) (len - i));
			c[len + 1] = '\0';
			strcpy(f.hits[f.count].word, c);
			f.hits[f.count].rank = (both ? 0 : 10) + kSplit;
			f.count++;
		}
	}
	/* the best first, in the word's case */
	for (i = 1; i < f.count; i++)
		for (j = i; j > 0 && f.hits[j].rank < f.hits[j - 1].rank; j--) {
			Hit t = f.hits[j];
			f.hits[j] = f.hits[j - 1];
			f.hits[j - 1] = t;
		}
	for (n = 0; n < f.count && n < max; n++) {
		strcpy(out[n], f.hits[n].word);
		if (caps)
			for (j = 0; out[n][j]; j++)
				out[n][j] = (char) find_upper((unsigned char) out[n][j]);
		else if (cap)
			out[n][0] = (char) find_upper((unsigned char) out[n][0]);
	}
	return n;
}

/* ---- which words ---- */

static long LineEnd(const char *t, long n, long p)
{
	while (p < n && t[p] != '\r' && t[p] != '\n')
		p++;
	return p;
}

void sp_prepare(const char *text, long n, unsigned char *cls)
{
	static const char *const kKeys[] = { "title", "description", "summary", NULL };
	long start, end, ls, le, p;
	int k;

	if (!fm_find(text, n, &start, &end))
		return;
	for (ls = start; ls < end; ls = le + 1) {
		le = LineEnd(text, n, ls);
		for (k = 0; kKeys[k] != NULL; k++) {
			long kl = (long) strlen(kKeys[k]);
			if (le - ls > kl && memcmp(text + ls, kKeys[k], (size_t) kl) == 0) {
				/* "title:" (YAML) or "title =" (TOML): the value is prose */
				p = ls + kl;
				while (p < le && (text[p] == ' ' || text[p] == '\t'))
					p++;
				if (p < le && (text[p] == ':' || text[p] == '=') && (text[p] == '=' || p == ls + kl))
					for (p++; p < le; p++)
						cls[p] = 0;
				break;
			}
		}
	}
}

static int Gap(const char *t, const unsigned char *cls, long p)
{
	return t[p] == ' ' || t[p] == '\t' || t[p] == '\r' || t[p] == '\n' || (cls[p] & kSkip);
}

/* A run of prose characters with no space in it, around p (markup and
   addresses end it: a link's text isn't its address) */
static void Chunk(const char *t, long n, const unsigned char *cls, long p, long *a, long *b)
{
	long s = p, e = p;

	while (s > 0 && !Gap(t, cls, s - 1))
		s--;
	while (e < n && !Gap(t, cls, e))
		e++;
	*a = s;
	*b = e;
}

/* An address, a file name, a path, an e-mail, snake_case or an entity:
   none of it is prose */
static int NotProse(const char *t, long a, long b)
{
	long i;

	for (i = a; i < b; i++) {
		char c = t[i];
		if (c == '/' || c == '@' || c == '_' || c == '\\' || c == '=' || c == '#')
			return 1;
		if (c == '&' && i + 1 < b && (t[i + 1] == '#' || IsWordChar((unsigned char) t[i + 1]))) {
			long j;
			for (j = i + 1; j < b && j < i + 12 && t[j] != ';'; j++)
				;
			if (j < b && t[j] == ';')
				return 1;
		}
		if (c == '.' && i > a && i + 1 < b && IsWordChar((unsigned char) t[i - 1]) && IsWordChar((unsigned char) t[i + 1]))
			return 1;
	}
	return 0;
}

long sp_next(const char *text, long n, long from, const unsigned char *cls, int *len)
{
	const unsigned char *t = (const unsigned char *) text;
	long p = from, s, e, a, b, i;
	int digits, upper, inner;

	while (p > 0 && p < n && IsWordChar(t[p - 1]) && IsWordChar(t[p]))
		p--;
	while (p < n) {
		if (!IsWordChar(t[p]) || (cls[p] & kSkip)) {
			p++;
			continue;
		}
		/* a word: letters and digits, with apostrophes inside */
		s = p;
		e = p;
		while (e < n && (IsWordChar(t[e]) || (IsApostrophe(text[e]) && e + 1 < n && IsWordChar(t[e + 1])
				&& e > s)))
			e++;
		p = e;
		Chunk(text, n, cls, s, &a, &b);
		if (NotProse(text, a, b)) {
			p = b;
			continue;
		}
		digits = upper = inner = 0;
		for (i = s; i < e; i++) {
			if (cls[i] & kSkip)
				break;
			if (IsDigit(t[i]))
				digits++;
			if (IsUpper(t[i])) {
				upper++;
				if (i > s)
					inner = 1;
			}
		}
		if (i < e || digits > 0 || e - s < 2)
			continue;
		if (upper == e - s && e - s <= 5)
			continue;                   /* an acronym: HTML, CSS */
		if (inner && upper < e - s)
			continue;                   /* iPhone, JavaScript */
		*len = (int) (e - s);
		return s;
	}
	return -1;
}
