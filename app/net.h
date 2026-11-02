/*
 * net.h: one TCP connection at a time, for Publish, which runs in the
 * background: each operation is started, then asked after (NetPoll) from
 * the event loop until it's done, so the writer can go on writing. Two
 * builds: MacTCP's .IPP driver (net_mactcp.c), or talkip's stack linked
 * into the application for a 512K Mac on LocalTalk (net_talkip.c,
 * scripts/build.sh --talkip).
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

/* NetPoll's answer while the operation goes on */
enum { netBusy = 1 };

/* Starts looking up a host name: MacTCP's resolver (the name servers its
   control panel lists, and its Hosts file), or talkip's (the server the
   MacIP gateway hands out). NetAddress has the answer. */
OSErr NetResolveStart(const char *name);
/* Starts connecting to ip:port, giving up after timeoutSecs */
OSErr NetOpenStart(unsigned long ip, unsigned short port, short timeoutSecs);
/* Starts sending [data, data+len), which must stay where it is until done */
OSErr NetSendStart(const char *data, long len);
/* Starts receiving up to cap bytes into buf (which must stay put):
   NetGot says how many. connectionClosing once the other end has finished. */
OSErr NetRecvStart(char *buf, long cap, short timeoutSecs);
/* netBusy while the operation goes on; then its result, once */
OSErr NetPoll(void);
unsigned long NetAddress(void);
long NetGot(void);
/* Abandons the operation and the connection */
void NetCancel(void);
/* Closes the connection (or abandons it), and frees the stream. Always safe. */
void NetClose(void);
/* From the event loop: talkip runs in the application and needs the time */
void NetIdle(void);
/* Before quitting: MacTCP's resolver must be closed, or the Mac crashes later */
void NetShutdown(void);

#endif
