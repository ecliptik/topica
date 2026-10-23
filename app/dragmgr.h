/*
 * dragmgr.h: the Drag Manager calls Topica uses (System 7.5 and later),
 * which Retro68's headers lack: _DragDispatch ($ABED) with the routine's
 * selector in D0 (MOVEQ), the selectors as Apple's Drag.h has them
 * (autc04/multiversal pull request 16 lists them all).
 */
#ifndef TOPICA_DRAGMGR_H
#define TOPICA_DRAGMGR_H

#include <Quickdraw.h>
#include <Events.h>
#include <Windows.h>
#include <Multiverse.h>

typedef struct OpaqueDragRef *DragReference;
typedef unsigned long ItemReference;
typedef OSType FlavorType;
typedef unsigned long FlavorFlags;      /* 32 bits, as Drag.h has it */
typedef unsigned short DragTrackingMessage;

enum {
	kDragTrackingEnterHandler = 1,
	kDragTrackingEnterWindow = 2,
	kDragTrackingInWindow = 3,
	kDragTrackingLeaveWindow = 4,
	kDragTrackingLeaveHandler = 5
};

enum { kDragGestalt = 'drag', kDragNotAccepted = -1850 };

/* On a 68K Mac the handlers are plain procedures */
typedef pascal OSErr (*DragTrackingHandlerUPP)(DragTrackingMessage message, WindowPtr w, void *refCon,
		DragReference drag);
typedef pascal OSErr (*DragReceiveHandlerUPP)(WindowPtr w, void *refCon, DragReference drag);

pascal OSErr NewDrag(DragReference *drag) M68K_INLINE(0x7005, 0xABED);
pascal OSErr DisposeDrag(DragReference drag) M68K_INLINE(0x7006, 0xABED);
pascal OSErr AddDragItemFlavor(DragReference drag, ItemReference item, FlavorType type, const void *data, Size size,
		FlavorFlags flags) M68K_INLINE(0x7007, 0xABED);
pascal OSErr SetDragItemBounds(DragReference drag, ItemReference item, const Rect *bounds)
		M68K_INLINE(0x7016, 0xABED);
pascal OSErr TrackDrag(DragReference drag, const EventRecord *event, RgnHandle region) M68K_INLINE(0x700D, 0xABED);
pascal OSErr CountDragItems(DragReference drag, unsigned short *count) M68K_INLINE(0x700E, 0xABED);
pascal OSErr GetDragItemReferenceNumber(DragReference drag, unsigned short index, ItemReference *item)
		M68K_INLINE(0x700F, 0xABED);
pascal OSErr ShowDragHilite(DragReference drag, RgnHandle frame, Boolean inside) M68K_INLINE(0x701E, 0xABED);
pascal OSErr HideDragHilite(DragReference drag) M68K_INLINE(0x701F, 0xABED);
pascal OSErr GetFlavorDataSize(DragReference drag, ItemReference item, FlavorType type, Size *size)
		M68K_INLINE(0x7013, 0xABED);
pascal OSErr GetFlavorData(DragReference drag, ItemReference item, FlavorType type, void *data, Size *size,
		unsigned long offset) M68K_INLINE(0x7014, 0xABED);
pascal OSErr GetDragMouse(DragReference drag, Point *mouse, Point *pinned) M68K_INLINE(0x701A, 0xABED);
pascal OSErr InstallTrackingHandler(DragTrackingHandlerUPP handler, WindowPtr w, void *refCon)
		M68K_INLINE(0x7001, 0xABED);
pascal OSErr InstallReceiveHandler(DragReceiveHandlerUPP handler, WindowPtr w, void *refCon)
		M68K_INLINE(0x7002, 0xABED);
pascal OSErr RemoveTrackingHandler(DragTrackingHandlerUPP handler, WindowPtr w) M68K_INLINE(0x7003, 0xABED);
pascal OSErr RemoveReceiveHandler(DragReceiveHandlerUPP handler, WindowPtr w) M68K_INLINE(0x7004, 0xABED);

#endif
