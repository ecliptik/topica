/*
 * publishwin.h: Post > Publish. Asks where the post goes (its path in the
 * blog, the bridge's address and key), then sends it to the bridge
 * (bridge/topica_bridge.py), which commits it on the blog's drafts branch.
 */
#ifndef TOPICA_PUBLISHWIN_H
#define TOPICA_PUBLISHWIN_H

#include "docwin.h"

void PublishPost(Doc *d);

#endif
