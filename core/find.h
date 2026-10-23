/*
 * find.h: Find and Replace over Mac Roman text. Matching can ignore case
 * (accented letters too: "É" finds "é") and can want whole words.
 */
#ifndef TOPICA_FIND_H
#define TOPICA_FIND_H

enum { kFindCase = 1, kFindWord = 2 };     /* flags: match case, whole words */

/* The first match at or after from, wrapping round to the start; -1 when
   there is none. */
long find_next(const char *text, long n, const char *pat, long plen, long from, int flags);

/* The last match ending at or before from, wrapping round to the end. */
long find_prev(const char *text, long n, const char *pat, long plen, long from, int flags);

/* Whether text[at, at + plen) is a match (for Replace, which replaces the
   selection only when it is one). */
int find_is_match(const char *text, long n, const char *pat, long plen, long at, int flags);

/* Every match replaced: the new text's length, -1 when it doesn't fit in
   cap (out NULL only measures); *count gets the number of replacements. */
long find_replace_all(const char *text, long n, const char *pat, long plen, const char *rep, long rlen,
	int flags, char *out, long cap, long *count);

/* Mac Roman lowercase and capitals, and whether a character is part of a word */
unsigned char find_lower(unsigned char c);
unsigned char find_upper(unsigned char c);
int find_is_word_char(unsigned char c);

#endif
