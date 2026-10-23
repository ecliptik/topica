/* net_mactcp.c: net.h on MacTCP's .IPP driver. Opening, sending and
   receiving are asynchronous calls waited on here, asking the cancel
   routine as they wait; the rest are synchronous. */
#include <Devices.h>
#include <Memory.h>
#include <OSUtils.h>
#include <string.h>

#include "mactcp.h"
#include "net.h"

enum { kRcvBuffer = 8192, kSendChunk = 4096 };

static short gIPP;              /* the driver's refnum, once opened */
static StreamPtr gStream;
static Ptr gRcvBuf;
static Boolean (*gCancel)(void);
/* The asynchronous calls' parameter blocks, and the send's buffer list: not
   on the stack, since MacTCP may still write to a call it has aborted */
static TCPiopb gOpenPB, gSendPB, gRcvPB;
static wdsEntry gWds[2];

void NetSetCancel(Boolean (*cancel)(void))
{
	gCancel = cancel;
}

static OSErr Call(TCPiopb *pb, short csCode)
{
	pb->ioCRefNum = gIPP;
	pb->csCode = csCode;
	pb->tcpStream = gStream;
	return PBControlSync((ParmBlkPtr) pb);
}

/* A call that may take a while: asynchronous, waited on here. A cancel
   aborts the connection, which ends the call. */
static OSErr CallWait(TCPiopb *pb, short csCode)
{
	Boolean canceled = false;

	pb->ioCRefNum = gIPP;
	pb->csCode = csCode;
	pb->tcpStream = gStream;
	pb->ioCompletion = NULL;
	(void) PBControlAsync((ParmBlkPtr) pb);
	while (pb->ioResult > 0) {
		SystemTask();
		if (!canceled && gCancel != NULL && gCancel()) {
			TCPiopb abort;
			memset(&abort, 0, sizeof(abort));
			(void) Call(&abort, TCPAbort);
			canceled = true;
		}
	}
	return canceled ? netCanceled : pb->ioResult;
}

OSErr NetOpen(unsigned long ip, unsigned short port, short timeoutSecs)
{
	TCPiopb pb;
	OSErr err;

	NetClose();
	if (gIPP == 0) {
		ParamBlockRec open;
		memset(&open, 0, sizeof(open));
		open.ioParam.ioNamePtr = (StringPtr) "\p.IPP";
		if (PBOpenSync(&open) != noErr)
			return netNoNetwork;
		gIPP = open.ioParam.ioRefNum;
	}
	if ((gRcvBuf = NewPtr(kRcvBuffer)) == NULL)
		return memFullErr;
	memset(&pb, 0, sizeof(pb));
	pb.csParam.create.rcvBuff = gRcvBuf;
	pb.csParam.create.rcvBuffLen = kRcvBuffer;
	if ((err = Call(&pb, TCPCreate)) != noErr) {
		DisposePtr(gRcvBuf);
		gRcvBuf = NULL;
		return err;
	}
	gStream = pb.tcpStream;
	memset(&gOpenPB, 0, sizeof(gOpenPB));
	gOpenPB.csParam.open.ulpTimeoutValue = (byte) timeoutSecs;
	gOpenPB.csParam.open.ulpTimeoutAction = 1;       /* abort */
	gOpenPB.csParam.open.validityFlags = timeoutValue | timeoutAction;
	gOpenPB.csParam.open.commandTimeoutValue = (byte) timeoutSecs;
	gOpenPB.csParam.open.remoteHost = ip;
	gOpenPB.csParam.open.remotePort = port;
	if ((err = CallWait(&gOpenPB, TCPActiveOpen)) != noErr)
		NetClose();
	return err;
}

OSErr NetSend(const char *data, long len)
{
	OSErr err = noErr;

	while (len > 0 && err == noErr) {
		unsigned short n = (unsigned short) (len > kSendChunk ? kSendChunk : len);
		gWds[0].length = n;
		gWds[0].ptr = (Ptr) data;
		gWds[1].length = 0;
		gWds[1].ptr = NULL;
		memset(&gSendPB, 0, sizeof(gSendPB));
		gSendPB.csParam.send.ulpTimeoutValue = 30;
		gSendPB.csParam.send.ulpTimeoutAction = 1;
		gSendPB.csParam.send.validityFlags = timeoutValue | timeoutAction;
		gSendPB.csParam.send.pushFlag = n == len;
		gSendPB.csParam.send.wdsPtr = (Ptr) gWds;
		err = CallWait(&gSendPB, TCPSend);
		data += n;
		len -= n;
	}
	return err;
}

OSErr NetRecv(char *buf, long cap, long *got, short timeoutSecs)
{
	OSErr err;

	*got = 0;
	memset(&gRcvPB, 0, sizeof(gRcvPB));
	gRcvPB.csParam.receive.commandTimeoutValue = (byte) timeoutSecs;
	gRcvPB.csParam.receive.rcvBuff = buf;
	gRcvPB.csParam.receive.rcvBuffLen = (unsigned short) (cap > 65535L ? 65535L : cap);
	err = CallWait(&gRcvPB, TCPRcv);
	if (err == noErr)
		*got = gRcvPB.csParam.receive.rcvBuffLen;
	return err;
}

void NetClose(void)
{
	TCPiopb pb;

	if (gStream != 0) {
		memset(&pb, 0, sizeof(pb));
		pb.csParam.close.ulpTimeoutValue = 10;
		pb.csParam.close.ulpTimeoutAction = 1;
		pb.csParam.close.validityFlags = timeoutValue | timeoutAction;
		if (Call(&pb, TCPClose) != noErr) {
			memset(&pb, 0, sizeof(pb));
			(void) Call(&pb, TCPAbort);
		}
		memset(&pb, 0, sizeof(pb));
		(void) Call(&pb, TCPRelease);
		gStream = 0;
	}
	if (gRcvBuf != NULL) {
		DisposePtr(gRcvBuf);
		gRcvBuf = NULL;
	}
}

void NetIdle(void)
{
}
