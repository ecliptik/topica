/*
 * net.h: one TCP connection at a time, for Publish. Each call waits until
 * it's done or its time is up (Publish sends one post and reads one line
 * back), asking the cancel routine (NetSetCancel) now and then so that
 * Command-period can end it. Two builds: MacTCP's .IPP driver (net_mactcp.c), or talkip's
 * stack linked into the application for a 512K Mac on LocalTalk
 * (net_talkip.c, scripts/build.sh --talkip).
 */
#ifndef TOPICA_NET_H
#define TOPICA_NET_H

#include <MacTypes.h>

/* Results are MacTCP's (app/mactcp.h: talkip's are the same), or our own:
   no network at all, or (talkip) no address from the MacIP gateway; and
   for a name, no such name, no name server, or no answer from it */
enum {
	netNoNetwork = -30101, netNoAddress = -30102, netNoName = -30103, netNoNameServer = -30104,
	netNoAnswer = -30105, netCanceled = -128 /* userCanceledErr */
};

/* The address of a host name: MacTCP's resolver (the name servers its
   control panel lists, and its Hosts file), or talkip's (the server the
   MacIP gateway hands out) */
OSErr NetResolve(const char *name, unsigned long *ip);
/* Before quitting: MacTCP's resolver must be closed, or the Mac crashes later */
void NetShutdown(void);

/* Connects to ip:port, giving up after timeoutSecs */
OSErr NetOpen(unsigned long ip, unsigned short port, short timeoutSecs);
/* Sends all of [data, data+len) */
OSErr NetSend(const char *data, long len);
/* Receives what has come, up to cap bytes, into buf: *got of them.
   connectionClosing once the other end has finished. */
OSErr NetRecv(char *buf, long cap, long *got, short timeoutSecs);
/* Closes the connection (or abandons it), and frees the stream. Always safe. */
void NetClose(void);
/* From the event loop: talkip runs in the application and needs the time */
void NetIdle(void);
/* While a call waits, it asks cancel (when not NULL) now and then: true
   abandons the connection, and the call returns netCanceled */
void NetSetCancel(Boolean (*cancel)(void));

#endif
