/*
 * frontmatter.h: a post's front matter, the block at the top between "---"
 * lines (YAML) or "+++" lines (TOML, as Hugo has it). Only the top-level
 * keys Post Info edits are read and written; every other line stays
 * exactly as it was.
 *
 * Values are read as YAML would: plain, "double" or 'single' quoted, a flow
 * list [a, b] or a block list ("- a" lines), lists joined with spaces.
 * A value that is something else (a "|" block, a nested map) is complex,
 * and Post Info leaves it alone. Lines may end in CR, LF or CRLF.
 *
 * TOML's are key = value lines: "basic" and 'literal' strings, one-line
 * arrays, and bare booleans, numbers and dates. The top-level keys end at
 * the first [table]; a multi-line array or string is complex. Written
 * values follow TOML: strings are always quoted, and an empty one is "".
 */
#ifndef TOPICA_FRONTMATTER_H
#define TOPICA_FRONTMATTER_H

enum { kFmAbsent = 0, kFmFound = 1, kFmComplex = 2 };

/* The block's byte range, from the opening "---" to just past the closing
   line's line end. 1 when there is one. */
int fm_find(const char *text, long n, long *start, long *end);

/* 1 when text's front matter is TOML ("+++") */
int fm_is_toml(const char *text, long n);

/* A key's value into out (NUL-terminated, cut to cap). kFmAbsent,
   kFmFound or kFmComplex; out is "" unless found. */
int fm_get(const char *text, long n, const char *key, char *out, long cap);

typedef struct {
	const char *key;
	const char *value;      /* lists: items separated by spaces */
} FmField;

/*
 * Writes a new front-matter block with the fields set: a key already there
 * keeps its place and style (quotes, list form); a missing key is added at
 * the end, unless its value is empty. A complex key is left as it is. With
 * no front matter in text, a new block is made. eol is the line end to
 * write; a new block is YAML. Returns the block's length, -1 when it doesn't fit in cap; out
 * NULL only measures. Replace [start, end) from fm_find with it (or insert
 * it at 0 when there was none).
 */
long fm_update(const char *text, long n, const FmField *fields, int count, char eol, char *out, long cap);

/* 1 when value has to be quoted to stay a plain YAML string. */
int fm_needs_quotes(const char *value);

#endif
