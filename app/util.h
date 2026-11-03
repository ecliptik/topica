/*
 * util.h: small Toolbox helpers shared by the windows and dialogs.
 */
#ifndef TOPICA_UTIL_H
#define TOPICA_UTIL_H

#include <Dialogs.h>
#include <Files.h>
#include <OSUtils.h>

/* Toolbox constants Retro68's interfaces leave out */
enum {
	kGeneva = 3,
	kMonaco = 4,
	kZoomDocProc = 8,           /* documentProc with a zoom box */
	kOK = 1,                    /* a dialog's OK and Cancel items */
	kCancel = 2
};

/* Resource IDs */
enum {
	kAboutAlert = 128,
	kSaveChangesAlert = 129,    /* Save, Cancel, Don't Save */
	kErrorAlert = 130,          /* OK */
	kRevertAlert = 131,         /* Revert, Cancel */
	kPublishingAlert = 132,     /* Quit, Cancel */
	kPostInfoDialog = 140,
	kImageDialog = 141,
	kFindDialog = 142,
	kPrefsDialog = 143,
	kLinkDialog = 144,
	kSpellDialog = 145,
	kPublishDialog = 146,
	kPrintingDialog = 148
};

/* C string to Pascal string (cut at 255) and back */
void PStr(Str255 out, const char *s);
void CStr(char *out, const unsigned char *p);

/* System facts, read once */
Boolean HasWaitNextEvent(void);
/* The Notification Manager (System 6 and later) */
Boolean HasNotifications(void);
/* MultiFinder has another application in front (main.c's) */
Boolean AppInBackground(void);
Boolean HasColorQD(void);
Boolean ColorScreen(void);          /* the main screen shows 16 colors or more */
Boolean HasGWorlds(void);           /* 32-bit QuickDraw's offscreen worlds */
Boolean IsSystem7(void);

/* A modal dialog from a DLOG: a plain box on System 6, movable on 7 */
DialogPtr GetModalDialog(short id);
/* The next dialog or alert has no balloons (System 7): before alerts, which
   would otherwise take the last dialog's by item number */
void NoDialogHelp(void);
/* Centres a window on the main screen, a third of the way down (the HIG's spot) */
void CenterWindow(WindowPtr w);
/* On System 7, hides the dialog's heading (the title bar says it) and
   moves the items up into its room; on System 6 the heading stays */
void DropHeading(DialogPtr d, short heading);
/* Item outline: a user item drawn as the default button's ring */
void OutlineDefault(DialogPtr d, short item);
/* The filter every dialog uses: Return and Enter press OK, Escape and
   Command-period press Cancel, Command-X/C/V edit the field, and windows
   behind are redrawn */
pascal Boolean StandardFilter(DialogPtr d, EventRecord *e, short *item);
/* Flashes a button as if clicked */
void FlashButton(DialogPtr d, short item);
/* A dialog item's text, and setting it */
void GetItemText(DialogPtr d, short item, char *out, short cap);
void SetItemText(DialogPtr d, short item, const char *s);

/* The clock's date, a 1920s or 1930s year read as the 2020s or 2030s
   (System 6 sets years in two digits); 0 when it's plainly wrong (a Mac
   without its PRAM battery thinks it's 1904) */
Boolean ClockDate(DateTimeRec *dt);
/* Today as "2026-10-23", or "" when the clock is plainly wrong */
void Today(char out[16]);

/* An alert that says what went wrong: what we were doing, then the reason */
void ErrorAlert(const char *what, OSErr err);
/* A note alert with a message */
void ShowNote(const char *msg);

/* --debug builds: appends a line to "Topica Log" beside the application */
#ifdef TOPICA_DEBUG
void DebugLog(const char *fmt, ...);
#define DLOG(args) DebugLog args
#else
#define DLOG(args) ((void) 0)
#endif

/* Redraws a window when an update arrives during a modal dialog
   (main.c's handler) */
extern void (*gUpdateHook)(WindowPtr w);

#endif
