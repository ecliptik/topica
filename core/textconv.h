/*
 * textconv.h: a post on disk (UTF-8, LF) and in the editor (Mac Roman, CR).
 *
 * Opening: LF and CRLF become CR; UTF-8 becomes Mac Roman. A character Mac
 * Roman lacks becomes a hex entity, "&#xFF41;", which kramdown renders as
 * the same character. Saving: CR becomes LF, Mac Roman becomes UTF-8, and
 * an entity for a character Mac Roman lacks becomes that character's UTF-8
 * again, so the round trip gives back the same bytes. An entity for a
 * character Mac Roman has ("&#x2014;") was typed by the writer and stays.
 *
 * Every function takes a length (the text may hold NULs) and, given a NULL
 * out, only measures. They return the output's length, or -1 when it
 * would not fit in cap.
 */
#ifndef TOPICA_TEXTCONV_H
#define TOPICA_TEXTCONV_H

/* 1 when the bytes are valid UTF-8 (plain ASCII is). */
int tc_is_utf8(const char *in, long n);

/* Disk to editor. utf8: decode UTF-8 (else the bytes are already Mac Roman). */
long tc_to_mac(const char *in, long n, char *out, long cap, int utf8);

/* Editor to disk. utf8: write UTF-8 (else Mac Roman); lf: LF line ends (else CR). */
long tc_to_disk(const char *in, long n, char *out, long cap, int utf8, int lf);

/* One code point as Mac Roman, or -1 when Mac Roman has no such character. */
int tc_roman_from_cp(unsigned long cp);
/* One Mac Roman byte as a code point. */
unsigned long tc_cp_from_roman(unsigned char c);

#endif
