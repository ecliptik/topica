/*
 * net_talkip.c: net.h on talkip (TCP/IP over LocalTalk, linked into the
 * application), through its MacTCP-style calls. talkip starts with the
 * first connection: it opens AppleTalk socket 72 and leases an address
 * from the MacIP gateway; from then on NetIdle gives it the time it needs.
 * Its own ExitToShell patch closes the socket when Topica quits.
 */
#include <Memory.h>
#include <string.h>

#include <MacTCP.h>
#include <talkip.h>

#include "net.h"

/* talkip's MacTCP-style calls (its tcpshim.c) */
OSErr _TCPInit(void);
OSErr _TCPCreate(TCPiopb *pb, StreamPtr *stream, Ptr rcvBufPtr, long rcvBufLen, TCPNotifyUPP aNotifyProc,
		Ptr userDataPtr, TCPIOCompletionUPP ioCompletion, Boolean async);
OSErr _TCPActiveOpen(TCPiopb *pb, StreamPtr stream, ip_addr remoteIP, tcp_port remotePort, ip_addr *localIP,
		tcp_port *localPort, Ptr userData, TCPIOCompletionUPP ioCompletion, Boolean async);
OSErr _TCPSend(TCPiopb *pb, StreamPtr stream, wdsEntry *wdsPtr, Ptr userData, TCPIOCompletionUPP ioCompletion,
		Boolean async);
OSErr _TCPRcv(TCPiopb *pb, StreamPtr stream, Ptr rcvBufPtr, unsigned short *rcvBufLen, Ptr userData,
		TCPIOCompletionUPP ioCompletion, Boolean async);
OSErr _TCPClose(TCPiopb *pb, StreamPtr stream, Ptr userData, TCPIOCompletionUPP ioCompletion, Boolean async);
OSErr _TCPAbort(TCPiopb *pb, StreamPtr stream, Ptr userData, TCPIOCompletionUPP ioCompletion, Boolean async);
OSErr _TCPRelease(TCPiopb *pb, StreamPtr stream, Ptr userData, TCPIOCompletionUPP ioCompletion, Boolean async);

enum { kRcvBuffer = 4096, kSendChunk = 1024 };

static Boolean gStarted;
static StreamPtr gStream;
static Ptr gRcvBuf;

OSErr NetOpen(unsigned long ip, unsigned short port, short timeoutSecs)
{
	TCPiopb pb;
	ip_addr localIP;
	tcp_port localPort;
	OSErr err;

	NetClose();
	if (!gStarted) {
		if (_TCPInit() != noErr)
			return netNoNetwork;
		gStarted = true;
	}
	if ((gRcvBuf = NewPtr(kRcvBuffer)) == NULL)
		return memFullErr;
	if ((err = _TCPCreate(&pb, &gStream, gRcvBuf, kRcvBuffer, NULL, NULL, NULL, false)) != noErr) {
		gStream = 0;
		NetClose();
		return err;
	}
	(void) timeoutSecs;             /* talkip's own: 20 s for a lease, 30 s to connect */
	err = _TCPActiveOpen(&pb, gStream, ip, port, &localIP, &localPort, NULL, NULL, false);
	if (err != noErr)
		NetClose();
	return err;
}

OSErr NetSend(const char *data, long len)
{
	TCPiopb pb;
	wdsEntry wds[2];
	OSErr err = noErr;

	while (len > 0 && err == noErr) {
		unsigned short n = (unsigned short) (len > kSendChunk ? kSendChunk : len);
		wds[0].length = n;
		wds[0].ptr = (Ptr) data;
		wds[1].length = 0;
		wds[1].ptr = NULL;
		err = _TCPSend(&pb, gStream, wds, NULL, NULL, false);
		data += n;
		len -= n;
	}
	return err;
}

OSErr NetRecv(char *buf, long cap, long *got, short timeoutSecs)
{
	TCPiopb pb;
	unsigned short n = (unsigned short) (cap > 65535L ? 65535L : cap);
	OSErr err;

	(void) timeoutSecs;             /* talkip's own: 30 s */
	err = _TCPRcv(&pb, gStream, buf, &n, NULL, NULL, false);
	*got = err == noErr ? n : 0;
	return err;
}

void NetClose(void)
{
	TCPiopb pb;

	if (gStream != 0) {
		if (_TCPClose(&pb, gStream, NULL, NULL, false) != noErr)
			(void) _TCPAbort(&pb, gStream, NULL, NULL, false);
		(void) _TCPRelease(&pb, gStream, NULL, NULL, false);
		gStream = 0;
	}
	if (gRcvBuf != NULL) {
		DisposePtr(gRcvBuf);
		gRcvBuf = NULL;
	}
}

void NetIdle(void)
{
	if (gStarted)
		talkip_idle();
}
