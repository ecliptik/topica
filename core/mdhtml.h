/*
 * mdhtml.h: Export HTML. A post as a web page of its own: the head from
 * the front matter, then the Markdown as HTML, the way mdpage reads it.
 * HTML in the post stays HTML; Liquid can't run here and shows as text.
 * The output is Mac Roman with LF line ends: textconv's tc_to_disk makes
 * it UTF-8, as the page's <meta charset> says.
 */
#ifndef TOPICA_MDHTML_H
#define TOPICA_MDHTML_H

/* The page into out, up to cap bytes (out NULL measures). cls is scratch,
   n bytes. Returns the length, or -1 when it doesn't fit. */
long mh_page(const char *text, long n, unsigned char *cls, char *out, long cap);

#endif
