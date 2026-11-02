/* net_mactcp.c: net.h on MacTCP's .IPP driver. Opening, sending and
   receiving are asynchronous calls that NetPoll asks after; names go to
   MacTCP's resolver, whose answer comes to a completion routine. */
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

/* What's going on */
enum { kIdle, kResolving, kOpening, kSending, kReceiving };

static short gIPP;              /* the driver's refnum, once opened */
static StreamPtr gStream;
static Ptr gRcvBuf;
static int gOp;
static OSErr gResult;           /* an operation that finished as it started */
static Boolean gHaveResult;
static unsigned long gAddress, gOpenStart;
static short gOpenTimeout;
static long gGot;
static const char *gSendData;
static long gSendLeft;
/* The asynchronous calls' parameter blocks, and the send's buffer list: not
   on the stack, since MacTCP may still write to a call it has aborted */
static TCPiopb gOpenPB, gSendPB, gRcvPB;
static wdsEntry gWds[2];

static OSErr Call(TCPiopb *pb, short csCode)
{
	pb->ioCRefNum = gIPP;
	pb->csCode = csCode;
	pb->tcpStream = gStream;
	return PBControlSync((ParmBlkPtr) pb);
}

/* A call that may take a while: started here, asked after by NetPoll */
static void Start(TCPiopb *pb, short csCode)
{
	pb->ioCRefNum = gIPP;
	pb->csCode = csCode;
	pb->tcpStream = gStream;
	pb->ioCompletion = NULL;
	(void) PBControlAsync((ParmBlkPtr) pb);
}

static OSErr Driver(void)
{
	ParamBlockRec open;

	if (gIPP != 0)
		return noErr;
	memset(&open, 0, sizeof(open));
	open.ioParam.ioNamePtr = (StringPtr) "\p.IPP";
	if (PBOpenSync(&open) != noErr)
		return netNoNetwork;
	gIPP = open.ioParam.ioRefNum;
	return noErr;
}

/* An operation done already: NetPoll gives its result next */
static OSErr Finished(OSErr err)
{
	gResult = err;
	gHaveResult = true;
	gOp = kIdle;
	return noErr;
}

OSErr NetOpenStart(unsigned long ip, unsigned short port, short timeoutSecs)
{
	TCPiopb pb;
	OSErr err;

	NetClose();
	if ((err = Driver()) != noErr)
		return err;
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
	gOpenStart = TickCount();
	gOpenTimeout = timeoutSecs;
	gOp = kOpening;
	gHaveResult = false;
	Start(&gOpenPB, TCPActiveOpen);
	return noErr;
}

static void SendChunk(void)
{
	unsigned short n = (unsigned short) (gSendLeft > kSendChunk ? kSendChunk : gSendLeft);

	gWds[0].length = n;
	gWds[0].ptr = (Ptr) gSendData;
	gWds[1].length = 0;
	gWds[1].ptr = NULL;
	memset(&gSendPB, 0, sizeof(gSendPB));
	gSendPB.csParam.send.ulpTimeoutValue = 30;
	gSendPB.csParam.send.ulpTimeoutAction = 1;
	gSendPB.csParam.send.validityFlags = timeoutValue | timeoutAction;
	gSendPB.csParam.send.pushFlag = n == gSendLeft;
	gSendPB.csParam.send.wdsPtr = (Ptr) gWds;
	gSendData += n;
	gSendLeft -= n;
	Start(&gSendPB, TCPSend);
}

OSErr NetSendStart(const char *data, long len)
{
	gSendData = data;
	gSendLeft = len;
	gHaveResult = false;
	if (len <= 0)
		return Finished(noErr);
	gOp = kSending;
	SendChunk();
	return noErr;
}

OSErr NetRecvStart(char *buf, long cap, short timeoutSecs)
{
	gGot = 0;
	gHaveResult = false;
	memset(&gRcvPB, 0, sizeof(gRcvPB));
	gRcvPB.csParam.receive.commandTimeoutValue = (byte) timeoutSecs;
	gRcvPB.csParam.receive.rcvBuff = buf;
	gRcvPB.csParam.receive.rcvBuffLen = (unsigned short) (cap > 65535L ? 65535L : cap);
	gOp = kReceiving;
	Start(&gRcvPB, TCPRcv);
	return noErr;
}

static OSErr Resolved(void);

OSErr NetPoll(void)
{
	OSErr err;

	if (gHaveResult) {
		gHaveResult = false;
		return gResult;
	}
	switch (gOp) {
	case kResolving:
		return Resolved();
	case kOpening:
		if (gOpenPB.ioResult > 0)
			return netBusy;
		gOp = kIdle;
		err = gOpenPB.ioResult;
		if (err != noErr) {
			NetClose();
			/* MacTCP says openFailed for a refusal and for its timeout alike
			   (seen on a Plus): one that took the whole timeout was nothing
			   answering */
			if (err == openFailed && TickCount() - gOpenStart >= (unsigned long) (gOpenTimeout - 2) * 60)
				err = commandTimeout;
		}
		return err;
	case kSending:
		if (gSendPB.ioResult > 0)
			return netBusy;
		if (gSendPB.ioResult == noErr && gSendLeft > 0) {
			SendChunk();
			return netBusy;
		}
		gOp = kIdle;
		return gSendPB.ioResult;
	case kReceiving:
		if (gRcvPB.ioResult > 0)
			return netBusy;
		gOp = kIdle;
		if (gRcvPB.ioResult == noErr)
			gGot = gRcvPB.csParam.receive.rcvBuffLen;
		return gRcvPB.ioResult;
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
	TCPiopb pb;

	if (gOp == kOpening || gOp == kSending || gOp == kReceiving) {
		/* aborting the connection ends the call */
		memset(&pb, 0, sizeof(pb));
		(void) Call(&pb, TCPAbort);
	}
	gOp = kIdle;                    /* a lookup's answer lands later, harmlessly */
	gHaveResult = false;
	NetClose();
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
/* Two answers, used in turn: one a cancel left behind can still land in
   the other without touching the lookup that's going on */
static HostInfo gHosts[2];
static volatile Boolean gHostDone[2];
static short gHost;

static pascal void HostDone(HostInfo *info, char *user)
{
	(void) user;
	gHostDone[info == &gHosts[1]] = true;
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

/* The answer, as net.h's results */
static OSErr Answer(OSErr err, const HostInfo *h)
{
	switch (err) {
	case noErr:
		gAddress = h->addr[0];
		return gAddress != 0 ? noErr : netNoName;
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

OSErr NetResolveStart(const char *name)
{
	char host[256];
	OSErr err;

	gHaveResult = false;
	if ((err = Driver()) != noErr || (err = OpenResolver()) != noErr)
		return err;
	if (gOp == kResolving || !gHostDone[gHost])
		gHost ^= 1;                     /* the last lookup may still answer into the other */
	strncpy(host, name, sizeof(host) - 1);
	host[sizeof(host) - 1] = '\0';
	memset(&gHosts[gHost], 0, sizeof(HostInfo));
	gHostDone[gHost] = false;
	err = ((StrToAddrFn) *gDnr)(kStrToAddr, host, &gHosts[gHost], HostDone, NULL);
	if (err != kCacheFault) {
		gHostDone[gHost] = true;
		return Finished(Answer(err, &gHosts[gHost]));
	}
	gOp = kResolving;                   /* asked the name server: HostDone says when */
	return noErr;
}

static OSErr Resolved(void)
{
	if (!gHostDone[gHost])
		return netBusy;
	gOp = kIdle;
	DLOG(("NetResolve: %d, %lx", (short) gHosts[gHost].rtnCode, gHosts[gHost].addr[0]));
	return Answer((OSErr) gHosts[gHost].rtnCode, &gHosts[gHost]);
}

void NetShutdown(void)
{
	NetCancel();
	if (gDnr != NULL) {
		(void) ((CloseFn) *gDnr)(kCloseResolver);
		DisposeHandle(gDnr);
		gDnr = NULL;
	}
}
