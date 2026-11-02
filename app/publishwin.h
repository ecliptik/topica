/*
 * publishwin.h: Post > Publish. Asks where the post goes (its path in the
 * blog, the bridge's address and key), then sends it to the bridge
 * (bridge/topica_bridge.py), which commits it on the blog's drafts branch.
 * The sending goes on in the background while the writer writes.
 */
#ifndef TOPICA_PUBLISHWIN_H
#define TOPICA_PUBLISHWIN_H

#include "docwin.h"

/* Asks where the post goes, then starts publishing it in the background */
void PublishPost(Doc *d);
/* From the event loop: the publishing's next step */
void PublishIdle(void);
Boolean PublishBusy(void);
/* Post > Stop Publishing */
void PublishStop(void);
/* Before quitting: true when nothing's publishing, or the writer says stop it */
Boolean PublishMayQuit(void);
/* What the post (its DocId) is publishing, for its status line; NULL when it isn't */
const char *PublishStatus(long doc);

#endif
