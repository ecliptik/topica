/*
 * publish.h: Post > Publish's side of the bridge (bridge/topica_bridge.py):
 * the bridge's address, the post's place in the blog, the request's head
 * and the reply. Portable C89; the bytes move in app/net.c.
 */
#ifndef TOPICA_PUBLISH_H
#define TOPICA_PUBLISH_H

enum { PUB_PORT = 8023, PUB_PATH = 160 };

/* "192.0.2.10", "192.0.2.10:8023" or "http://192.0.2.10:8023/" -> the
   address and port (8023 when not given). 0 for anything else: a Mac has
   no names to look up, so the bridge is an address. */
int pub_address(const char *text, unsigned long *ip, unsigned short *port);

/*
 * The post's path in the blog's repository:
 *   Jekyll  "_posts/2026-10-23-title.md", a draft "_drafts/title.md"
 *   Hugo    "content/posts/title.md" (a draft says so in its front matter)
 * name is the post's file name on the Mac: used as it is when it already
 * has the blog's form ("2026-10-23-title.md" for a Jekyll post, with the
 * date dropped for a draft); otherwise the name comes from title (Mac
 * Roman) and date ("2026-10-23", the post's or today's).
 */
void pub_path(int hugo, int draft, const char *name, const char *title, const char *date, char *out, int cap);

/* 1 when the bridge takes path: a Markdown file right in _posts (dated,
   as Jekyll wants), _drafts or content/posts */
int pub_path_ok(const char *path);

/* The request's head: POST /publish with the key, the path and the body's
   length. Returns its length, -1 when it doesn't fit out. */
long pub_request(char *out, long cap, const char *host, unsigned short port, const char *key, const char *path,
		long bodyLen);

/*
 * The bridge's reply in buf: its status and the first line of its text
 * (printable ASCII, cut to cap). 1: complete; 0: more to come (closed says
 * the connection has ended, and then what came is all there is); -1: not
 * an HTTP reply.
 */
int pub_reply(const char *buf, long len, int closed, int *status, char *msg, int cap);

#endif
