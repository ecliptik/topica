/* net_mactcp.c: net.h on MacTCP's .IPP driver, with synchronous calls. */
#include <Devices.h>
#include <Memory.h>
#include <string.h>

#include "mactcp.h"
#include "net.h"

enum { kRcvBuffer = 8192, kSendChunk = 4096 };

static short gIPP;              /* the driver's refnum, once opened */
static StreamPtr gStream;
static Ptr gRcvBuf;

static OSErr Call(TCPiopb *pb, short csCode)
{
	pb->ioCRefNum = gIPP;
	pb->csCode = csCode;
	pb->tcpStream = gStream;
	return PBControlSync((ParmBlkPtr) pb);
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
	memset(&pb, 0, sizeof(pb));
	pb.csParam.open.ulpTimeoutValue = (byte) timeoutSecs;
	pb.csParam.open.ulpTimeoutAction = 1;           /* abort */
	pb.csParam.open.validityFlags = timeoutValue | timeoutAction;
	pb.csParam.open.commandTimeoutValue = (byte) timeoutSecs;
	pb.csParam.open.remoteHost = ip;
	pb.csParam.open.remotePort = port;
	if ((err = Call(&pb, TCPActiveOpen)) != noErr)
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
		memset(&pb, 0, sizeof(pb));
		pb.csParam.send.ulpTimeoutValue = 30;
		pb.csParam.send.ulpTimeoutAction = 1;
		pb.csParam.send.validityFlags = timeoutValue | timeoutAction;
		pb.csParam.send.pushFlag = n == len;
		pb.csParam.send.wdsPtr = (Ptr) wds;
		err = Call(&pb, TCPSend);
		data += n;
		len -= n;
	}
	return err;
}

OSErr NetRecv(char *buf, long cap, long *got, short timeoutSecs)
{
	TCPiopb pb;
	OSErr err;

	*got = 0;
	memset(&pb, 0, sizeof(pb));
	pb.csParam.receive.commandTimeoutValue = (byte) timeoutSecs;
	pb.csParam.receive.rcvBuff = buf;
	pb.csParam.receive.rcvBuffLen = (unsigned short) (cap > 65535L ? 65535L : cap);
	err = Call(&pb, TCPRcv);
	if (err == noErr)
		*got = pb.csParam.receive.rcvBuffLen;
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
