/*
 * mactcp.h: the parts of MacTCP's .IPP driver interface Topica uses (TCP
 * create, open, send, receive, close, abort, release), written from the
 * MacTCP Programmer's Guide (Apple, 1989) so that no Apple header is
 * copied here. Names follow the guide's, so code reads like its examples.
 *
 * Layouts are the 68000's (two-byte alignment), as the driver expects:
 * tests/check-mactcp-layout.sh compares every offset with Apple's header
 * when one is at hand.
 */
#ifndef TOPICA_MACTCP_H
#define TOPICA_MACTCP_H

#include <MacTypes.h>

typedef unsigned long ip_addr;
typedef unsigned short tcp_port;
typedef unsigned long StreamPtr;
typedef unsigned char byte;

enum {
	TCPCreate = 30, TCPActiveOpen = 32, TCPSend = 34, TCPRcv = 37,
	TCPClose = 38, TCPAbort = 39, TCPRelease = 42
};

/* validityFlags */
enum { timeoutValue = 0x80, timeoutAction = 0x40 };

/* results */
enum {
	ipBadLapErr = -23000, ipBadCnfgErr = -23001, ipNoCnfgErr = -23002,
	ipLoadErr = -23003, ipBadAddr = -23004,
	connectionClosing = -23005, invalidLength = -23006,
	connectionExists = -23007, connectionDoesntExist = -23008,
	insufficientResources = -23009, invalidStreamPtr = -23010,
	streamAlreadyOpen = -23011, connectionTerminated = -23012,
	invalidBufPtr = -23013, invalidRDS = -23014, invalidWDS = -23014,
	openFailed = -23015, commandTimeout = -23016, duplicateSocket = -23017
};

typedef struct wdsEntry {
	unsigned short length;
	Ptr ptr;
} wdsEntry;

typedef struct TCPCreatePB {
	Ptr rcvBuff;
	unsigned long rcvBuffLen;
	ProcPtr notifyProc;
	Ptr userDataPtr;
} TCPCreatePB;

typedef struct TCPOpenPB {
	byte ulpTimeoutValue;
	byte ulpTimeoutAction;
	byte validityFlags;
	byte commandTimeoutValue;
	ip_addr remoteHost;
	tcp_port remotePort;
	ip_addr localHost;
	tcp_port localPort;
	byte tosFlags;
	byte precedence;
	Boolean dontFrag;
	byte timeToLive;
	byte security;
	byte optionCnt;
	byte options[40];
	Ptr userDataPtr;
} TCPOpenPB;

typedef struct TCPSendPB {
	byte ulpTimeoutValue;
	byte ulpTimeoutAction;
	byte validityFlags;
	Boolean pushFlag;
	Boolean urgentFlag;
	Ptr wdsPtr;
	unsigned long sendFree;
	unsigned short sendLength;
	Ptr userDataPtr;
} TCPSendPB;

typedef struct TCPReceivePB {
	byte commandTimeoutValue;
	byte filler;
	Boolean markFlag;
	Boolean urgentFlag;
	Ptr rcvBuff;
	unsigned short rcvBuffLen;
	Ptr rdsPtr;
	unsigned short rdsLength;
	unsigned short secondTimeStamp;
	Ptr userDataPtr;
} TCPReceivePB;

typedef struct TCPClosePB {
	byte ulpTimeoutValue;
	byte ulpTimeoutAction;
	byte validityFlags;
	Ptr userDataPtr;
} TCPClosePB;

typedef struct TCPiopb {
	char fill12[12];
	ProcPtr ioCompletion;
	short ioResult;
	Ptr ioNamePtr;
	short ioVRefNum;
	short ioCRefNum;
	short csCode;
	StreamPtr tcpStream;
	union {
		TCPCreatePB create;         /* also TCPRelease's */
		TCPOpenPB open;
		TCPSendPB send;
		TCPReceivePB receive;
		TCPClosePB close;
	} csParam;
} TCPiopb;

#endif
