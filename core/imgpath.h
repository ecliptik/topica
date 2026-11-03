/*
 * imgpath.h: where an image in a post is on the Mac. A post's images are
 * web addresses, /assets/images/x.jpg from the site's top or images/x.jpg
 * from the post; when the blog's folders are on the Mac, the file is there
 * too. This makes an HFS partial path of the address, for the preview to
 * look for.
 */
#ifndef TOPICA_IMGPATH_H
#define TOPICA_IMGPATH_H

enum {
	IP_NONE,                    /* no file can be it: http:, data:, empty, too long */
	IP_SITE,                    /* from the site's top folder (it began with /) */
	IP_HERE                     /* from the post's folder */
};

/* The address in url [0, n) as an HFS partial path into out (a C string of
   at most 255 characters, cap with the NUL): ":assets:images:x.jpg", or
   "::x.jpg" for ../x.jpg from the post. %-escapes are decoded, UTF-8 made
   Mac Roman, the query and fragment dropped. Returns IP_NONE, IP_SITE or
   IP_HERE. */
int ip_path(const char *url, long n, char *out, int cap);

/* The path's last part as a partial path (":x.jpg"), into out */
void ip_leaf(const char *path, char *out, int cap);

/* The Mac file type its name's extension suggests ('JPEG', 'GIFf', 'PNGf',
   'PICT', 'BMPf', 'TIFF', '8BPS'), 0 when none */
unsigned long ip_type(const char *path);

#endif
