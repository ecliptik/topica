/* Host tests for Topica's portable core: make -C tests test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/dict.h"
#include "../core/find.h"
#include "../core/frontmatter.h"
#include "../core/layout.h"
#include "../core/mdhtml.h"
#include "../core/mdpage.h"
#include "../core/mdscan.h"
#include "../core/prefs.h"
#include "../core/publish.h"
#include "../core/recovery.h"
#include "../core/spell.h"
#include "../core/post.h"
#include "../core/textconv.h"
#include "../core/undo.h"
#include "../core/vi.h"

static int gFailed, gRun;

#define CHECK(cond) do { gRun++; if (!(cond)) { gFailed++; \
	printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void CheckStr(const char *got, const char *want, int line)
{
	gRun++;
	if (strcmp(got, want) != 0) {
		gFailed++;
		printf("FAIL line %d:\n  got  [%s]\n  want [%s]\n", line, got, want);
	}
}
#define CHECK_STR(got, want) CheckStr((got), (want), __LINE__)

/* ---- textconv ---- */

static void RoundTrip(const char *disk, int line)
{
	char mac[512], back[512];
	long n = (long) strlen(disk), m, b;

	m = tc_to_mac(disk, n, mac, sizeof(mac), tc_is_utf8(disk, n));
	CHECK(m >= 0);
	CHECK(tc_to_mac(disk, n, NULL, 0, tc_is_utf8(disk, n)) == m);
	b = tc_to_disk(mac, m, back, sizeof(back) - 1, 1, 1);
	CHECK(b >= 0);
	CHECK(tc_to_disk(mac, m, NULL, 0, 1, 1) == b);
	back[b < 0 ? 0 : b] = '\0';
	CheckStr(back, disk, line);
}

static void TestTextconv(void)
{
	char out[256];
	long n;

	CHECK(tc_is_utf8("plain", 5));
	CHECK(tc_is_utf8("caf\xC3\xA9", 5));
	CHECK(!tc_is_utf8("caf\x8E", 4));               /* Mac Roman e-acute */
	CHECK(!tc_is_utf8("\xC0\x80", 2));              /* overlong */
	CHECK(!tc_is_utf8("\xED\xA0\x80", 3));          /* a surrogate */
	CHECK(!tc_is_utf8("\xE2\x80", 2));              /* cut short */

	/* line ends: LF and CRLF become CR */
	n = tc_to_mac("a\nb\r\nc\rd", 8, out, sizeof(out), 1);
	out[n] = '\0';
	CHECK_STR(out, "a\rb\rc\rd");

	/* UTF-8 to Mac Roman: e-acute, em dash, curly quotes */
	{
		const char *q = "caf\xC3\xA9 \xE2\x80\x94 \xE2\x80\x9Chi\xE2\x80\x9D";
		n = tc_to_mac(q, (long) strlen(q), out, sizeof(out), 1);
	}
	out[n] = '\0';
	CHECK_STR(out, "caf\x8E \xD1 \xD2hi\xD3");

	/* characters Mac Roman lacks: fullwidth a, an emoji, an arrow */
	n = tc_to_mac("\xEF\xBD\x81 \xF0\x9F\x98\x80 \xE2\x86\x92", 12, out, sizeof(out), 1);
	out[n] = '\0';
	CHECK_STR(out, "&#xFF41; &#x01F600; &#x2192;");

	/* a byte-order mark at the start is dropped */
	n = tc_to_mac("\xEF\xBB\xBFhi", 5, out, sizeof(out), 1);
	out[n] = '\0';
	CHECK_STR(out, "hi");

	/* Mac Roman files open as they are */
	n = tc_to_mac("caf\x8E\n", 5, out, sizeof(out), 0);
	out[n] = '\0';
	CHECK_STR(out, "caf\x8E\r");

	/* round trips give back the same bytes */
	RoundTrip("---\ntitle: Hello\n---\n\nBody\n", __LINE__);
	RoundTrip("caf\xC3\xA9 \xE2\x80\x94 na\xC3\xAFve \xE2\x80\x9Cquoted\xE2\x80\x9D \xE2\x80\xA6\n", __LINE__);
	RoundTrip("\xEF\xBD\x81\xEF\xBD\x85\xEF\xBD\x93 \xF0\x9F\x98\x80 \xE2\x86\x92 \xE2\x82\xAC\n", __LINE__);
	RoundTrip("an entity typed by hand: &#x2014; and &amp; and &#x41;\n", __LINE__);

	/* a hand-typed entity for a character Mac Roman has stays as typed */
	n = tc_to_disk("&#x2014;", 8, out, sizeof(out), 1, 1);
	out[n] = '\0';
	CHECK_STR(out, "&#x2014;");
	/* one for a character it lacks becomes UTF-8, lowercase hex too */
	n = tc_to_disk("&#xff41;", 8, out, sizeof(out), 1, 1);
	out[n] = '\0';
	CHECK_STR(out, "\xEF\xBD\x81");
	/* not entities */
	n = tc_to_disk("&#x; &#xZZ; &#x110000; &#xD800;", 31, out, sizeof(out), 1, 1);
	out[n] = '\0';
	CHECK_STR(out, "&#x; &#xZZ; &#x110000; &#xD800;");

	/* CR line ends kept when asked; Mac Roman out */
	n = tc_to_disk("a\rb\x8E", 4, out, sizeof(out), 0, 0);
	out[n] = '\0';
	CHECK_STR(out, "a\rb\x8E");

	/* a buffer too small */
	CHECK(tc_to_mac("hello", 5, out, 3, 1) == -1);
	CHECK(tc_to_disk("\x8E\x8E", 2, out, 3, 1, 1) == -1);
	CHECK(tc_roman_from_cp(0x2014) == 0xD1);
	CHECK(tc_roman_from_cp(0x20AC) == -1);
	CHECK(tc_cp_from_roman(0xD1) == 0x2014);
}

/* ---- frontmatter ---- */

static const char kPost[] =
	"---\n"
	"layout: post\n"
	"title: Vibe Coding a Compositor\n"
	"description: \"How I used it: a \\\"story\\\"\"\n"
	"category: linux\n"
	"tags: linux ai claude\n"
	"redirect_from: /old-url/\n"
	"# a comment\n"
	"---\n"
	"\n"
	"Body text here.\n";

static void TestFrontmatterGet(void)
{
	char v[128];
	long s, e;
	const char *list =
		"---\r"
		"tags: [one, \"two words\", three]\r"
		"categories:\r"
		"  - mac\r"
		"  - retro\r"
		"title: 'It''s here' # comment\r"
		"summary: |\r"
		"  long text\r"
		"plain: value # comment\r"
		"empty:\r"
		"...\r"
		"rest";

	CHECK(fm_find(kPost, (long) strlen(kPost), &s, &e));
	CHECK(s == 0);
	CHECK(strncmp(kPost + e, "\nBody", 5) == 0);
	CHECK(fm_get(kPost, (long) strlen(kPost), "title", v, sizeof(v)) == kFmFound);
	CHECK_STR(v, "Vibe Coding a Compositor");
	CHECK(fm_get(kPost, (long) strlen(kPost), "description", v, sizeof(v)) == kFmFound);
	CHECK_STR(v, "How I used it: a \"story\"");
	CHECK(fm_get(kPost, (long) strlen(kPost), "tags", v, sizeof(v)) == kFmFound);
	CHECK_STR(v, "linux ai claude");
	CHECK(fm_get(kPost, (long) strlen(kPost), "date", v, sizeof(v)) == kFmAbsent);
	CHECK_STR(v, "");
	/* a key in the body isn't front matter */
	CHECK(fm_get("---\na: 1\n---\nb: 2\n", 17, "b", v, sizeof(v)) == kFmAbsent);

	CHECK(fm_find(list, (long) strlen(list), &s, &e));
	CHECK_STR(list + e, "rest");
	CHECK(fm_get(list, (long) strlen(list), "tags", v, sizeof(v)) == kFmFound);
	CHECK_STR(v, "one two words three");
	CHECK(fm_get(list, (long) strlen(list), "categories", v, sizeof(v)) == kFmFound);
	CHECK_STR(v, "mac retro");
	CHECK(fm_get(list, (long) strlen(list), "title", v, sizeof(v)) == kFmFound);
	CHECK_STR(v, "It's here");
	CHECK(fm_get(list, (long) strlen(list), "summary", v, sizeof(v)) == kFmComplex);
	CHECK(fm_get(list, (long) strlen(list), "plain", v, sizeof(v)) == kFmFound);
	CHECK_STR(v, "value");
	CHECK(fm_get(list, (long) strlen(list), "empty", v, sizeof(v)) == kFmFound);
	CHECK_STR(v, "");

	/* no front matter: no "---" first, or never closed */
	CHECK(!fm_find("Hello\n---\n", 10, &s, &e));
	CHECK(!fm_find("---\ntitle: x\n", 13, &s, &e));
	/* a value cut to the buffer */
	CHECK(fm_get(kPost, (long) strlen(kPost), "title", v, 5) == kFmFound);
	CHECK_STR(v, "Vibe");
}

static void TestFrontmatterToml(void)
{
	char v[128];
	long s, e;
	const char *t =
		"+++\n"
		"title = \"Vibe \\\"Coding\\\"\"\n"
		"date = 2026-10-23T09:00:00-07:00\n"
		"tags = [\"mac\", 'retro', \"two words\"]\n"
		"draft = true\n"
		"description = 'C:\\temp'\n"
		"weight=5 # comment\n"
		"summary = \"\"\"\n"
		"title = \"inside\"\n"
		"\"\"\"\n"
		"categories = [\n"
		"  \"a\",\n"
		"]\n"
		"empty = \"\"\n"
		"---\n"
		"[params]\n"
		"author = \"x\"\n"
		"+++\n"
		"body = 1\n";
	long n = (long) strlen(t);

	CHECK(fm_find(t, n, &s, &e) && s == 0);
	CHECK_STR(t + e, "body = 1\n");
	CHECK(fm_is_toml(t, n));
	CHECK(!fm_is_toml(kPost, (long) strlen(kPost)));
	CHECK(fm_get(t, n, "title", v, sizeof(v)) == kFmFound);
	CHECK_STR(v, "Vibe \"Coding\"");
	CHECK(fm_get(t, n, "date", v, sizeof(v)) == kFmFound);
	CHECK_STR(v, "2026-10-23T09:00:00-07:00");
	CHECK(fm_get(t, n, "tags", v, sizeof(v)) == kFmFound);
	CHECK_STR(v, "mac retro two words");
	CHECK(fm_get(t, n, "draft", v, sizeof(v)) == kFmFound);
	CHECK_STR(v, "true");
	CHECK(fm_get(t, n, "description", v, sizeof(v)) == kFmFound);
	CHECK_STR(v, "C:\\temp");                        /* a literal string has no escapes */
	CHECK(fm_get(t, n, "weight", v, sizeof(v)) == kFmFound);
	CHECK_STR(v, "5");
	CHECK(fm_get(t, n, "summary", v, sizeof(v)) == kFmComplex);
	CHECK(fm_get(t, n, "categories", v, sizeof(v)) == kFmComplex);
	CHECK(fm_get(t, n, "empty", v, sizeof(v)) == kFmFound);
	CHECK_STR(v, "");
	CHECK(fm_get(t, n, "author", v, sizeof(v)) == kFmAbsent);   /* in a table */
	CHECK(fm_get(t, n, "body", v, sizeof(v)) == kFmAbsent);
	/* "---" doesn't close TOML, nor "+++" YAML */
	{
		const char *a = "+++\ntitle = \"x\"\n---\n", *b = "---\ntitle: x\n+++\n";
		CHECK(!fm_find(a, (long) strlen(a), &s, &e));
		CHECK(!fm_find(b, (long) strlen(b), &s, &e));
	}
}

static void Update(const char *text, const FmField *f, int count, char eol, const char *want, int line)
{
	char out[1024];
	long n = (long) strlen(text), len, s = 0, e = 0;

	len = fm_update(text, n, f, count, eol, NULL, 0);
	gRun++;
	if (len != fm_update(text, n, f, count, eol, out, sizeof(out) - 1)) {
		gFailed++;
		printf("FAIL line %d: measured and written lengths differ\n", line);
	}
	if (len < 0)
		len = 0;
	out[len] = '\0';
	/* the whole text after the edit */
	{
		char whole[2048];
		if (fm_find(text, n, &s, &e))
			sprintf(whole, "%s%s", out, text + e);
		else
			sprintf(whole, "%s%s", out, text);
		CheckStr(whole, want, line);
	}
}

static void TestFrontmatterUpdate(void)
{
	FmField f[5];
	char out[16];

	f[0].key = "title";       f[0].value = "A New Title: with colon";
	f[1].key = "tags";        f[1].value = "mac retro";
	f[2].key = "description"; f[2].value = "Say \"hi\"";
	f[3].key = "category";    f[3].value = "";
	f[4].key = "date";        f[4].value = "2026-10-22 09:00";
	Update(kPost, f, 5, '\n',
		"---\n"
		"layout: post\n"
		"title: \"A New Title: with colon\"\n"
		"description: \"Say \\\"hi\\\"\"\n"
		"category:\n"
		"tags: mac retro\n"
		"redirect_from: /old-url/\n"
		"# a comment\n"
		"date: 2026-10-22 09:00\n"
		"---\n"
		"\n"
		"Body text here.\n", __LINE__);

	/* list styles are kept; complex values are left alone */
	f[0].key = "tags";       f[0].value = "a b c";
	f[1].key = "categories"; f[1].value = "x";
	f[2].key = "summary";    f[2].value = "ignored";
	Update("---\rtags: [one, two]\rcategories:\r  - mac\r  - retro\rsummary: |\r  long\r---\rBody",
		f, 3, '\r',
		"---\rtags: [a, b, c]\rcategories:\r  - x\rsummary: |\r  long\r---\rBody", __LINE__);

	/* no front matter: a new block, empty fields left out */
	f[0].key = "title"; f[0].value = "Hello";
	f[1].key = "tags";  f[1].value = "";
	Update("Body\n", f, 2, '\n', "---\ntitle: Hello\n---\nBody\n", __LINE__);
	/* nothing to write */
	f[0].value = "";
	Update("Body\n", f, 2, '\n', "Body\n", __LINE__);
	/* a single-quoted value stays single-quoted */
	f[0].key = "title"; f[0].value = "Don't";
	Update("---\ntitle: 'old'\n---\n", f, 1, '\n', "---\ntitle: 'Don''t'\n---\n", __LINE__);
	/* a value with a line end in it stays on one line */
	f[0].value = "two\rlines";
	Update("---\ntitle: x\n---\n", f, 1, '\n', "---\ntitle: two lines\n---\n", __LINE__);
	/* booleans Jekyll and Hugo read stay unquoted; a title "true" is quoted */
	f[0].key = "draft"; f[0].value = "false";
	f[1].key = "title"; f[1].value = "true";
	Update("---\ndraft: true\n---\n", f, 2, '\n', "---\ndraft: false\ntitle: \"true\"\n---\n", __LINE__);
	/* too small */
	CHECK(fm_update(kPost, (long) strlen(kPost), f, 1, '\n', out, sizeof(out)) == -1);

	/* TOML: strings quoted, booleans and dates bare, new keys before the first table */
	f[0].key = "title";       f[0].value = "New: \"quoted\"";
	f[1].key = "tags";        f[1].value = "mac retro";
	f[2].key = "draft";       f[2].value = "true";
	f[3].key = "date";        f[3].value = "2026-10-24";
	f[4].key = "description"; f[4].value = "Hello";
	Update("+++\ntitle = \"Old\"\ntags = [\"a\"]\ndraft = false\ndate = 2026-10-23\nsummary = \"\"\"\nx\n\"\"\"\n"
		"[params]\nauthor = \"x\"\n+++\nBody\n", f, 5, '\n',
		"+++\ntitle = \"New: \\\"quoted\\\"\"\ntags = [\"mac\", \"retro\"]\ndraft = true\ndate = 2026-10-24\n"
		"summary = \"\"\"\nx\n\"\"\"\ndescription = \"Hello\"\n[params]\nauthor = \"x\"\n+++\nBody\n", __LINE__);
	/* a literal string stays literal, unless the value has a quote; a bare date that isn't one is quoted;
	   an emptied key is "" */
	f[0].key = "title";    f[0].value = "Plain";
	f[1].key = "subtitle"; f[1].value = "Don't";
	f[2].key = "date";     f[2].value = "next week";
	f[3].key = "category"; f[3].value = "";
	Update("+++\ntitle = 'old'\nsubtitle = 'old'\ndate = 2026-10-23\ncategory = \"x\"\n+++\n", f, 4, '\n',
		"+++\ntitle = 'Plain'\nsubtitle = \"Don't\"\ndate = \"next week\"\ncategory = \"\"\n+++\n", __LINE__);
	/* new keys go above blank lines before a table or the end */
	f[0].key = "title"; f[0].value = "T";
	f[1].key = "category"; f[1].value = "retro";
	Update("+++\ntitle = \"\"\n\n[params]\na = 1\n+++\n", f, 2, '\n',
		"+++\ntitle = \"T\"\ncategory = \"retro\"\n\n[params]\na = 1\n+++\n", __LINE__);
	Update("---\ntitle: x\n\n---\n", f, 2, '\n', "---\ntitle: T\ncategory: retro\n\n---\n", __LINE__);
	Update("---\ntitle: x\n\nlayout: post\n---\n", f, 2, '\n', "---\ntitle: T\n\nlayout: post\ncategory: retro\n---\n", __LINE__);
	/* a new key: a date with seconds is bare, without them a string */
	f[0].key = "date"; f[0].value = "2026-10-24 09:00:00";
	Update("+++\n+++\n", f, 1, '\n', "+++\ndate = 2026-10-24 09:00:00\n+++\n", __LINE__);
	f[0].value = "2026-10-24 09:00";
	Update("+++\n+++\n", f, 1, '\n', "+++\ndate = \"2026-10-24 09:00\"\n+++\n", __LINE__);

	CHECK(fm_needs_quotes("yes"));
	CHECK(fm_needs_quotes("True"));
	CHECK(fm_needs_quotes("1984"));
	CHECK(fm_needs_quotes("3.14"));
	CHECK(fm_needs_quotes("- dash"));
	CHECK(fm_needs_quotes("#tag"));
	CHECK(fm_needs_quotes("a: b"));
	CHECK(fm_needs_quotes(" lead"));
	CHECK(!fm_needs_quotes("2026-10-22"));
	CHECK(!fm_needs_quotes("Plain title, with comma"));
	CHECK(!fm_needs_quotes("caf\x8E"));
	CHECK(!fm_needs_quotes(""));
}

/* ---- undo ---- */

/* A tiny editor: the text and the undo stack, changes applied as the app does */
static char gDoc[256];
static long gLen;

static void Replace(UndoStack *u, int kind, long pos, long dlen, const char *ins, long ilen)
{
	undo_record(u, kind, pos, gDoc + pos, dlen, ins, ilen);
	memmove(gDoc + pos + ilen, gDoc + pos + dlen, (size_t) (gLen - pos - dlen));
	memcpy(gDoc + pos, ins, (size_t) ilen);
	gLen += ilen - dlen;
	gDoc[gLen] = '\0';
}

static void Type(UndoStack *u, const char *s)
{
	while (*s)
		Replace(u, kUndoTyping, gLen, 0, s++, 1);
}

static int Step(UndoStack *u, int redo)
{
	UndoApply a;
	char tmp[256];

	if (!undo_step(u, redo, &a))
		return 0;
	memcpy(tmp, a.insert, (size_t) a.insertLen);
	memmove(gDoc + a.pos + a.insertLen, gDoc + a.pos + a.removeLen, (size_t) (gLen - a.pos - a.removeLen));
	memcpy(gDoc + a.pos, tmp, (size_t) a.insertLen);
	gLen += a.insertLen - a.removeLen;
	gDoc[gLen] = '\0';
	return a.kind;
}

/* Back to the saved text, by undo or redo, is unchanged again */
static void TestUndoSaved(void)
{
	UndoStack u;
	char buf[64];
	int i;

	undo_init(&u, buf, sizeof(buf));
	gLen = 0;
	gDoc[0] = '\0';
	CHECK(undo_at_saved(&u));               /* as opened */
	Type(&u, "ab");
	CHECK(!undo_at_saved(&u));
	(void) Step(&u, 0);
	CHECK(undo_at_saved(&u));               /* undone back to it */
	(void) Step(&u, 1);
	undo_mark_saved(&u);                    /* saved "ab" */
	CHECK(undo_at_saved(&u));
	Type(&u, "c");                          /* typing after a save is its own step */
	CHECK(u.count == 2 && !undo_at_saved(&u));
	(void) Step(&u, 0);
	CHECK(undo_at_saved(&u) && strcmp(gDoc, "ab") == 0);
	(void) Step(&u, 0);
	CHECK(!undo_at_saved(&u));              /* before the save */
	(void) Step(&u, 1);
	CHECK(undo_at_saved(&u));               /* redone back to it */
	/* undo past the save, then change: the saved text can't come back */
	(void) Step(&u, 0);
	undo_seal(&u);
	Type(&u, "x");
	(void) Step(&u, 0);
	CHECK(!undo_at_saved(&u));
	/* the saved step falls off the history: gone too */
	undo_clear(&u);
	gLen = 0;
	gDoc[0] = '\0';
	Type(&u, "a");
	undo_mark_saved(&u);
	for (i = 0; i < 8; i++) {
		undo_seal(&u);
		Type(&u, "0123456789");
	}
	CHECK(u.lost && u.saved == -1);
}

/* A checker's corrections, joined: one undo and one redo take them all */
static void TestUndoJoin(void)
{
	UndoStack u;
	char buf[128];
	UndoApply a;

	undo_init(&u, buf, sizeof(buf));
	undo_record(&u, kUndoSpelling, 0, "Inline", 6, "Online", 6);
	undo_join(&u);
	undo_record(&u, kUndoSpelling, 7, "b", 1, "bee", 3);
	undo_group(&u, 0);
	undo_seal(&u);
	undo_record(&u, kUndoTyping, 10, "", 0, "x", 1);
	CHECK(undo_step(&u, 0, &a) && a.kind == kUndoTyping && !a.more);
	CHECK(undo_step(&u, 0, &a) && a.kind == kUndoSpelling && a.pos == 7 && a.more);
	CHECK(undo_step(&u, 0, &a) && a.pos == 0 && !a.more);
	CHECK(undo_step(&u, 1, &a) && a.pos == 0 && a.more);
	CHECK(undo_step(&u, 1, &a) && a.pos == 7 && !a.more);
	/* nothing before it: the first step stands alone */
	undo_clear(&u);
	undo_join(&u);
	undo_record(&u, kUndoSpelling, 0, "a", 1, "b", 1);
	undo_group(&u, 0);
	CHECK(undo_step(&u, 0, &a) && !a.more);
}

static void TestUndo(void)
{
	UndoStack u;
	char buf[96];
	UndoApply a;

	undo_init(&u, buf, sizeof(buf));
	gLen = 0;
	gDoc[0] = '\0';
	CHECK(undo_peek(&u, 0) == kUndoNone);
	CHECK(!undo_step(&u, 0, &a));

	Type(&u, "hello");
	CHECK(u.count == 1);                    /* one typing step */
	CHECK(undo_peek(&u, 0) == kUndoTyping);
	undo_seal(&u);
	Type(&u, " world");
	CHECK(u.count == 2);
	CHECK(Step(&u, 0) == kUndoTyping);
	CHECK_STR(gDoc, "hello");
	CHECK(undo_peek(&u, 1) == kUndoTyping);
	CHECK(Step(&u, 1) == kUndoTyping);
	CHECK_STR(gDoc, "hello world");

	/* Delete after typing takes back the typing */
	undo_seal(&u);
	Type(&u, "!!");
	Replace(&u, kUndoDelete, gLen - 1, 1, "", 0);
	CHECK(u.count == 3);
	CHECK_STR(gDoc, "hello world!");
	Replace(&u, kUndoDelete, gLen - 1, 1, "", 0);
	CHECK(u.count == 2);                    /* the step is gone */
	/* more Deletes make one Delete step */
	Replace(&u, kUndoDelete, gLen - 1, 1, "", 0);
	Replace(&u, kUndoDelete, gLen - 1, 1, "", 0);
	CHECK(u.count == 3);
	CHECK_STR(gDoc, "hello wor");
	CHECK(Step(&u, 0) == kUndoDelete);
	CHECK_STR(gDoc, "hello world");

	/* a new change drops the redo steps */
	Replace(&u, kUndoPaste, 0, 5, "HELLO", 5);
	CHECK(undo_peek(&u, 1) == kUndoNone);
	CHECK(Step(&u, 0) == kUndoPaste);
	CHECK_STR(gDoc, "hello world");
	CHECK(Step(&u, 0) == kUndoTyping);
	CHECK(Step(&u, 0) == kUndoTyping);
	CHECK_STR(gDoc, "");
	CHECK(!u.lost);

	/* a full buffer drops the oldest steps */
	undo_clear(&u);
	gLen = 0;
	gDoc[0] = '\0';
	Replace(&u, kUndoPaste, 0, 0, "0123456789012345678901234567890", 31);
	Replace(&u, kUndoPaste, 0, 0, "abcdefghijabcdefghijabcdefghija", 31);
	Replace(&u, kUndoPaste, 0, 0, "ABCDEFGHIJABCDEFGHIJABCDEFGHIJA", 31);
	CHECK(u.lost);
	CHECK(u.count == 2);
	CHECK(u.used <= u.cap);
	Step(&u, 0);
	Step(&u, 0);
	CHECK(!undo_step(&u, 0, &a));
	CHECK_STR(gDoc, "0123456789012345678901234567890");

	/* a step bigger than the buffer clears it */
	{
		char big[200];
		memset(big, 'x', sizeof(big));
		undo_record(&u, kUndoPaste, 0, "", 0, big, sizeof(big));
		CHECK(u.count == 0 && u.lost);
	}

	/* typing over a selection is a new step that restores the selection */
	undo_clear(&u);
	strcpy(gDoc, "abc");
	gLen = 3;
	Replace(&u, kUndoTyping, 1, 1, "X", 1);
	Type(&u, "YZ");
	CHECK_STR(gDoc, "aXcYZ");
	CHECK(u.count == 2);                    /* YZ wasn't at the step's end */
	undo_step(&u, 0, &a);
	undo_step(&u, 0, &a);
	CHECK(a.selStart == 1 && a.selEnd == 2);
}

/* ---- post ---- */

static void TestPost(void)
{
	char s[64];
	char t[128];
	long n;

	post_slug("Vibe Coding a Fluxbox-Inspired Wayland Compositor", s, sizeof(s));
	CHECK_STR(s, "vibe-coding-a-fluxbox-inspired-wayland-compositor");
	post_slug("  Hello, W\x9Arld! Don't  stop ", s, sizeof(s));
	CHECK_STR(s, "hello-world-dont-stop");
	post_slug("\xAE" "sthetics & \xBE" "ther", s, sizeof(s));
	CHECK_STR(s, "aesthetics-aether");
	post_slug("one two three", s, 10);
	CHECK_STR(s, "one-two");
	post_slug("!!!", s, sizeof(s));
	CHECK_STR(s, "");

	post_filename(2026, 10, 22, "Three Days with Claude Code", 0, s, sizeof(s));
	CHECK_STR(s, "2026-10-22-three-days-with-claude-code.md");
	post_filename(2026, 10, 22, "Three Days with Claude Code", 31, s, sizeof(s));
	CHECK_STR(s, "2026-10-22-three-days-with.md");
	CHECK(strlen(s) <= 31);
	post_filename(0, 1, 1, "Three Days with Claude Code", 31, s, sizeof(s));
	CHECK_STR(s, "three-days-with-claude-code.md");
	post_filename(2026, 1, 2, "", 31, s, sizeof(s));
	CHECK_STR(s, "2026-01-02-untitled.md");
	post_filename(2026, 1, 2, "Supercalifragilisticexpialidocious", 31, s, sizeof(s));
	CHECK(strlen(s) <= 31);

	n = post_template(0, "post", NULL, '\r', t, sizeof(t) - 1);
	CHECK(n > 0);
	t[n] = '\0';
	CHECK_STR(t, "---\rlayout: post\rtitle:\rdescription:\rcategory:\rtags:\r---\r\r");
	CHECK(post_template(0, "post", NULL, '\r', t, 5) == -1);
	n = post_template(1, "post", "2026-10-22", '\n', t, sizeof(t) - 1);
	t[n < 0 ? 0 : n] = '\0';
	CHECK_STR(t, "---\ntitle:\ndate: 2026-10-22\ndraft: true\ndescription:\ncategories:\ntags:\n---\n\n");
	n = post_template(1, NULL, "", '\n', t, sizeof(t) - 1);
	t[n < 0 ? 0 : n] = '\0';
	CHECK_STR(t, "---\ntitle:\ndate:\ndraft: true\ndescription:\ncategories:\ntags:\n---\n\n");

	{
		const char *tpl = "+++\rdate = {{date}}\rtags = [\"mac\"]\r+++\r\r{{date}}{{dat";
		n = post_fill(tpl, (long) strlen(tpl), "2026-10-23", NULL, 0);
		CHECK(n == post_fill(tpl, (long) strlen(tpl), "2026-10-23", t, sizeof(t) - 1));
		t[n < 0 ? 0 : n] = '\0';
		CHECK_STR(t, "+++\rdate = 2026-10-23\rtags = [\"mac\"]\r+++\r\r2026-10-23{{dat");
		CHECK(post_fill(tpl, (long) strlen(tpl), "2026-10-23", t, 10) == -1);
		CHECK(post_fill("", 0, "x", t, 1) == 0);
	}

	CHECK(post_words("---\ntitle: a b c\n---\nOne two, three-four don't.\n", 46) == 4);
	CHECK(post_words("caf\x8E na\x95ve \xD1 r\x8Esum\x8E", 18) == 3);
	CHECK(post_words("", 0) == 0);
	CHECK(post_minutes(0) == 0);
	CHECK(post_minutes(1) == 1);
	CHECK(post_minutes(400) == 2);
	CHECK(post_minutes(401) == 3);
}

/* ---- find ---- */

static void TestFind(void)
{
	const char *t = "The cat sat. Concatenate CAT! caf\x8E Caf\x83";
	long n = (long) strlen(t), count;
	char out[128];

	CHECK(find_next(t, n, "cat", 3, 0, 0) == 4);
	CHECK(find_next(t, n, "cat", 3, 5, 0) == 16);               /* inside Concatenate */
	CHECK(find_next(t, n, "cat", 3, 5, kFindWord) == 25);       /* CAT, a whole word */
	CHECK(find_next(t, n, "cat", 3, 5, kFindWord | kFindCase) == 4);   /* wraps round */
	CHECK(find_next(t, n, "dog", 3, 0, 0) == -1);
	CHECK(find_next(t, n, "caf\x8E", 4, 0, 0) == 30);
	CHECK(find_next(t, n, "caf\x8E", 4, 31, 0) == 35);         /* E-acute finds e-acute */
	CHECK(find_next(t, n, "caf\x8E", 4, 31, kFindCase) == 30);
	CHECK(find_prev(t, n, "cat", 3, 16, 0) == 4);
	CHECK(find_prev(t, n, "cat", 3, 3, 0) == 25);               /* wraps to the end */
	CHECK(find_is_match(t, n, "CAT", 3, 4, 0));
	CHECK(!find_is_match(t, n, "CAT", 3, 4, kFindCase));
	CHECK(!find_is_match(t, n, "cat", 3, 16, kFindWord));
	CHECK(find_next(t, n, "", 0, 0, 0) == -1);
	CHECK(find_lower(0x83) == 0x8E);
	CHECK(find_is_word_char(0x8E) && !find_is_word_char(0xD1));

	n = find_replace_all(t, (long) strlen(t), "cat", 3, "dog", 3, kFindWord, out, sizeof(out) - 1, &count);
	CHECK(n == (long) strlen(t) && count == 2);
	out[n < 0 ? 0 : n] = '\0';
	CHECK_STR(out, "The dog sat. Concatenate dog! caf\x8E Caf\x83");
	n = find_replace_all("aaa", 3, "a", 1, "bb", 2, 0, out, sizeof(out) - 1, &count);
	out[n < 0 ? 0 : n] = '\0';
	CHECK_STR(out, "bbbbbb");
	CHECK(count == 3);
	CHECK(find_replace_all("aaa", 3, "a", 1, "bb", 2, 0, out, 4, &count) == -1);
	CHECK(find_replace_all("aaa", 3, "a", 1, "", 0, 0, NULL, 0, &count) == 0);
}

/* ---- prefs ---- */

static void TestPrefs(void)
{
	TopicaPrefs p, q;
	char buf[1024];
	long n;

	prefs_default(&p);
	CHECK_STR(p.font, "Monaco");
	CHECK(p.size == 9 && p.utf8 && p.lf && p.autosaveMinutes == 2 && p.profile == kProfileJekyll && p.format);
	CHECK(!p.vi && !p.viJk && p.bridge[0] == '\0' && p.bridgeKey[0] == '\0' && !p.yearFolders);
	{
		const char *text = "publish-bridge: 10.0.2.2:8023\rpublish-key: a b:c\r# a comment\rfont: Geneva\rsize: 12\rblog: hugo\rencoding: mac-roman\r"
			"line-ends: cr\rautosave-minutes: 5\rimage-folder: /img/\rlayout: page\runknown: x\rformatting: off\rvi: on\r"
			"publish-year-folders: on\rspelling-checker: Disk:Apps:Excalibur 2.6\rtheme: Solarized Dark\r";
		prefs_read(&p, text, (long) strlen(text));
	}
	CHECK_STR(p.font, "Geneva");
	CHECK(p.size == 12 && p.profile == kProfileHugo && !p.utf8 && !p.lf && p.autosaveMinutes == 5 && !p.format);
	CHECK(p.vi && !p.viJk && p.yearFolders);
	CHECK_STR(p.wordService, "Disk:Apps:Excalibur 2.6");
	CHECK_STR(p.theme, "Solarized Dark");
	CHECK_STR(p.imagePath, "/img/");
	CHECK_STR(p.layout, "page");
	CHECK_STR(p.bridge, "10.0.2.2:8023");
	CHECK_STR(p.bridgeKey, "a b:c");
	/* bad numbers keep what was there */
	prefs_read(&p, "size: 99\nautosave-minutes: x\n", (long) strlen("size: 99\nautosave-minutes: x\n"));
	CHECK(p.size == 12 && p.autosaveMinutes == 5);
	/* written and read back */
	n = prefs_write(&p, '\r', buf, sizeof(buf));
	CHECK(n > 0 && n == prefs_write(&p, '\r', NULL, 0));
	prefs_default(&q);
	prefs_read(&q, buf, n);
	CHECK(memcmp(&p, &q, sizeof(p)) == 0);
	CHECK(prefs_write(&p, '\r', buf, 10) == -1);
	/* the longest values fit */
	memset(p.font, 'f', sizeof(p.font) - 1);
	memset(p.imagePath, 'i', sizeof(p.imagePath) - 1);
	memset(p.layout, 'l', sizeof(p.layout) - 1);
	memset(p.bridge, 'b', sizeof(p.bridge) - 1);
	memset(p.bridgeKey, 'k', sizeof(p.bridgeKey) - 1);
	memset(p.wordService, 'w', sizeof(p.wordService) - 1);
	memset(p.theme, 't', sizeof(p.theme) - 1);
	n = prefs_write(&p, '\r', buf, sizeof(buf));
	CHECK(n > 0);
	prefs_default(&q);
	prefs_read(&q, buf, n);
	CHECK(memcmp(&p, &q, sizeof(p)) == 0);
}

/* ---- recovery ---- */

static void TestRecovery(void)
{
	char buf[64], title[16];
	long n, body;

	n = recovery_header("my post.md", buf, sizeof(buf));
	CHECK(n == recovery_header("my post.md", NULL, 0));
	memcpy(buf + n, "---\rtext", 9);
	CHECK(recovery_parse(buf, n + 9, title, sizeof(title), &body));
	CHECK_STR(title, "my post.md");
	CHECK(body == n && memcmp(buf + body, "---", 3) == 0);
	CHECK(!recovery_parse("---\rtitle: x\r", 14, title, sizeof(title), &body));
	CHECK(!recovery_parse("Topica recovery: no end", 23, title, sizeof(title), &body));
	CHECK(recovery_header("x", buf, 5) == -1);
	n = recovery_header("a very long title that will be cut", buf, sizeof(buf));
	CHECK(recovery_parse(buf, n, title, sizeof(title), &body));
	CHECK(strlen(title) == sizeof(title) - 1);
}

/* ---- mdscan ---- */

/* One letter per character: m markup, c code, u address, f meta, l link
   text, h heading, B bold italic, b bold, i italic, . plain, / line end */
static void Classes(const char *text, char *out)
{
	unsigned char cls[512];
	MdState s;
	long n = (long) strlen(text), i;

	md_begin(&s);
	md_classify(&s, text, n, n, cls);
	for (i = 0; i < n; i++) {
		int c = cls[i];
		out[i] = text[i] == '\n' || text[i] == '\r' ? '/' : c & MD_MARK ? 'm' : c & MD_CODE ? 'c' : c & MD_URL ? 'u'
			: c & MD_META ? 'f' : c & MD_LINK ? 'l' : c & MD_HEADING ? 'h'
			: (c & (MD_BOLD | MD_ITALIC)) == (MD_BOLD | MD_ITALIC) ? 'B' : c & MD_BOLD ? 'b' : c & MD_ITALIC ? 'i' : '.';
	}
	out[n] = '\0';
}

static void Styled(const char *text, const char *want, int line)
{
	char got[512];

	Classes(text, got);
	CheckStr(got, want, line);
}
#define STYLED(text, want) Styled((text), (want), __LINE__)

static void TestMdscan(void)
{
	static const char kDoc[] =
		"---\ntitle: \"A *post*\"\n# a comment\n---\n\n# One\n\nSome **bold** and a [link](http://x.y/a_b).\n\n"
		"```c\n# not a heading\nint *p;\n```\n\n## Two ##\n\n> quoted *text*\n\n- item `code`\n### Three\n";
	char buf[512];
	unsigned char all[512], part[512];
	MdHeading h[8];
	MdState s;
	long n = (long) strlen(kDoc), at;
	int count;
	unsigned long b1, b2;

	STYLED("# Title", "mmhhhhh");
	STYLED("#No heading", "...........");
	/* TOML front matter (Hugo): "+++" opens and only "+++" closes it */
	STYLED("+++\n# x = 1\n---\n+++\n# H", "mmm/fffffff/fff/mmm/mmh");
	STYLED("+++\n# x", "mmm/fff");
	STYLED("## Two ##", "mmmhhhmmm");
	STYLED("Some *it* and **bold** and ***both***", "....."   "miim"  "....." "mmbbbbmm" "....." "mmmBBBBmmm");
	STYLED("a snake_case_word here", "......................");
	STYLED("2 * 3 * 4", ".........");
	STYLED("use `x*y*z` now", "....mcccccm....");
	STYLED("``a`b``", "mmcccmm");
	STYLED("[Topica](https://example.com/a_b_c) x", "mllllllm" "uuuuuuuuuuuuuuuuuuuuuuuuuuu" "..");
	STYLED("[![a](i.png)](u)", "mmmlmuuuuuuumuuu");
	STYLED("see [the docs][ref].", "....mllllllllmuuuuu.");
	STYLED("a note[^1].", "......mllm.");
	STYLED("[ref]: https://x.y \"T\"", "mmmmmm" "." "uuuuuuuuuuu" "....");
	STYLED("**bold *it***", "mmbbbbbmBBmmm");
	STYLED("[a **b**](u)", "mllmmlmmmuuu");
	STYLED("\\*not\\*", "mm...mm");
	STYLED("{% include x.html %} y", "ffffffffffffffffffff..");
	STYLED("{{ page.title }}", "ffffffffffffffff");
	STYLED("{: .note}", "fffffffff");
	STYLED("<br/> and <!-- c -->", "fffff.....ffffffffff");
	STYLED("<https://a.b> 3 < 4", "muuuuuuuuuuum......");
	STYLED("> quote *it*", "miiiiiiimiim");
	STYLED("- item *a*", "mm.....mim");
	STYLED("1. one", "mmm...");
	STYLED("- [x] done", "mmmmm.....");
	STYLED("***", "mmm");
	STYLED("---\ntitle: *x*\n---\n# H", "mmm/ffffffffff/mmm/mmh");
	STYLED("x\n---\ny", "./mmm/.");
	STYLED("```c\n# not\n```\n# yes", "mmmm/ccccc/mmm/mmhhh");
	STYLED("~~~\n```\n~~~\n*a*", "mmm/ccc/mmm/mim");
	STYLED("```\nnever closed\n# x", "mmm/cccccccccccc/ccc");

	/* the same classes scanned in one go or from the middle */
	md_begin(&s);
	md_classify(&s, kDoc, n, n, all);
	CHECK(s.pos == n);
	md_begin(&s);
	at = md_seek(&s, kDoc, n, 75);
	CHECK(at > 0 && at <= 75 && (kDoc[at - 1] == '\n'));
	md_classify(&s, kDoc, n, n, part);
	CHECK(memcmp(all + at, part, (size_t) (n - at)) == 0);
	md_begin(&s);
	at = md_seek(&s, kDoc, n, (long) (strstr(kDoc, "int *p") - kDoc) + 3);
	CHECK(at == (long) (strstr(kDoc, "int *p") - kDoc));
	CHECK(s.fence == '`');
	CHECK(md_line_after(kDoc, n, at) == at + 8);
	CHECK(md_line_after("ab", 2, 1) == 2);

	count = md_headings(kDoc, n, h, 8);
	CHECK(count == 3);
	CHECK(h[0].level == 1 && h[0].len == 3 && memcmp(kDoc + h[0].start, "One", 3) == 0);
	CHECK(h[1].level == 2 && h[1].len == 3 && memcmp(kDoc + h[1].start, "Two", 3) == 0);
	CHECK(h[2].level == 3 && memcmp(kDoc + h[2].line, "### Three", 9) == 0);
	CHECK(md_headings(kDoc, n, h, 1) == 3);

	/* the blocks' number changes with a fence, not with prose */
	b1 = md_blocks(kDoc, n);
	strcpy(buf, kDoc);
	buf[strstr(buf, "Some") - buf] = 'X';
	CHECK(md_blocks(buf, n) == b1);
	strcpy(buf, kDoc);
	memcpy(strstr(buf, "```\n\n##"), "``x", 3);
	b2 = md_blocks(buf, n);
	CHECK(b2 != b1);
	strcpy(buf, kDoc);
	buf[0] = '-';
	buf[1] = '+';
	CHECK(md_blocks(buf, n) != b1);

	/* any text at all: no reads or writes out of bounds (ASan watches) */
	{
		static const char kChars[] = "*_`[]()!#>-~{}%<:/\\^ \nab1.";
		int round, len, i;
		srand(1);
		for (round = 0; round < 3000; round++) {
			len = rand() % 120;
			for (i = 0; i < len; i++)
				buf[i] = kChars[rand() % (int) (sizeof(kChars) - 1)];
			buf[len] = '\0';
			md_begin(&s);
			md_classify(&s, buf, len, len, all);
			(void) md_headings(buf, len, h, 8);
			(void) md_blocks(buf, len);
		}
		CHECK(round == 3000);
	}
}

/* ---- mdpage ---- */

/* The blocks as words: > per quote, then T title, Y byline, D lead,
   P paragraph, H heading, I item, C code, B table, X HTML, R rule, M image;
   a digit for a heading's level or a nesting depth, .n an item's number,
   [ ] or [x] a task */
static void Blocks(const char *text, const char *want, int line)
{
	MpScan p;
	MpBlock b;
	char got[512], *o = got;
	int i;

	mp_begin(&p, text, (long) strlen(text));
	while (mp_next(&p, &b)) {
		if (o > got)
			*o++ = ' ';
		for (i = 0; i < b.quote; i++)
			*o++ = '>';
		*o++ = "?TYDPHICBXRM"[b.kind];
		if (b.kind == MP_HEADING)
			*o++ = (char) ('0' + b.level);
		else if (b.depth)
			*o++ = (char) ('0' + b.depth);
		if (b.number)
			o += sprintf(o, ".%ld", b.number);
		if (b.task)
			o += sprintf(o, "[%c]", b.task);
	}
	*o = '\0';
	CheckStr(got, want, line);
}
#define BLOCKS(text, want) Blocks((text), (want), __LINE__)

static void LinkSeen(void *ctx, long at)
{
	char *seen = (char *) ctx;
	sprintf(seen + strlen(seen), "%ld,", at);
}

/* Block k's display text (| for a break) and its styles: . plain, b bold,
   i italic, B both, c code, l link, f meta, n note */
static void Shown(const char *text, int k, const char *want, const char *wantSty, int line)
{
	MpScan p;
	MpBlock b;
	char out[512];
	unsigned char sty[512], cls[512];
	long n = (long) strlen(text), len, i;
	char st[512];

	mp_begin(&p, text, n);
	do
		(void) mp_next(&p, &b);
	while (k-- > 0);
	len = mp_text(text, n, &b, out, sty, sizeof(out), cls);
	CHECK(mp_text(text, n, &b, NULL, NULL, 0, cls) == len);
	for (i = 0; i < len; i++) {
		int c = sty[i];
		if (out[i] == MP_BREAK)
			out[i] = '|';
		st[i] = c & MS_NOTE ? 'n' : c & MS_META ? 'f' : c & MS_CODE ? 'c' : c & MS_LINK ? 'l'
			: (c & (MS_BOLD | MS_ITALIC)) == (MS_BOLD | MS_ITALIC) ? 'B' : c & MS_BOLD ? 'b' : c & MS_ITALIC ? 'i' : '.';
	}
	out[len] = st[len] = '\0';
	CheckStr(out, want, line);
	if (wantSty != NULL)
		CheckStr(st, wantSty, line);
}
#define SHOWN(text, k, want, sty) Shown((text), (k), (want), (sty), __LINE__)

/* Where a typed quote stays straight */
static void TestStraightQuotes(void)
{
	static const char t[] =
		"---\ntitle: x\n---\n"   /* 0-17: front matter */
		"Say `a b` now\n"          /* 18-31: a span at 22-26 */
		"```\ncode\n```\n"        /* 32-45 */
		"plain";                     /* 46- */
	unsigned char buf[64];
	long n = (long) strlen(t);

	CHECK(md_straight_at(t, n, 5, buf, sizeof(buf)));          /* front matter */
	CHECK(md_straight_at(t, n, 0, buf, sizeof(buf)));          /* its first --- */
	CHECK(!md_straight_at(t, n, 19, buf, sizeof(buf)));        /* prose */
	CHECK(md_straight_at(t, n, 24, buf, sizeof(buf)));         /* inside `a b` */
	CHECK(md_straight_at(t, n, 23, buf, sizeof(buf)));         /* just after the backtick */
	CHECK(!md_straight_at(t, n, 27, buf, sizeof(buf)));        /* just after the span */
	CHECK(md_straight_at(t, n, 38, buf, sizeof(buf)));         /* in the fence */
	CHECK(!md_straight_at(t, n, 48, buf, sizeof(buf)));        /* after it */
	CHECK(!md_straight_at(t, n, n, buf, sizeof(buf)));         /* the end */
	CHECK(!md_straight_at(t, n, 24, buf, 4));                  /* a line too long to look at */
}

static void TestMdpage(void)
{
	static const char kPost[] =
		"---\ntitle: \"Hello\"\ndate: 2026-10-22 10:00:00 -0700\ncategory: retro\n"
		"description: A short one.\n---\n\nSome [text][r] and <https://a.b>.\n\n[r]: https://example.com/r \"T\"\n";
	char url[64], buf[64], seen[64];
	MpScan p;
	MpBlock b;
	unsigned char cls[256];
	long n = (long) strlen(kPost);

	BLOCKS("", "");
	BLOCKS("One line\nand more\n\nTwo", "P P");
	BLOCKS("# A\ntext\n## B ##\n---\n***", "H1 P H2 R R");
	BLOCKS("- a\n- b\n  - c\n    - d\n- e", "I1 I1 I2 I3 I1");
	BLOCKS("1. one\n2. two\n\n   more of two\n\nafter", "I1.1 I1.2 P1 P");
	BLOCKS("- [ ] todo\n- [x] done", "I1[ ] I1[x]");
	BLOCKS("- a\nlazy\n- b", "I1 I1");
	BLOCKS("> quote\n> more\n>\n> > deeper\n\nout", ">P >>P P");
	BLOCKS("> - in a quote\n> - two", ">I1 >I1");
	BLOCKS("```c\nint x;\n\n```\ntext", "C P");
	BLOCKS("- item\n  ```\n  code\n  ```", "I1 C1");
	BLOCKS("```\nnever closed", "C");
	BLOCKS("| a | b |\n|---|---|\n| 1 | 2 |\n\nx", "B P");
	BLOCKS("<div>\n<p>x</p>\n</div>\n\n<em>inline</em> text", "X P");
	BLOCKS("{% include image.html %}\nnext\n\npara", "X P");
	BLOCKS("{% raw %} and text\n\n{%x%}", "P X");
	BLOCKS("![A cat](/a/cat.png)\n\n![A](b.png) and text", "M P");
	BLOCKS("Text\n[r]: http://x\n\nmore", "P P");
	BLOCKS("para\n# heading\n- item\ntext", "P H1 I1");
	BLOCKS(kPost, "T Y D P");
	BLOCKS("---\ntitle:\n---\nbody", "P");

	SHOWN(kPost, 0, "Hello", NULL);
	SHOWN(kPost, 1, "Oct 22, 2026 \xE1 retro", NULL);
	SHOWN(kPost, 2, "A short one.", NULL);
	SHOWN(kPost, 3, "Some text and https://a.b.", ".....llll.....lllllllllll.");
	SHOWN("Some **bold** and *it*\nnext  \nline\\\nend", 0, "Some bold and it next|line|end",
			".....bbbb.....ii..............");
	SHOWN("# A `code` title #", 0, "A code title", "..cccc......");
	SHOWN("- [x] *done* now", 0, "done now", "iiii....");
	SHOWN("3. three\n   carried on", 0, "three carried on", NULL);
	SHOWN("> quoted *it*\n> more", 0, "quoted it more", ".......ii.....");
	SHOWN("```\n\tx = 1;\n  y\n```", 0, "    x = 1;|  y", "cccccccccc.ccc");
	SHOWN("Escaped \\*star\\* and \\\\", 0, "Escaped *star* and \\", NULL);
	SHOWN("A &amp; B &mdash; &#x1F600; &#233; &bogus;", 0, "A & B \xD1 \xD7 \x8E &bogus;", NULL);
	SHOWN("Liquid {{ site.url }} here {: .x}", 0, "Liquid {{ site.url }} here", ".......ffffffffffffff.....");
	SHOWN("Tags <em>gone</em><br>next", 0, "Tags gone|next", NULL);
	SHOWN("A note[^1] here.", 0, "A note1 here.", "......n......");
	SHOWN("lots   of\t spaces", 0, "lots of spaces", NULL);
	SHOWN("![A cat](/img/cat.png)", 0, "A cat", "lllll");
	SHOWN("| a |\n|---|", 0, "| a |||---|", NULL);
	SHOWN("> | a |\n> | b |", 0, "| a ||| b |", NULL);

	/* links: where each one starts, its address and its text */
	{
		static const char kLinks[] = "A [one](http://1.x \"t\") [![img](i.png)](http://2.x) <http://3.x> [r][] [^n]\n\n[R]: <http://r.x>\n";
		long k = (long) strlen(kLinks);
		seen[0] = '\0';
		mp_begin(&p, kLinks, k);
		(void) mp_next(&p, &b);
		(void) mp_text_links(kLinks, k, &b, buf, NULL, sizeof(buf), cls, LinkSeen, seen);
		CHECK_STR(seen, "2,24,25,52,65,");
		CHECK(mp_link_url(kLinks, k, 2, url, sizeof(url)) == 10);
		CHECK_STR(url, "http://1.x");
		CHECK(mp_link_text(kLinks, k, 2, buf, sizeof(buf)) == 3);
		CHECK_STR(buf, "one");
		(void) mp_link_url(kLinks, k, 24, url, sizeof(url));
		CHECK_STR(url, "http://2.x");
		(void) mp_link_text(kLinks, k, 24, buf, sizeof(buf));
		CHECK_STR(buf, "![img](i.png)");
		(void) mp_link_url(kLinks, k, 25, url, sizeof(url));
		CHECK_STR(url, "i.png");
		(void) mp_link_text(kLinks, k, 25, buf, sizeof(buf));
		CHECK_STR(buf, "img");
		(void) mp_link_url(kLinks, k, 52, url, sizeof(url));
		CHECK_STR(url, "http://3.x");
		(void) mp_link_url(kLinks, k, 65, url, sizeof(url));
		CHECK_STR(url, "http://r.x");
		CHECK(mp_link_url(kLinks, k, 71, url, sizeof(url)) == 0);
		CHECK(mp_link_url(kLinks, k, 2, url, 5) == 10);
		CHECK_STR(url, "http");
	}
	(void) mp_link_url(kPost, n, (long) (strstr(kPost, "[text]") - kPost), url, sizeof(url));
	CHECK_STR(url, "https://example.com/r");

	/* fonts */
	b.kind = MP_HEADING;
	b.level = 2;
	CHECK(mp_font(&b, MS_BOLD) == MF_H2);
	CHECK(mp_font(&b, MS_CODE) == MF_CODE);
	b.kind = MP_PARA;
	CHECK(mp_font(&b, MS_BOLD | MS_ITALIC | MS_LINK) == MF_BOLD_ITALIC);
	CHECK(mp_font(&b, MS_NOTE) == MF_SMALL);
	b.kind = MP_LEAD;
	CHECK(mp_font(&b, 0) == MF_ITALIC);

	/* any text at all */
	{
		static const char kChars[] = "*_`[]()!#>-~{}%<:/\\^&;| \n\tab1.x";
		char text[160], out[200];
		unsigned char sty[200], cl[160];
		int round, len, i;
		srand(2);
		for (round = 0; round < 3000; round++) {
			len = rand() % 150;
			for (i = 0; i < len; i++)
				text[i] = kChars[rand() % (int) (sizeof(kChars) - 1)];
			text[len] = '\0';
			mp_begin(&p, text, len);
			while (mp_next(&p, &b)) {
				CHECK(b.start <= b.end && b.end <= len);
				if (mp_text(text, len, &b, out, sty, sizeof(out), cl) > (long) sizeof(out))
					gFailed++;
				(void) mp_link_url(text, len, b.at, url, sizeof(url));
				(void) mp_link_text(text, len, b.at, buf, sizeof(buf));
			}
		}
		CHECK(round == 3000);
	}
}

/* ---- layout ---- */

/* Lines of display text in a font where every character is 1 wide, bold
   2, code 1 (taller); | between lines */
static void Broken(const MpBlock *b, const char *text, const char *styles, int w, const char *want, int line)
{
	static LayFonts f;
	unsigned char sty[256];
	char got[512], *o = got;
	long n = (long) strlen(text), at = 0, i;
	LayLine l;
	int font, guard = 0;

	for (font = 0; font < MF_COUNT; font++) {
		memset(f.width[font], font == MF_BOLD ? 2 : 1, 256);
		f.ascent[font] = (short) (font == MF_CODE ? 9 : 7);
		f.descent[font] = 2;
		f.leading[font] = 1;
	}
	for (i = 0; i < n; i++)
		sty[i] = (unsigned char) (styles != NULL && styles[i] == 'b' ? MS_BOLD : styles != NULL && styles[i] == 'c' ? MS_CODE : 0);
	do {
		at = lay_line(&f, b, text, sty, n, at, w, &l);
		if (o > got)
			*o++ = '|';
		memcpy(o, text + l.start, (size_t) l.len);
		o += l.len;
		CHECK(l.width == lay_width(&f, b, text, sty, l.start, l.len));
		CHECK(l.width <= w || l.len == 1);
		CHECK(l.height == (b->kind == MP_CODE || memchr(sty + l.start, MS_CODE, (size_t) l.len) ? 12 : 10));
	} while (at < n && ++guard < 100);
	*o = '\0';
	CheckStr(got, want, line);
}

static void TestLayout(void)
{
	MpBlock para, code;

	memset(&para, 0, sizeof(para));
	memset(&code, 0, sizeof(code));
	para.kind = MP_PARA;
	code.kind = MP_CODE;
	Broken(&para, "one two three four", NULL, 9, "one two|three|four", __LINE__);
	Broken(&para, "one two three four", NULL, 7, "one two|three|four", __LINE__);
	Broken(&para, "one two three four", NULL, 6, "one|two|three|four", __LINE__);
	Broken(&para, "abcdefghij", NULL, 4, "abcd|efgh|ij", __LINE__);
	Broken(&para, "well-known words", NULL, 8, "well-|known|words", __LINE__);
	Broken(&para, "a/b/c/d", NULL, 4, "a/b/|c/d", __LINE__);
	Broken(&para, "aa bb", "bbbbb", 5, "aa|bb", __LINE__);
	Broken(&para, "one\rtwo", NULL, 20, "one|two", __LINE__);
	Broken(&para, "one  \r\rtwo", NULL, 20, "one  ||two", __LINE__);
	Broken(&para, "aa   bb", NULL, 3, "aa|bb", __LINE__);
	Broken(&para, "x code y", "..cccc..", 4, "x|code|y", __LINE__);
	Broken(&code, "  if (x)\r    y;", NULL, 40, "  if (x)|    y;", __LINE__);
	Broken(&code, "  a b c d", NULL, 5, "  a b| c d", __LINE__);
	Broken(&para, "", NULL, 5, "", __LINE__);
	Broken(&para, "aacc", "..cc", 2, "aa|cc", __LINE__);
	Broken(&para, "aa cccc", "...cccc", 4, "aa|cccc", __LINE__);
}

/* ---- mdhtml ---- */

/* The HTML between <div class="post-content"> and </div> */
static void Html(const char *text, const char *want, int line)
{
	static char out[8192];
	unsigned char cls[1024];
	long n = (long) strlen(text), len;
	char *a, *e;

	len = mh_page(text, n, cls, NULL, 0);
	CHECK(mh_page(text, n, cls, out, sizeof(out) - 1) == len);
	CHECK(mh_page(text, n, cls, out, len - 1) == -1);
	out[len] = '\0';
	a = strstr(out, "<div class=\"post-content\">\n");
	e = strstr(out, "</div>\n</article>");
	if (a == NULL || e == NULL) {
		CheckStr(out, want, line);
		return;
	}
	a += strlen("<div class=\"post-content\">\n");
	*e = '\0';
	CheckStr(a, want, line);
}
#define HTML(text, want) Html((text), (want), __LINE__)

static void TestMdhtml(void)
{
	static char out[4096];
	unsigned char cls[512];
	static const char kPost[] = "---\ntitle: A <post> & more\ndate: 2026-01-02\n---\nHi\n";
	long len;

	HTML("Some **bold** and *it*.\nNext line  \nbreak", "<p>Some <strong>bold</strong> and <em>it</em>.\nNext line<br>\nbreak</p>\n");
	HTML("# One\n## Two `x` ##", "<h1>One</h1>\n<h2>Two <code>x</code></h2>\n");
	HTML("A [link](http://x.y?a=1&b=\"2\") and <https://a.b>.",
		"<p>A <a href=\"http://x.y?a=1&amp;b=&quot;2&quot;\">link</a> and <a href=\"https://a.b\">https://a.b</a>.</p>\n");
	HTML("**[bold link](u)** x", "<p><a href=\"u\"><strong>bold link</strong></a> x</p>\n");
	HTML("![A cat](/i/cat.png)", "<p><img src=\"/i/cat.png\" alt=\"A cat\"></p>\n");
	HTML("[![img](i.png)](http://x)", "<p><a href=\"http://x\"><img src=\"i.png\" alt=\"img\"></a></p>\n");
	HTML("See [it][r].\n\n[r]: http://r.x", "<p>See <a href=\"http://r.x\">it</a>.</p>\n");
	HTML("A < B & C &amp; D &#233;", "<p>A &lt; B &amp; C &amp; D &#233;</p>\n");
	HTML("`a < b && c`", "<p><code>a &lt; b &amp;&amp; c</code></p>\n");
	HTML("x <em>raw</em> {{ site.url }} {: .c}", "<p>x <em>raw</em> {{ site.url }} </p>\n");
	HTML("A note[^1].", "<p>A note<sup>1</sup>.</p>\n");
	HTML("\\*not\\*", "<p>*not*</p>\n");
	HTML("- a\n- b\n  - c\n- d", "<ul>\n<li>a</li>\n<li>b<ul>\n<li>c</li>\n</ul>\n</li>\n<li>d</li>\n</ul>\n");
	HTML("1. a\n2. b\n\n   more\n\nafter", "<ol>\n<li>a</li>\n<li>b<p>more</p>\n</li>\n</ol>\n<p>after</p>\n");
	HTML("3. c\n- d", "<ol start=\"3\">\n<li>c</li>\n</ol>\n<ul>\n<li>d</li>\n</ul>\n");
	HTML("- [x] done\n- [ ] not", "<ul>\n<li><input type=\"checkbox\" disabled checked> done</li>\n"
		"<li><input type=\"checkbox\" disabled> not</li>\n</ul>\n");
	HTML("> quote\n>\n> > deep\n\nout", "<blockquote>\n<p>quote</p>\n<blockquote>\n<p>deep</p>\n</blockquote>\n</blockquote>\n<p>out</p>\n");
	HTML("> - a\n> - b", "<blockquote>\n<ul>\n<li>a</li>\n<li>b</li>\n</ul>\n</blockquote>\n");
	HTML("```c\nif (a < b)\n\treturn;\n```", "<pre><code class=\"language-c\">if (a &lt; b)\n\treturn;\n</code></pre>\n");
	HTML("~~~\nplain\n~~~", "<pre><code>plain\n</code></pre>\n");
	HTML("x\n\n---\n\n***", "<p>x</p>\n<hr>\n<hr>\n");
	HTML("| a | *b* |\n|---|:-:|\n| `x|y` | 2 |", "<table>\n<tr><th>a</th><th><em>b</em></th></tr>\n"
		"<tr><td><code>x|y</code></td><td>2</td></tr>\n</table>\n");
	HTML("| no | delimiter |", "<p>| no | delimiter |</p>\n");
	HTML("<div class=\"x\">\n<b>hi</b>\n</div>", "<div class=\"x\">\n<b>hi</b>\n</div>\n");
	HTML("    - deep\n- shallow", "<ul>\n<li>deep</li>\n<li>shallow</li>\n</ul>\n");

	len = mh_page(kPost, (long) strlen(kPost), cls, out, sizeof(out) - 1);
	CHECK(len > 0);
	out[len > 0 ? len : 0] = '\0';
	CHECK(strstr(out, "<title>A &lt;post&gt; &amp; more</title>") != NULL);
	CHECK(strstr(out, "<header class=\"post-header\">\n<h1 class=\"post-title\">A &lt;post&gt; &amp; more</h1>\n"
		"<p class=\"post-meta\">Jan 2, 2026</p>\n</header>\n<div class=\"post-content\">\n<p>Hi</p>\n</div>\n") != NULL);
	CHECK(strstr(out, "<meta charset=\"utf-8\">") != NULL);

	/* any text at all */
	{
		static const char kChars[] = "*_`[]()!#>-~{}%<:/\\^&;| \n\tab1.x";
		char text[200];
		int round, n, i;
		srand(3);
		for (round = 0; round < 2000; round++) {
			n = rand() % 180;
			for (i = 0; i < n; i++)
				text[i] = kChars[rand() % (int) (sizeof(kChars) - 1)];
			text[n] = '\0';
			len = mh_page(text, n, cls, NULL, 0);
			if (mh_page(text, n, cls, out, sizeof(out)) != len)
				gFailed++;
		}
		CHECK(round == 2000);
	}
}

/* ---- vi ---- */

/* A plain buffer as vi's host, with a real undo stack */
static char gVt[1024];
static long gVn, gVs, gVe;
static int gVbeeps;
static UndoStack gVu;
static char gVub[4096];

static const char *VhText(void *c, long *n) { (void) c; *n = gVn; return gVt; }
static void VhGet(void *c, long *s, long *e) { (void) c; *s = gVs; *e = gVe; }
static void VhSet(void *c, long s, long e) { (void) c; gVs = s; gVe = e; }
static void VhBeep(void *c) { (void) c; gVbeeps++; }
static void VhGroup(void *c, int begin) { (void) c; undo_group(&gVu, begin); }

static void VhApply(long s, long e, const char *ins, long len)
{
	memmove(gVt + s + len, gVt + e, (size_t) (gVn - e));
	memcpy(gVt + s, ins, (size_t) len);
	gVn += len - (e - s);
}

static int VhReplace(void *c, int kind, long s, long e, const char *ins, long len)
{
	(void) c;
	if (s < 0 || e < s || e > gVn) {
		gFailed++;
		return 0;
	}
	if (gVn - (e - s) + len > (long) sizeof(gVt) - 1)
		return 0;                   /* too long, as the editor can say */
	undo_record(&gVu, kind == VI_TYPE ? kUndoTyping : kind == VI_DELETE ? kUndoDelete : kUndoReplace,
		s, gVt + s, e - s, ins, len);
	VhApply(s, e, ins, len);
	gVs = gVe = s + len;
	return 1;
}

static long VhLineStart(long p)
{
	while (p > 0 && gVt[p - 1] != '\n' && gVt[p - 1] != '\r')
		p--;
	return p;
}

static long VhLineEnd(long p)
{
	while (p < gVn && gVt[p] != '\n' && gVt[p] != '\r')
		p++;
	return p;
}

static long VhVertical(void *c, long pos, int lines, long *goal)
{
	long ls = VhLineStart(pos);

	(void) c;
	if (*goal < 0)
		*goal = pos - ls;
	for (; lines > 0 && VhLineEnd(ls) < gVn; lines--)
		ls = VhLineEnd(ls) + 1;
	for (; lines < 0 && ls > 0; lines++)
		ls = VhLineStart(ls - 1);
	return ls + *goal < VhLineEnd(ls) ? ls + *goal : VhLineEnd(ls) > ls ? VhLineEnd(ls) - 1 : ls;
}

static void VhUndo(void *c, int redo)
{
	UndoApply a;

	(void) c;
	do {
		if (!undo_step(&gVu, redo, &a))
			return;
		VhApply(a.pos, a.pos + a.removeLen, a.insert, a.insertLen);
		gVs = gVe = a.selStart;
	} while (a.more);
}

static ViReg gVreg;

/* Registers a to z for the vi tests, emptied by VhInit */
static ViReg gVnamed[26];
static char gVnamedBuf[26][512];

static ViReg *VhNamed(void *ctx, int name)
{
	ViReg *r = &gVnamed[name - 'a'];

	(void) ctx;
	r->buf = gVnamedBuf[name - 'a'];
	r->cap = sizeof(gVnamedBuf[0]);
	return r;
}

static void VhInit(ViHost *h, Vi *v, char *reg, long cap, const char *text)
{
	const char *bar = strchr(text, '|');

	memset(h, 0, sizeof(*h));
	h->text = VhText;
	h->get_sel = VhGet;
	h->set_sel = VhSet;
	h->replace = VhReplace;
	h->vertical = VhVertical;
	h->undo = VhUndo;
	h->group = VhGroup;
	h->beep = VhBeep;
	h->named = VhNamed;
	memset(gVnamed, 0, sizeof(gVnamed));
	gVn = (long) strlen(text) - 1;
	memcpy(gVt, text, (size_t) (bar - text));
	memcpy(gVt + (bar - text), bar + 1, strlen(bar + 1));
	gVs = gVe = bar - text;
	gVbeeps = 0;
	undo_init(&gVu, gVub, sizeof(gVub));
	gVreg.buf = reg;
	gVreg.cap = cap;
	gVreg.len = 0;
	gVreg.lines = 0;
	vi_init(v, &gVreg);
	v->on = 1;
}

/* The text with | at the cursor */
static void VhShow(char *out)
{
	memcpy(out, gVt, (size_t) gVs);
	out[gVs] = '|';
	memcpy(out + gVs + 1, gVt + gVs, (size_t) (gVn - gVs));
	out[gVn + 1] = '\0';
}

static void ViCase(const char *text, const char *keys, const char *want, int cr, int line)
{
	char in[512], got[1024], exp[512], reg[512];
	const char *k;
	Vi v;
	ViHost h;
	unsigned long t = 0;
	size_t i;

	strcpy(in, text);
	strcpy(exp, want);
	if (cr) {
		/* the editor's CR line ends */
		for (i = 0; in[i]; i++)
			if (in[i] == '\n')
				in[i] = '\r';
		for (i = 0; exp[i]; i++)
			if (exp[i] == '\n')
				exp[i] = '\r';
	}
	VhInit(&h, &v, reg, sizeof(reg), in);
	for (k = keys; *k; k++)
		(void) vi_key(&v, &h, (unsigned char) *k, t += 100);
	VhShow(got);
	if (!cr)
		for (i = 0; got[i]; i++)
			if (got[i] == '\r')
				got[i] = '\n';     /* vi makes CR line ends, the editor's */
	CheckStr(got, exp, line);
	CHECK(v.mode == VI_NORMAL);
}

/* The command line and ZZ hand their commands to the host */
static char gVcmd[40];
static int gVcmdOk;

static int VhCommand(void *ctx, const char *cmd)
{
	(void) ctx;
	strcpy(gVcmd, cmd);
	return gVcmdOk;
}

static void ViKeys(Vi *v, ViHost *h, const char *keys)
{
	while (*keys)
		(void) vi_key(v, h, (unsigned char) *keys++, 0);
}

static void TestViCommands(void)
{
	ViHost h;
	Vi v;
	char reg[64];
	static const char *const kWant[][2] = {
		{ ":w\r", "w" }, { ":wq\r", "wq" }, { ":q!\r", "q!" }, { ":x\r", "x" }, { ": w\r", "w" },
		{ "ZZ", "x" }, { "ZQ", "q!" }
	};
	size_t i;

	for (i = 0; i < sizeof(kWant) / sizeof(kWant[0]); i++) {
		VhInit(&h, &v, reg, sizeof(reg), "|ab");
		h.command = VhCommand;
		gVcmdOk = 1;
		gVcmd[0] = '\0';
		ViKeys(&v, &h, kWant[i][0]);
		CHECK_STR(gVcmd, kWant[i][1]);
		CHECK(gVbeeps == 0 && v.lineLen == 0);
	}
	/* the line shows in the status as it's typed */
	VhInit(&h, &v, reg, sizeof(reg), "|ab");
	ViKeys(&v, &h, ":wq");
	CHECK_STR(vi_status(&v), ":wq");
	ViKeys(&v, &h, "\033");
	CHECK_STR(vi_status(&v), "");
	/* a host that can't, or none: a beep */
	h.command = VhCommand;
	gVcmdOk = 0;
	ViKeys(&v, &h, ":w\r");
	CHECK(gVbeeps == 1);
	h.command = NULL;
	ViKeys(&v, &h, "ZZ");
	CHECK(gVbeeps == 2);
}

static void TestVi(void)
{
	static const char *const kVi[][3] = {
		{ "hello |world", "x", "hello |orld" },
		{ "|hello world", "$", "hello worl|d" },
		{ "hello wor|ld", "0", "|hello world" },
		{ "   ind|ent", "^", "   |indent" },
		{ "|one two three", "w", "one |two three" },
		{ "|one two three", "2w", "one two |three" },
		{ "one two |three", "b", "one |two three" },
		{ "|one two", "e", "on|e two" },
		{ "|one.two three", "w", "one|.two three" },
		{ "|one.two three", "W", "one.two |three" },
		{ "|a\n\nb", "w", "a\n|\nb" },
		{ "a\n\n|b", "b", "a\n|\nb" },
		{ "|one\ntwo", "w", "one\n|two" },
		{ "one|\ntwo", "w", "one\n|two" },
		{ "|ab cd", "ee", "ab c|d" },
		{ "x |y", "b", "|x y" },
		{ "|l1\nl2\nl3", "G", "l1\nl2\n|l3" },
		{ "l1\nl2\n|l3", "gg", "|l1\nl2\nl3" },
		{ "|l1\nl2\nl3", "2G", "l1\n|l2\nl3" },
		{ "|a\nb\n\nc\nd\n\ne", "}", "a\nb\n|\nc\nd\n\ne" },
		{ "|a\nb\n\nc\nd\n\ne", "2}", "a\nb\n\nc\nd\n|\ne" },
		{ "a\nb\n\nc\n|d", "{", "a\nb\n|\nc\nd" },
		{ "|hello world", "fo", "hell|o world" },
		{ "|hello world", "fo;", "hello w|orld" },
		{ "|hello world", "to", "hel|lo world" },
		{ "hello |world", "Fe", "h|ello world" },
		{ "hello wor|ld", "To", "hello wo|rld" },
		{ "|a.b.c", "f.;,", "a|.b.c" },
		{ "|abc\nde\nfghi", "jj", "abc\nde\n|fghi" },
		{ "ab|c\nd\nefg", "jj", "abc\nd\nef|g" },
		{ "|ab\ncd", "j$k", "a|b\ncd" },
		{ "|one", "l", "o|ne" },
		{ "on|e", "l", "on|e" },
		{ "|one two three", "dw", "|two three" },
		{ "one |two three", "dw", "one |three" },
		{ "one two |three", "dw", "one two| " },
		{ "|one two three", "d2w", "|three" },
		{ "|one two three", "2dw", "|three" },
		{ "|one two\nthree", "dw", "|two\nthree" },
		{ "one tw|o\nthree", "dw", "one t|w\nthree" },
		{ "|hello world", "de", "| world" },
		{ "hello |world", "db", "|world" },
		{ "hello |world", "d0", "|world" },
		{ "hello |world", "d$", "hello| " },
		{ "hello |world", "D", "hello| " },
		{ "|hello world", "dfo", "| world" },
		{ "|hello world", "dto", "|o world" },
		{ "|l1\nl2\nl3", "dj", "|l3" },
		{ "l1\nl2\n|l3", "dk", "|l1" },
		{ "l1\n|l2\nl3", "dG", "|l1" },
		{ "l1\n|l2\nl3", "dgg", "|l3" },
		{ "|a\nb\n\nc", "d}", "|\nc" },
		{ "|hello", "x", "|ello" },
		{ "hell|o", "x", "hel|l" },
		{ "|hello", "3x", "|lo" },
		{ "hel|lo", "X", "he|lo" },
		{ "|hello", "~", "H|ello" },
		{ "|hello", "3~", "HEL|lo" },
		{ "|hello", "rX", "|Xello" },
		{ "|hello", "3rX", "XX|Xlo" },
		{ "|l1\nl2\nl3", "dd", "|l2\nl3" },
		{ "l1\n|l2\nl3", "dd", "l1\n|l3" },
		{ "l1\nl2\n|l3", "dd", "l1\n|l2" },
		{ "|l1\nl2\nl3", "2dd", "|l3" },
		{ "|only", "dd", "|" },
		{ "|l1\nl2", "yyp", "l1\n|l1\nl2" },
		{ "|l1\nl2", "yyP", "|l1\nl1\nl2" },
		{ "l1\n|l2", "yyp", "l1\nl2\n|l2" },
		{ "|l1\nl2\nl3", "ddp", "l2\n|l1\nl3" },
		{ "|l1\nl2", "Yp", "l1\n|l1\nl2" },
		{ "|  l1\nl2", "J", "  l1| l2" },
		{ "|a\n  b\nc", "3J", "a b| c" },
		{ "|a.\nb", "J", "a.| b" },
		{ "|a\n)b", "J", "a|)b" },
		{ "|one two", "cwONE\033", "ON|E two" },
		{ "one |two", "cwX\033", "one |X" },
		{ "o|ne two", "cwX\033", "o|X two" },
		{ "|one two", "c2wX\033", "|X" },
		{ "|one two", "ceX\033", "|X two" },
		{ "one |two", "C!\033", "one |!" },
		{ "|l1\nl2", "ccX\033", "|X\nl2" },
		{ "|l1\nl2", "SX\033", "|X\nl2" },
		{ "|hello", "sJ\033", "|Jello" },
		{ "|hello", "3sJ\033", "|Jlo" },
		{ "|ab", "iX\033", "|Xab" },
		{ "|ab", "aX\033", "a|Xb" },
		{ "a|b", "IX\033", "|Xab" },
		{ "|ab", "AX\033", "ab|X" },
		{ "|ab\ncd", "oX\033", "ab\n|X\ncd" },
		{ "ab\n|cd", "OX\033", "ab\n|X\ncd" },
		{ "|ab", "3iX\033", "XX|Xab" },
		{ "|ab", "2aXY\033", "aXYX|Yb" },
		{ "|ab", "iXY\bZ\033", "X|Zab" },
		{ "|ab", "i1\r2\033", "1\n|2ab" },
		{ "|a", "3oX\033", "a\nX\nX\n|X" },
		{ "one t|wo three", "diw", "one | three" },
		{ "one t|wo three", "daw", "one |three" },
		{ "one two thre|e", "daw", "one tw|o" },
		{ "one t|wo three", "ciwX\033", "one |X three" },
		{ "say \"he|llo\" now", "di\"", "say \"|\" now" },
		{ "say \"he|llo\" now", "da\"", "say |now" },
		{ "say \"he|llo\" now", "ci\"X\033", "say \"|X\" now" },
		{ "f(a, |b) g", "di(", "f(|) g" },
		{ "f(a, |b) g", "da(", "f| g" },
		{ "f(a, (|b)) g", "di(", "f(a, (|)) g" },
		{ "[x |y]", "di[", "[|]" },
		{ "{a\n|b\n}", "di{", "|{\n}" },
		{ "|a\nb\n\nc", "dip", "|\nc" },
		{ "|a\nb\n\nc", "dap", "|c" },
		{ "a\n|b\n\nc\nd", "yipP", "|a\nb\na\nb\n\nc\nd" },
		{ "|one two", "ywP", "one| one two" },
		{ "|one two", "yw$p", "one twoone| " },
		{ "|one two", "dwu", "|one two" },
		{ "|one two", "dwu\022", "|two" },
		{ "|one two", "cwX\033u", "|one two" },
		{ "|one two three", "cwX\033wu", "|one two three" },
		{ "|a\nb\nc", "dd.", "|c" },
		{ "|abcdef", "x..", "|def" },
		{ "|abcdef", "x3.", "|ef" },
		{ "|one two three", "cwX\033w.", "X |X three" },
		{ "|a b c", "iX\033w.w.", "Xa Xb |Xc" },
		{ "|ab\ncd", "Ax\033j.", "abx\ncd|x" },
		{ "|l1\nl2\nl3\nl4", "dj.", "|" },
		{ "|a\nb", "yyjp.", "a\nb\na\n|a" },
		{ "|one two", "dwP", "one| two" },
		{ "|abc", "xp", "b|ac" },
		{ "|one two three", "dwwP", "two one| three" },
		{ "|aXbXc", "fXr-;.", "a-b|-c" },
		{ "|l1\nl2", "ddu", "|l1\nl2" },
		{ "|hello world", "vlld", "|lo world" },
		{ "|hello world", "vey$p", "hello worldhell|o" },
		{ "|l1\nl2\nl3", "Vjd", "|l3" },
		{ "|hello world", "vec!\033", "|! world" },
		{ "|hello", "v$~", "|HELLO" },
		{ "|a\nb\nc", "VjJ", "a| b\nc" },
		{ "hel|lo world", "vbd", "|o world" },
		{ "|abc", "lvhd", "|c" },
		{ "|l1\nl2", "Vy$p", "l1\n|l1\nl2" },
		{ "|ab", "v\033x", "|b" },
		{ "|", "x", "|" },
		{ "|", "dd", "|" },
		{ "|", "iX\033", "|X" },
		{ "|", "oX\033", "\n|X" },
		{ "|", "p", "|" },
		{ "|a", "ddp", "\n|a" },
		{ "|ab", "dw", "|" },
		{ "a|b", "dw", "|a" },
		{ "a |b", "dw", "a| " },
		{ "ab|\ncd", "x", "|a\ncd" },
		{ "ab\n|\ncd", "x", "ab\n|\ncd" },
		{ "ab\n|\ncd", "dd", "ab\n|cd" },
		{ "|one  two", "cwX\033", "|X  two" },
		{ "one| two", "cwX\033", "one|Xtwo" },
		{ "|ab cd", "5w", "ab c|d" },
		{ "|ab cd", "5e", "ab c|d" },
		{ "ab |cd", "5b", "|ab cd" },
		{ "|l1\nl2", "9G", "l1\n|l2" },
		{ "|l1\nl2", "9j", "l1\n|l2" },
		{ "l1\n|l2", "9k", "|l1\nl2" },
		{ "|l1\nl2", "9dd", "|" },
		{ "|l1\nl2", "yy9p", "l1\n|l1\nl1\nl1\nl1\nl1\nl1\nl1\nl1\nl1\nl2" },
		{ "l1\n|l2", "yyP", "l1\n|l2\nl2" },
		{ "l1\n|l2", "ddP", "|l2\nl1" },
		{ "|l1\nl2", "J", "l1| l2" },
		{ "l1\n|l2", "J", "l1\n|l2" },
		{ "|l1", "J", "|l1" },
		{ "|abc", "$a!\033", "abc|!" },
		{ "|abc", "A\b\bX\033", "a|X" },
		{ "|ab\ncd", "jdk", "|" },
		{ "|ab\ncd\nef", "jdj", "|ab" },
		{ "|ab", "yl3p", "aaa|ab" },
		{ "|ab", "yl3P", "aa|aab" },
		{ "|hello", "fz", "|hello" },
		{ "|hello", "dfz", "|hello" },
		{ "he|llo", "d0", "|llo" },
		{ "|a b c d", "dW.", "|c d" },
		{ "|a\nb\nc", "jyykp", "a\n|b\nb\nc" },
		{ "|  ab", "0cwX\033", "|Xab" },
		{ "|ab", "vd", "|b" },
		{ "a|b", "v$y0P", "|bab" },
		{ "|one\ntwo\nthree", "Vjy", "|one\ntwo\nthree" },
		{ "|one\ntwo\nthree", "VjyGp", "one\ntwo\nthree\n|one\ntwo" },
		{ "|abc def", "wvbd", "|ef" },
		{ "|a\n\nb", "}dd", "a\n|b" },
		{ "|x", "rX~", "|x" },
		{ "|abc", "d\033x", "|bc" },
		{ "|abc", "3\033x", "|bc" },
		/* registers, marks and the command line */
		{ "|one two", "\"ayw\"bywP\"aP", "oneone|  one two" },
		{ "|one two", "\"ayw$\"ap", "one twoone| " },
		{ "|l1\nl2", "\"ayyj\"ap", "l1\nl2\n|l1" },
		{ "|one two", "\"adwP", "one| two" },
		{ "|one two", "\"ayww\"Ayw0\"aP", "one tw|oone two" },
		{ "|l1\nl2\nl3", "\"ayyj\"Ayyj\"ap", "l1\nl2\nl3\n|l1\nl2" },
		{ "|ab cd", "v\"ay$\"ap", "ab cd|a" },
		{ "|one two", "\"aywp", "oone| ne two" },
		{ "|ab", "\"ax\"ap", "b|a" },
		{ "|one two", "\"a2yw$\"ap", "one twoone tw|o" },
		{ "|l1\nl2\nl3", "majj'a", "|l1\nl2\nl3" },
		{ "l|1\nl2\nl3", "majj`a", "l|1\nl2\nl3" },
		{ "|l1\nl2\nl3", "jmakd'a", "|l3" },
		{ "|abc def", "wmb0d`b", "|def" },
		{ "|l1\nl2\nl3", "jjmaggOx\033'a", "x\nl1\nl2\n|l3" },
		{ "ab|cdef", "mb0x`b", "bc|def" },
		{ "|l1\nl2\nl3", "jmaggy'aP", "|l1\nl2\nl1\nl2\nl3" },
		{ "|l1\nl2\nl3", ":3\r", "l1\nl2\n|l3" },
		{ "l1\nl2\n|l3", ":1\r", "|l1\nl2\nl3" },
		{ "|l1\n  l2", ":2\r", "l1\n  |l2" },
		{ "|l1\nl2", ":$\r", "l1\n|l2" },
		{ "|l1\nl2\nl3", ":2\b3\r", "l1\nl2\n|l3" },
	};

	char reg[256], got[256];
	Vi v;
	ViHost h;
	size_t i;

	/* every case checked against vim (tests/vi_oracle.py), with LF and CR */
	for (i = 0; i < sizeof(kVi) / sizeof(kVi[0]); i++) {
		ViCase(kVi[i][0], kVi[i][1], kVi[i][2], 0, (int) i);
		ViCase(kVi[i][0], kVi[i][1], kVi[i][2], 1, (int) i);
	}

	/* off: every key is the app's */
	VhInit(&h, &v, reg, sizeof(reg), "|ab");
	v.on = 0;
	CHECK(vi_key(&v, &h, 'x', 0) == 0);
	CHECK_STR(vi_status(&v), "");
	v.on = 1;
	CHECK_STR(vi_status(&v), "");
	CHECK(vi_key(&v, &h, 'i', 0) == 1);
	CHECK_STR(vi_status(&v), "-- INSERT --");
	CHECK(vi_key(&v, &h, 0x1C, 0) == 0);            /* an arrow key: the app's */
	CHECK(vi_key(&v, &h, VI_ESC, 0) == 1);
	CHECK(vi_key(&v, &h, 'V', 0) == 1);
	CHECK_STR(vi_status(&v), "-- VISUAL LINE --");
	CHECK(gVs == 0 && gVe == 2);
	(void) vi_key(&v, &h, 'v', 0);
	CHECK_STR(vi_status(&v), "-- VISUAL --");
	(void) vi_key(&v, &h, VI_ESC, 0);
	gVs = 1;
	gVe = 1;
	vi_clicked(&v, &h);
	CHECK(v.mode == VI_NORMAL && gVs == 1 && gVe == 1);
	gVs = 0;
	gVe = 2;
	vi_clicked(&v, &h);                 /* dragged over "ab": visual */
	CHECK(v.mode == VI_VISUAL);
	(void) vi_key(&v, &h, 'd', 0);
	CHECK(gVn == 0 && v.mode == VI_NORMAL);

	/* jk: quickly, it's Escape; slowly, it's text */
	VhInit(&h, &v, reg, sizeof(reg), "|ab");
	v.jk = 1;
	(void) vi_key(&v, &h, 'i', 1000);
	(void) vi_key(&v, &h, 'x', 1001);
	(void) vi_key(&v, &h, 'j', 1002);
	(void) vi_key(&v, &h, 'k', 1010);
	VhShow(got);
	CHECK_STR(got, "|xab");
	CHECK(v.mode == VI_NORMAL);
	(void) vi_key(&v, &h, 'u', 1100);
	VhShow(got);
	CHECK_STR(got, "|ab");
	VhInit(&h, &v, reg, sizeof(reg), "|ab");
	v.jk = 1;
	(void) vi_key(&v, &h, 'i', 1000);
	(void) vi_key(&v, &h, 'j', 1002);
	(void) vi_key(&v, &h, 'k', 1200);
	CHECK(v.mode == VI_INSERT);
	(void) vi_key(&v, &h, VI_ESC, 1300);
	VhShow(got);
	CHECK_STR(got, "j|kab");

	/* a full register: what fits */
	{
		char small[4];
		VhInit(&h, &v, small, sizeof(small), "|abcdef");
		(void) vi_key(&v, &h, 'y', 0);
		(void) vi_key(&v, &h, '$', 0);
		(void) vi_key(&v, &h, 'P', 0);
		VhShow(got);
		CHECK_STR(got, "ab|cabcdef");
	}

	/* any keys at all: no reads or writes out of bounds, the caret in the text */
	{
		static const char kKeys[] = "hjklwbeWBE0^$GgfFtT;,dcyxXDCsSYpPrJ~iaIAoOvVuU.{}\033\033\033\b\r 12a";
		char big[64];
		int round, j;
		srand(4);
		for (round = 0; round < 3000; round++) {
			int len = rand() % 40;
			char text[64];
			for (j = 0; j < len; j++)
				text[j] = "ab .(\n\n x"[rand() % 9];
			text[len] = '\0';
			memmove(text + 1, text, (size_t) len + 1);
			text[0] = '|';
			VhInit(&h, &v, big, sizeof(big), text);
			for (j = 0; j < 30; j++) {
				(void) vi_key(&v, &h, (unsigned char) kKeys[rand() % (int) (sizeof(kKeys) - 1)], (unsigned long) j);
				if (gVs < 0 || gVe > gVn || gVs > gVe) {
					gFailed++;
					break;
				}
			}
		}
		CHECK(round == 3000);
	}
}

/* ---- dict and spell ---- */

/* A section, as tools/mkdict.py makes one, from sorted words */
static long MakeDict(const char *const *words, int count, int block, unsigned char *out)
{
	unsigned char index[2048], data[8192];
	long ilen = 0, dlen = 0, bstart = 0, n;
	const char *prev = "";
	int i, nb = 0, inblock = 0;

	for (i = 0; i < count; i++) {
		const char *w = words[i];
		int shared = 0, len = (int) strlen(w);
		if (inblock == 0) {
			bstart = dlen;
			prev = "";
			index[ilen] = (unsigned char) (bstart >> 24);
			index[ilen + 1] = (unsigned char) (bstart >> 16);
			index[ilen + 2] = (unsigned char) (bstart >> 8);
			index[ilen + 3] = (unsigned char) bstart;
			index[ilen + 6] = (unsigned char) len;
			memcpy(index + ilen + 7, w, (size_t) len);
		}
		while (prev[shared] && w[shared] && prev[shared] == w[shared])
			shared++;
		data[dlen++] = (unsigned char) shared;
		data[dlen++] = (unsigned char) (len - shared);
		memcpy(data + dlen, w + shared, (size_t) (len - shared));
		dlen += len - shared;
		prev = w;
		inblock++;
		if (dlen - bstart >= block || i == count - 1) {
			index[ilen + 4] = (unsigned char) ((dlen - bstart) >> 8);
			index[ilen + 5] = (unsigned char) (dlen - bstart);
			ilen += 7 + index[ilen + 6];
			nb++;
			inblock = 0;
		}
	}
	memcpy(out, "TPDC\0\1", 6);
	out[6] = (unsigned char) (nb >> 8);
	out[7] = (unsigned char) nb;
	n = count;
	out[8] = 0; out[9] = 0; out[10] = (unsigned char) (n >> 8); out[11] = (unsigned char) n;
	out[12] = 0; out[13] = 0; out[14] = (unsigned char) (ilen >> 8); out[15] = (unsigned char) ilen;
	memcpy(out + 16, index, (size_t) ilen);
	memcpy(out + 16 + ilen, data, (size_t) dlen);
	return 16 + ilen + dlen;
}

static const unsigned char *gFile;
static int gReads;

static long ReadFile(void *ctx, long offset, unsigned char *buf, long len)
{
	(void) ctx;
	gReads++;
	memcpy(buf, gFile + offset, (size_t) len);
	return len;
}

/* A dictionary from words, all in memory or read through a small cache */
static void OpenDict(Dict *d, const char *const *words, int count, unsigned char *file, long *entries,
		int reader, unsigned char *cache, long *cached, unsigned long *used)
{
	long blocks, nw, ilen;

	(void) MakeDict(words, count, 24, file);
	CHECK(dict_header(file, &blocks, &nw, &ilen));
	CHECK(dict_init(d, file + DICT_HEADER, ilen, blocks, nw, entries));
	if (reader) {
		gFile = file;
		dict_reader(d, ReadFile, NULL, DICT_HEADER + ilen, cache, 2, cached, used);
	} else {
		dict_data(d, file + DICT_HEADER + ilen);
	}
}

static int UserWords(void *ctx, const char *w, int len)
{
	(void) ctx;
	return len == 6 && memcmp(w, "Topica", 6) == 0;
}

static void TestSpell(void)
{
	static const char *const kCommon[] = { "a", "and", "is", "it's", "the", "word" };
	static const char *const kMain[] = {
		"Apple", "Paris", "a", "and", "apple", "cat", "cats", "dog", "don't", "is", "it's", "quick",
		"the", "they", "word", "words", "world", "writer", "writers"
	};
	static const char *const kAll[] = {
		"Apple", "Paris", "a", "and", "apple", "cat", "cats", "dog", "don't", "is", "it's", "quick",
		"the", "they", "word", "words", "world", "writer", "writers"
	};
	unsigned char f1[4096], f2[4096], cache[2 * 64];
	long e1[64], e2[64], cached[2];
	unsigned long used[2];
	char sug[8][DICT_WORD];
	Dict common, all;
	Speller sp;
	int i, n, len;

	/* every word found, at the start, inside and at the end of blocks */
	OpenDict(&all, kAll, (int) (sizeof(kAll) / sizeof(kAll[0])), f2, e2, 0, NULL, NULL, NULL);
	CHECK(all.blocks > 3);
	for (i = 0; i < (int) (sizeof(kAll) / sizeof(kAll[0])); i++)
		CHECK(dict_has(&all, kAll[i], (int) strlen(kAll[i])));
	CHECK(!dict_has(&all, "", 0));
	CHECK(!dict_has(&all, "Aardvark", 8));
	CHECK(!dict_has(&all, "zebra", 5));
	CHECK(!dict_has(&all, "wor", 3));
	CHECK(!dict_has(&all, "worlds", 6));
	CHECK(!dict_has(&all, "Cat", 3));

	/* from the file a block at a time: the cache keeps two */
	gReads = 0;
	OpenDict(&all, kAll, (int) (sizeof(kAll) / sizeof(kAll[0])), f2, e2, 1, cache, cached, used);
	CHECK(all.maxBlock <= 64);
	CHECK(dict_has(&all, "writer", 6) && dict_has(&all, "writers", 7));
	CHECK(gReads == 1);
	CHECK(dict_has(&all, "Apple", 5) && dict_has(&all, "writer", 6));
	CHECK(gReads == 2);
	for (i = 0; i < (int) (sizeof(kAll) / sizeof(kAll[0])); i++)
		CHECK(dict_has(&all, kAll[i], (int) strlen(kAll[i])));
	CHECK(!dict_has(&all, "zebra", 5));

	/* a damaged index */
	{
		Dict bad;
		long blocks, nw, ilen;
		CHECK(dict_header(f2, &blocks, &nw, &ilen));
		CHECK(!dict_init(&bad, f2 + DICT_HEADER, ilen - 1, blocks, nw, e1));
		f1[0] = 'X';
		CHECK(!dict_header(f1, &blocks, &nw, &ilen));
	}

	OpenDict(&common, kCommon, (int) (sizeof(kCommon) / sizeof(kCommon[0])), f1, e1, 0, NULL, NULL, NULL);
	OpenDict(&all, kMain, (int) (sizeof(kMain) / sizeof(kMain[0])), f2, e2, 0, NULL, NULL, NULL);
	sp.common = &common;
	sp.main = &all;
	sp.user = UserWords;
	sp.ctx = NULL;
	CHECK(sp_correct(&sp, "word", 4));
	CHECK(sp_correct(&sp, "Word", 4));          /* a sentence's start */
	CHECK(sp_correct(&sp, "WORD", 4));
	CHECK(!sp_correct(&sp, "wOrd", 4));
	CHECK(sp_correct(&sp, "Paris", 5));
	CHECK(!sp_correct(&sp, "paris", 5));        /* a proper noun needs its capital */
	CHECK(sp_correct(&sp, "PARIS", 5));
	CHECK(sp_correct(&sp, "cat's", 5));
	CHECK(sp_correct(&sp, "cat\xD5s", 5));      /* a curly apostrophe */
	CHECK(sp_correct(&sp, "writers'", 8));
	CHECK(sp_correct(&sp, "don\xD5t", 5));
	CHECK(sp_correct(&sp, "Topica", 6));        /* the writer's own */
	CHECK(!sp_correct(&sp, "wrod", 4));
	CHECK(!sp_correct(&sp, "teh", 3));

	n = sp_suggest(&sp, "teh", 3, sug, 8);
	CHECK(n >= 1);
	CHECK_STR(sug[0], "the");
	n = sp_suggest(&sp, "Teh", 3, sug, 8);
	CHECK(n >= 1 && strcmp(sug[0], "The") == 0);
	n = sp_suggest(&sp, "TEH", 3, sug, 8);
	CHECK(n >= 1 && strcmp(sug[0], "THE") == 0);
	n = sp_suggest(&sp, "wrod", 4, sug, 8);
	CHECK(n >= 1 && strcmp(sug[0], "word") == 0);
	n = sp_suggest(&sp, "wordd", 5, sug, 8);
	CHECK(n >= 1 && strcmp(sug[0], "word") == 0);
	n = sp_suggest(&sp, "wrld", 4, sug, 8);
	CHECK(n >= 1 && strcmp(sug[0], "world") == 0);
	n = sp_suggest(&sp, "paris", 5, sug, 8);
	CHECK(n >= 1 && strcmp(sug[0], "Paris") == 0);
	n = sp_suggest(&sp, "Pariss", 6, sug, 8);
	CHECK(n >= 1 && strcmp(sug[0], "Paris") == 0);
	n = sp_suggest(&sp, "dont", 4, sug, 8);
	CHECK(n >= 1 && strcmp(sug[0], "don't") == 0);
	n = sp_suggest(&sp, "thequick", 8, sug, 8);
	CHECK(n >= 1 && strcmp(sug[n - 1], "the quick") == 0);
	CHECK(sp_suggest(&sp, "zzzzzz", 6, sug, 8) == 0);
	n = sp_suggest(&sp, "cta", 3, sug, 1);
	CHECK(n == 1 && strcmp(sug[0], "cat") == 0);

	/* which words get checked */
	{
		static const char kPost[] =
			"---\ntitle: A wrod title\nlayout: psot\ntags: [abc]\n---\n"
			"Some wrod in `cod` and [lnk](http://exampel.com/pth). HTML and CSS, iPhone, x86 and\n"
			"see exampel.com or me@exampel.com, a_b, &amp; {% incld %} <spn>txt</spn> don\xD5t.\n";
		static const char *const kWant[] = {
			"wrod", "title", "Some", "wrod", "in", "and", "lnk", "and", "and", "see", "or", "txt", "don\xD5t", NULL
		};
		unsigned char cls[512];
		MdState st;
		long n2 = (long) strlen(kPost), at = 0;
		int k = 0;
		md_begin(&st);
		md_classify(&st, kPost, n2, n2, cls);
		sp_prepare(kPost, n2, cls);
		while ((at = sp_next(kPost, n2, at, cls, &len)) >= 0) {
			char w[64];
			memcpy(w, kPost + at, (size_t) len);
			w[len] = '\0';
			if (kWant[k] == NULL) {
				CheckStr(w, "(no more)", __LINE__);
				break;
			}
			CheckStr(w, kWant[k], __LINE__);
			k++;
			at += len;
		}
		CHECK(kWant[k] == NULL);
		/* TOML front matter: a title's words are checked, other keys' values aren't */
		{
			static const char kToml[] = "+++\ntitle = \"A wrod\"\nlayout = \"psot\"\ntitlex = \"zzz\"\n+++\nOk.\n";
			static const char *const kWantToml[] = { "wrod", "Ok", NULL };
			unsigned char cls3[128];
			long n3 = (long) strlen(kToml), at3 = 0;
			int len3;
			int k3 = 0;
			md_begin(&st);
			md_classify(&st, kToml, n3, n3, cls3);
			sp_prepare(kToml, n3, cls3);
			while ((at3 = sp_next(kToml, n3, at3, cls3, &len3)) >= 0 && kWantToml[k3] != NULL) {
				char w[64];
				memcpy(w, kToml + at3, (size_t) len3);
				w[len3] = '\0';
				CheckStr(w, kWantToml[k3], __LINE__);
				k3++;
				at3 += len3;
			}
			CHECK(kWantToml[k3] == NULL && at3 < 0);
		}
		/* from the middle of a word: that word */
		at = sp_next(kPost, n2, (long) (strstr(kPost, "Some") - kPost) + 2, cls, &len);
		CHECK(at == strstr(kPost, "Some") - kPost && len == 4);
	}
}

static void TestPublish(void)
{
	unsigned long ip = 0;
	unsigned short port = 0;
	char out[600], msg[64];
	int status = 0;
	long n;
	const char *ok = "HTTP/1.0 200 OK\r\nServer: x\r\nContent-Type: text/plain\r\nContent-Length: 39\r\n\r\n"
		"Published _posts/a.md on drafts (abc).\n";

	/* the bridge's address */
	CHECK(pub_address("192.0.2.10", &ip, &port) && ip == 0xC000020AUL && port == PUB_PORT);
	CHECK(pub_address("http://10.0.2.2:8080/", &ip, &port) && ip == 0x0A000202UL && port == 8080);
	CHECK(pub_address("10.0.2.2:1", &ip, &port) && port == 1);
	CHECK(!pub_address("", &ip, &port));
	CHECK(!pub_address("example.com", &ip, &port));
	CHECK(!pub_address("https://10.0.2.2/", &ip, &port));
	CHECK(!pub_address("10.0.2", &ip, &port));
	CHECK(!pub_address("10.0.2.256", &ip, &port));
	CHECK(!pub_address("10.0.2.2:", &ip, &port));
	CHECK(!pub_address("10.0.2.2:0", &ip, &port));
	CHECK(!pub_address("10.0.2.2:65536", &ip, &port));
	CHECK(!pub_address("10.0.2.2/x", &ip, &port));
	CHECK(!pub_address("1.2.3.4 ", &ip, &port));

	/* the post's path */
	pub_path(0, 0, 0, "2026-10-23-hello.md", "Other", "2026-01-01", out, sizeof(out));
	CHECK_STR(out, "_posts/2026-10-23-hello.md");
	pub_path(0, 0, 0, "Hello World", "Hello, W\x9Arld!", "2026-10-23 09:00:00 -0700", out, sizeof(out));
	CHECK_STR(out, "_posts/2026-10-23-hello-world.md");
	pub_path(0, 0, 0, "untitled", "", "", out, sizeof(out));
	CHECK_STR(out, "_posts/untitled.md");                  /* no date: the dialog asks for one */
	pub_path(0, 1, 0, "2026-10-23-hello.md", "x", "2026-10-23", out, sizeof(out));
	CHECK_STR(out, "_drafts/hello.md");
	pub_path(0, 1, 0, "notes.md", "x", "2026-10-23", out, sizeof(out));
	CHECK_STR(out, "_drafts/notes.md");
	pub_path(0, 1, 0, "My Notes", "Caf\x8E Notes", "2026-10-23", out, sizeof(out));
	CHECK_STR(out, "_drafts/cafe-notes.md");
	pub_path(1, 0, 0, "hello.md", "x", "2026-10-23", out, sizeof(out));
	CHECK_STR(out, "content/posts/hello.md");
	pub_path(1, 1, 0, "Hello", "Hugo Post", "", out, sizeof(out));
	CHECK_STR(out, "content/posts/hugo-post.md");
	pub_path(0, 0, 0, "2026-10-23-hello.md", "x", "", out, 12);
	CHECK_STR(out, "_posts/2026");
	CHECK(pub_path_ok("_posts/2026-10-23-hello.md"));
	CHECK(pub_path_ok("_drafts/a.markdown"));
	CHECK(pub_path_ok("content/posts/A_b.c-d.md"));
	CHECK(!pub_path_ok("_posts/../x.md"));
	CHECK(!pub_path_ok("_posts/sub/x.md"));
	CHECK(!pub_path_ok("_posts/.x.md"));
	CHECK(!pub_path_ok("_posts/x.txt"));
	CHECK(!pub_path_ok("_posts/.md"));
	CHECK(!pub_path_ok("_config.yml"));
	CHECK(!pub_path_ok("_posts/a b.md"));
	CHECK(!pub_path_ok("_posts/untitled.md"));             /* Jekyll skips a post without a date */
	CHECK(!pub_path_ok("_posts/2026-10-2-x.md"));
	pub_path(0, 0, 1, "Hello World", "Hello", "2026-10-23", out, sizeof(out));
	CHECK_STR(out, "_posts/2026/2026-10-23-hello.md");
	pub_path(0, 0, 1, "untitled", "", "", out, sizeof(out));
	CHECK_STR(out, "_posts/untitled.md");
	pub_path(0, 1, 1, "Hello World", "Hello", "2026-10-23", out, sizeof(out));
	CHECK_STR(out, "_drafts/hello.md");
	pub_path(1, 0, 1, "Hello World", "Hello", "2026-10-23", out, sizeof(out));
	CHECK_STR(out, "content/posts/hello.md");
	CHECK(pub_path_ok("_posts/2026/2026-10-23-hello.md"));
	CHECK(pub_path_years("_posts/2026/2026-10-23-hello.md"));
	CHECK(!pub_path_years("_posts/2026-10-23-hello.md"));
	CHECK(!pub_path_years("_posts/2026"));
	CHECK(!pub_path_years("_posts/"));
	CHECK(!pub_path_ok("_posts/2025/2026-10-23-hello.md"));
	CHECK(!pub_path_ok("_posts/2026/hello.md"));
	CHECK(!pub_path_ok("_posts/2026/10/2026-10-23-x.md"));
	CHECK(!pub_path_ok("_posts/2026/2026-10-23-a/b.md"));
	CHECK(!pub_path_ok("_posts/2026/"));
	CHECK(!pub_path_ok("_drafts/2026/2026-10-23-x.md"));

	/* the request */
	n = pub_request(out, sizeof(out), "10.0.2.2", 8023, "kkkkkkkkkkkkkkkk", "_posts/a.md", 1234);
	CHECK(n == (long) strlen(out));
	CHECK(strncmp(out, "POST /publish HTTP/1.0\r\nHost: 10.0.2.2:8023\r\n", 45) == 0);
	CHECK(strstr(out, "\r\nX-Topica-Key: kkkkkkkkkkkkkkkk\r\n") != NULL);
	CHECK(strstr(out, "\r\nX-Topica-Path: _posts/a.md\r\n") != NULL);
	CHECK(n > 26 && strcmp(out + n - 26, "\r\nContent-Length: 1234\r\n\r\n") == 0);
	CHECK(pub_request(out, 100, "10.0.2.2", 8023, "k", "_posts/a.md", 1) == -1);

	/* the reply */
	CHECK(pub_reply(ok, (long) strlen(ok), 0, &status, msg, sizeof(msg)) == 1);
	CHECK(status == 200);
	CHECK_STR(msg, "Published _posts/a.md on drafts (abc).");
	CHECK(pub_reply(ok, (long) strlen(ok) - 1, 0, &status, msg, sizeof(msg)) == 0);
	CHECK(pub_reply(ok, (long) strlen(ok) - 1, 1, &status, msg, sizeof(msg)) == 1);
	CHECK(pub_reply(ok, 40, 0, &status, msg, sizeof(msg)) == 0);
	CHECK(pub_reply(ok, 40, 1, &status, msg, sizeof(msg)) == -1);
	CHECK(pub_reply(ok, 3, 0, &status, msg, sizeof(msg)) == 0);
	CHECK(pub_reply("SSH-2.0-x\r\n", 11, 0, &status, msg, sizeof(msg)) == -1);
	CHECK(pub_reply(ok, (long) strlen(ok), 0, &status, msg, 10) == 1);
	CHECK_STR(msg, "Published");
	{
		const char *lf = "HTTP/1.0 401 Unauthorized\nContent-Length: 4\n\nNo.\nmore";
		CHECK(pub_reply(lf, (long) strlen(lf), 0, &status, msg, sizeof(msg)) == 1);
		CHECK(status == 401);
		CHECK_STR(msg, "No.");
	}
	{
		const char *empty = "HTTP/1.1 502 Bad Gateway\r\n\r\n";
		CHECK(pub_reply(empty, (long) strlen(empty), 0, &status, msg, sizeof(msg)) == 0);
		CHECK(pub_reply(empty, (long) strlen(empty), 1, &status, msg, sizeof(msg)) == 1);
		CHECK(status == 502);
		CHECK_STR(msg, "Bad Gateway");
	}
	{
		const char *ctl = "HTTP/1.0 200 OK\r\n\r\nA\tB\x80" "C";
		CHECK(pub_reply(ctl, (long) strlen(ctl), 1, &status, msg, sizeof(msg)) == 1);
		CHECK_STR(msg, "A?B?C");
	}
}

int main(void)
{
	TestTextconv();
	TestFrontmatterGet();
	TestFrontmatterToml();
	TestFrontmatterUpdate();
	TestUndo();
	TestUndoSaved();
	TestUndoJoin();
	TestPost();
	TestFind();
	TestPrefs();
	TestRecovery();
	TestMdscan();
	TestStraightQuotes();
	TestMdpage();
	TestLayout();
	TestMdhtml();
	TestVi();
	TestViCommands();
	TestSpell();
	TestPublish();
	printf("%d checks, %d failed\n", gRun, gFailed);
	return gFailed ? 1 : 0;
}
