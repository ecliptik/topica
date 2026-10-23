/* ae.c: see ae.h. After Shiori Classic's. */
#include <AppleEvents.h>
#include <Gestalt.h>
#include <Traps.h>
#include <Errors.h>

#include "ae.h"

enum { kUserCanceled = -128 };

static Boolean (*gQuit)(void);
static void (*gOpenApp)(void);
static void (*gOpenFile)(const FileRef *f);
static void (*gPrintFile)(const FileRef *f);
static Boolean gHaveAE;

static pascal OSErr OpenApp(const AppleEvent *e, AppleEvent *reply, long refcon)
{
	(void) e; (void) reply; (void) refcon;
	if (gOpenApp != NULL)
		gOpenApp();
	return noErr;
}

/* Calls fn for each file in the event's list */
static OSErr EachFile(const AppleEvent *e, void (*fn)(const FileRef *f))
{
	AEDescList list;
	long count, i;
	OSErr err;

	err = AEGetParamDesc((AERecord *) e, keyDirectObject, typeAEList, &list);
	if (err != noErr)
		return err;
	err = AECountItems(&list, &count);
	for (i = 1; err == noErr && i <= count; i++) {
		FSSpec spec;
		AEKeyword key;
		DescType type;
		Size size;
		FileRef f;
		if (AEGetNthPtr(&list, i, typeFSS, &key, &type, &spec, (Size) sizeof(spec), &size) != noErr)
			continue;
		FileRefFromFSSpec(&spec, &f);
		if (fn != NULL)
			fn(&f);
	}
	(void) AEDisposeDesc(&list);
	return err;
}

static pascal OSErr OpenDocs(const AppleEvent *e, AppleEvent *reply, long refcon)
{
	(void) reply; (void) refcon;
	return EachFile(e, gOpenFile);
}

static pascal OSErr PrintDocs(const AppleEvent *e, AppleEvent *reply, long refcon)
{
	(void) reply; (void) refcon;
	return EachFile(e, gPrintFile);
}

static pascal OSErr Quit(const AppleEvent *e, AppleEvent *reply, long refcon)
{
	(void) e; (void) reply; (void) refcon;
	if (gQuit != NULL && !gQuit())
		return kUserCanceled;           /* the writer chose Cancel: so does Shut Down */
	return noErr;
}

Boolean AppleEventsInit(Boolean (*quit)(void), void (*openApp)(void), void (*openFile)(const FileRef *f),
		void (*printFile)(const FileRef *f))
{
	long response;

	gQuit = quit;
	gOpenApp = openApp;
	gOpenFile = openFile;
	gPrintFile = printFile;
	if (NGetTrapAddress(_Gestalt, kToolboxTrapType) == NGetTrapAddress(_Unimplemented, kToolboxTrapType)
			|| Gestalt(gestaltAppleEventsAttr, &response) != noErr || !(response & 1))
		return false;
	gHaveAE = true;
	AEInstallEventHandler(kCoreEventClass, kAEOpenApplication, NewAEEventHandlerUPP(OpenApp), 0, false);
	AEInstallEventHandler(kCoreEventClass, kAEOpenDocuments, NewAEEventHandlerUPP(OpenDocs), 0, false);
	AEInstallEventHandler(kCoreEventClass, kAEPrintDocuments, NewAEEventHandlerUPP(PrintDocs), 0, false);
	AEInstallEventHandler(kCoreEventClass, kAEQuitApplication, NewAEEventHandlerUPP(Quit), 0, false);
	return true;
}

void AppleEventsHandle(EventRecord *e)
{
	if (gHaveAE)
		(void) AEProcessAppleEvent(e);
}
