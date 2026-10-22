/* Host tests for Topica's portable core: make -C tests test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/find.h"
#include "../core/frontmatter.h"
#include "../core/prefs.h"
#include "../core/recovery.h"
#include "../core/post.h"
#include "../core/textconv.h"
#include "../core/undo.h"

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
	char buf[512];
	long n;

	prefs_default(&p);
	CHECK_STR(p.font, "Monaco");
	CHECK(p.size == 9 && p.utf8 && p.lf && p.autosaveMinutes == 2 && p.profile == kProfileJekyll);
	{
		const char *text = "# a comment\rfont: Geneva\rsize: 12\rblog: hugo\rencoding: mac-roman\r"
			"line-ends: cr\rautosave-minutes: 5\rimage-folder: /img/\rlayout: page\runknown: x\r";
		prefs_read(&p, text, (long) strlen(text));
	}
	CHECK_STR(p.font, "Geneva");
	CHECK(p.size == 12 && p.profile == kProfileHugo && !p.utf8 && !p.lf && p.autosaveMinutes == 5);
	CHECK_STR(p.imagePath, "/img/");
	CHECK_STR(p.layout, "page");
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

int main(void)
{
	TestTextconv();
	TestFrontmatterGet();
	TestFrontmatterUpdate();
	TestUndo();
	TestPost();
	TestFind();
	TestPrefs();
	TestRecovery();
	printf("%d checks, %d failed\n", gRun, gFailed);
	return gFailed ? 1 : 0;
}
