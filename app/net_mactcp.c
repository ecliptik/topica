/* net_mactcp.c: net.h on MacTCP's .IPP driver. Opening, sending and
   receiving are asynchronous calls waited on here, asking the cancel
   routine as they wait; the rest are synchronous. */
#include <Devices.h>
#include <Files.h>
#include <Gestalt.h>
#include <Memory.h>
#include <OSUtils.h>
#include <Resources.h>
#include <string.h>

#include "mactcp.h"
#include "net.h"
#include "util.h"

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
	unsigned long start;

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
	start = TickCount();
	if ((err = CallWait(&gOpenPB, TCPActiveOpen)) != noErr) {
		NetClose();
		/* MacTCP says openFailed for a refusal and for its timeout alike
		   (seen on a Plus): one that took the whole timeout was nothing
		   answering */
		if (err == openFailed && TickCount() - start >= (unsigned long) (timeoutSecs - 2) * 60)
			err = commandTimeout;
	}
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

/* ---- names: MacTCP's resolver ----
   The 'dnrp' code MacTCP keeps in its control panel (creator ztcp, or mtcp
   for 1.0) or in "MacTCP DNR", loaded and called as Apple's dnr.c does:
   C calls whose first argument says which routine. */

#ifndef kOnSystemDisk
#define kOnSystemDisk ((short) 0x8000)  /* Folders.h's, which Retro68 lacks */
#endif

enum { kOpenResolver = 1, kCloseResolver = 2, kStrToAddr = 3 };
enum { kCacheFault = -23042, kNameSyntax = -23041, kNoNameServer = -23044, kAuthName = -23045, kNoAnswer = -23046 };

typedef struct {
	long rtnCode;
	char cname[255];
	unsigned long addr[4];
} HostInfo;

typedef pascal void (*ResultProc)(HostInfo *info, char *user);
typedef OSErr (*OpenFn)(long selector, char *fileName);
typedef OSErr (*CloseFn)(long selector);
typedef OSErr (*StrToAddrFn)(long selector, char *host, HostInfo *info, ResultProc done, char *user);

static Handle gDnr;
/* The answer: the resolver writes it when it's done, maybe after a cancel */
static HostInfo gHost;
static volatile Boolean gHostDone;

static pascal void HostDone(HostInfo *info, char *user)
{
	(void) info;
	(void) user;
	gHostDone = true;
}

/* A file of creator in folder (vRefNum, dirID) with a 'dnrp' resource:
   its resource file, open; -1 when there's none */
static short DnrFile(short vRefNum, long dirID)
{
	HParamBlockRec pb;
	Str63 name;
	short i, ref;
	OSType creator;

	for (i = 1; i < 500; i++) {
		memset(&pb, 0, sizeof(pb));
		pb.fileParam.ioNamePtr = name;
		pb.fileParam.ioVRefNum = vRefNum;
		pb.fileParam.ioDirID = dirID;
		pb.fileParam.ioFDirIndex = i;
		if (PBHGetFInfoSync(&pb) != noErr)
			break;
		creator = pb.fileParam.ioFlFndrInfo.fdCreator;
		if ((creator == 'ztcp' || creator == 'mtcp')
				&& (pb.fileParam.ioFlFndrInfo.fdType == 'cdev' || pb.fileParam.ioFlFndrInfo.fdType == 'dnrp')) {
			ref = HOpenResFile(vRefNum, dirID, name, fsRdPerm);
			if (ref != -1) {
				if (Count1Resources('dnrp') > 0)
					return ref;
				CloseResFile(ref);
			}
		}
	}
	return -1;
}

static OSErr OpenResolver(void)
{
	SysEnvRec env;
	short ref = -1, cur = CurResFile(), v;
	long attr, dir;
	Handle h;
	OSErr err;

	if (gDnr != NULL)
		return noErr;
	if (Gestalt(gestaltFindFolderAttr, &attr) == noErr && (attr & 1)
			&& FindFolder(kOnSystemDisk, kControlPanelFolderType, false, &v, &dir) == noErr)
		ref = DnrFile(v, dir);
	if (ref == -1 && SysEnvirons(1, &env) == noErr)
		ref = DnrFile(env.sysVRefNum, 0);
	if (ref == -1)
		return netNoNameServer;         /* no MacTCP to ask */
	UseResFile(ref);
	h = Get1IndResource('dnrp', 1);
	if (h != NULL) {
		DetachResource(h);
		MoveHHi(h);
		HLock(h);
	}
	CloseResFile(ref);
	UseResFile(cur);
	if (h == NULL)
		return netNoNameServer;
	err = ((OpenFn) *h)(kOpenResolver, NULL);
	if (err != noErr) {
		DisposeHandle(h);
		return err;
	}
	gDnr = h;
	return noErr;
}

OSErr NetResolve(const char *name, unsigned long *ip)
{
	char host[256];
	OSErr err;

	if (gIPP == 0) {
		ParamBlockRec open;
		memset(&open, 0, sizeof(open));
		open.ioParam.ioNamePtr = (StringPtr) "\p.IPP";
		if (PBOpenSync(&open) != noErr)
			return netNoNetwork;
		gIPP = open.ioParam.ioRefNum;
	}
	if ((err = OpenResolver()) != noErr)
		return err;
	strncpy(host, name, sizeof(host) - 1);
	host[sizeof(host) - 1] = '\0';
	memset(&gHost, 0, sizeof(gHost));
	gHostDone = false;
	err = ((StrToAddrFn) *gDnr)(kStrToAddr, host, &gHost, HostDone, NULL);
	if (err == kCacheFault) {
		/* asked the name server: wait for the answer */
		while (!gHostDone) {
			SystemTask();
			if (gCancel != NULL && gCancel())
				return netCanceled;     /* the answer lands in gHost later, harmlessly */
		}
		err = (OSErr) gHost.rtnCode;
		DLOG(("NetResolve: %s: %d, %lx", host, err, gHost.addr[0]));
	}
	switch (err) {
	case noErr:
		*ip = gHost.addr[0];
		return *ip != 0 ? noErr : netNoName;
	case kCacheFault:                   /* still, after the answer: MacTCP 2.0 found no such name */
	case kNameSyntax:
	case kAuthName:
		return netNoName;
	case kNoNameServer:
		return netNoNameServer;
	case kNoAnswer:
		return netNoAnswer;
	}
	return err;
}

void NetShutdown(void)
{
	NetClose();
	if (gDnr != NULL) {
		(void) ((CloseFn) *gDnr)(kCloseResolver);
		DisposeHandle(gDnr);
		gDnr = NULL;
	}
}
