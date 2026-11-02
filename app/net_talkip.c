/*
 * net_talkip.c: net.h on talkip (TCP/IP over LocalTalk, linked into the
 * application), through its MacTCP-style calls. talkip starts with the
 * first connection: it opens AppleTalk socket 72 and leases an address
 * from the MacIP gateway; from then on NetIdle gives it the time it needs.
 * The connection opens asynchronously, a send is copied at once, and a
 * receive waits for TCPStatus to say there's something to read; NetPoll
 * asks after each. Its own ExitToShell patch closes the socket when Topica
 * quits.
 */
#include <Memory.h>
#include <OSUtils.h>
#include <string.h>

#include <MacTCP.h>
#include <talkip.h>
#include "dns.h"                        /* talkip's resolver (its src/, for this file only) */

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
OSErr _TCPStatus(TCPiopb *pb, StreamPtr stream, struct TCPStatusPB *status, Ptr userData,
		TCPIOCompletionUPP ioCompletion, Boolean async);
OSErr _TCPClose(TCPiopb *pb, StreamPtr stream, Ptr userData, TCPIOCompletionUPP ioCompletion, Boolean async);
OSErr _TCPAbort(TCPiopb *pb, StreamPtr stream, Ptr userData, TCPIOCompletionUPP ioCompletion, Boolean async);
OSErr _TCPRelease(TCPiopb *pb, StreamPtr stream, Ptr userData, TCPIOCompletionUPP ioCompletion, Boolean async);

enum { kRcvBuffer = 4096, kSendChunk = 1024 };

enum { kIdle, kResolving, kOpening, kReceiving };

static Boolean gStarted;
static StreamPtr gStream;
static Ptr gRcvBuf;
static int gOp;
static OSErr gResult;
static Boolean gHaveResult;
static unsigned long gAddress, gUntil;
static long gGot, gCap;
static char *gBuf;
static TCPiopb gOpenPB;                 /* not on the stack: the open runs on */

static OSErr Finished(OSErr err)
{
	gResult = err;
	gHaveResult = true;
	gOp = kIdle;
	return noErr;
}

static OSErr Started(void)
{
	if (!gStarted) {
		if (_TCPInit() != noErr)
			return netNoNetwork;
		gStarted = true;
	}
	return noErr;
}

OSErr NetOpenStart(unsigned long ip, unsigned short port, short timeoutSecs)
{
	TCPiopb pb;
	ip_addr localIP;
	tcp_port localPort;
	OSErr err;

	NetClose();
	gHaveResult = false;
	if ((err = Started()) != noErr)
		return err;
	if ((gRcvBuf = NewPtr(kRcvBuffer)) == NULL)
		return memFullErr;
	if ((err = _TCPCreate(&pb, &gStream, gRcvBuf, kRcvBuffer, NULL, NULL, NULL, false)) != noErr) {
		gStream = 0;
		NetClose();
		return err;
	}
	(void) timeoutSecs;             /* talkip's own: 20 s for a lease, 30 s to connect */
	err = _TCPActiveOpen(&gOpenPB, gStream, ip, port, &localIP, &localPort, NULL, NULL, true);
	if (err != noErr) {
		NetClose();
		return talkip_local_ip() == 0 ? netNoAddress : err;
	}
	gOp = kOpening;
	return noErr;
}

OSErr NetSendStart(const char *data, long len)
{
	TCPiopb pb;
	wdsEntry wds[2];
	OSErr err = noErr;

	/* talkip copies what it's given: done at once */
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
	return Finished(err);
}

OSErr NetRecvStart(char *buf, long cap, short timeoutSecs)
{
	gBuf = buf;
	gCap = cap > 65535L ? 65535L : cap;
	gGot = 0;
	gUntil = TickCount() + timeoutSecs * 60L;
	gHaveResult = false;
	gOp = kReceiving;
	return noErr;
}

OSErr NetPoll(void)
{
	TCPiopb pb;
	DNSResult r;
	unsigned short n;
	OSErr err;

	if (gHaveResult) {
		gHaveResult = false;
		return gResult;
	}
	switch (gOp) {
	case kResolving:
		if (dns_poll(&r) == DNS_PENDING)
			return netBusy;
		gOp = kIdle;
		err = r.err;
		if (err == noErr && r.naddrs == 0)
			err = DNS_ERR_NODATA;
		switch (err) {
		case noErr:
			gAddress = r.addr[0];
			return noErr;
		case DNS_ERR_NXDOMAIN:
		case DNS_ERR_NODATA:
		case DNS_ERR_FORMAT:
			return netNoName;
		case DNS_ERR_NOSERVER:
			return netNoNameServer;
		case DNS_ERR_TIMEOUT:
		case DNS_ERR_SERVFAIL:
			return netNoAnswer;
		}
		return err;
	case kOpening:
		if (gOpenPB.ioResult > 0)
			return netBusy;
		gOp = kIdle;
		err = gOpenPB.ioResult;
		if (err != noErr) {
			NetClose();
			if (talkip_local_ip() == 0)
				err = netNoAddress;     /* the gateway never leased one */
		}
		return err;
	case kReceiving:
		/* something to read, or the other end has finished: then TCPRcv doesn't wait */
		if (_TCPStatus(&pb, gStream, NULL, NULL, NULL, false) == noErr && pb.csParam.status.amtUnreadData == 0
				&& pb.csParam.status.connectionState <= 8) {    /* not past established */
			if (TickCount() <= gUntil)
				return netBusy;
			gOp = kIdle;
			return commandTimeout;
		}
		gOp = kIdle;
		n = (unsigned short) gCap;
		err = _TCPRcv(&pb, gStream, gBuf, &n, NULL, NULL, false);
		gGot = err == noErr ? n : 0;
		return err;
	}
	return noErr;
}

unsigned long NetAddress(void)
{
	return gAddress;
}

long NetGot(void)
{
	return gGot;
}

void NetCancel(void)
{
	TCPiopb abort;

	if (gOp == kResolving)
		dns_cancel();
	if (gOp == kOpening && gStream != 0)
		(void) _TCPAbort(&abort, gStream, NULL, NULL, false);   /* ends the open */
	gOp = kIdle;
	gHaveResult = false;
	NetClose();
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

/* talkip's resolver asks the name server the MacIP gateway handed out */
OSErr NetResolveStart(const char *name)
{
	OSErr err;

	gHaveResult = false;
	if ((err = Started()) != noErr)
		return err;
	if (talkip_local_ip() == 0)
		return netNoAddress;
	if ((err = dns_begin(name)) != noErr)
		return Finished(err == DNS_ERR_NOSERVER ? netNoNameServer : err == DNS_ERR_FORMAT ? netNoName : err);
	gOp = kResolving;
	return noErr;
}

void NetShutdown(void)
{
	NetCancel();                        /* talkip's ExitToShell patch does the rest */
}
