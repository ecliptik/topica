/* headings.c: see headings.h. */
#include <Menus.h>
#include <Memory.h>
#include <string.h>

#include "../core/mdscan.h"
#include "headings.h"
#include "util.h"

enum { kMaxHeadings = 40, kItemChars = 40 };

static long gAt[kMaxHeadings];          /* where each item's heading starts */
static short gCount;

void HeadingsMenu(Doc *d)
{
	MenuHandle m = GetMenuHandle(kHeadingsMenu);
	MdHeading *h;
	Editor *ed;
	const char *t;
	long s, e, len;
	int count, i, indent, current = 0;
	char item[kItemChars + 16];
	Str255 p;

	while (CountMItems(m) > 0)
		DeleteMenuItem(m, 1);
	gCount = 0;
	if (d == NULL)
		return;
	h = (MdHeading *) NewPtr(sizeof(MdHeading) * kMaxHeadings);
	if (h == NULL)
		return;
	ed = DocEditor(d);
	EdGetSelection(ed, &s, &e);
	t = EdLockText(ed);
	count = md_headings(t, EdLength(ed), h, kMaxHeadings);
	if (count > kMaxHeadings)
		count = kMaxHeadings;
	for (i = 0; i < count; i++) {
		/* indented by level; a long one cut short with an ellipsis */
		indent = (h[i].level - 1) * 2;
		len = h[i].len;
		memset(item, ' ', (size_t) indent);
		if (len > kItemChars) {
			memcpy(item + indent, t + h[i].start, kItemChars - 1);
			item[indent + kItemChars - 1] = '\xC9';
			len = kItemChars;
		} else {
			memcpy(item + indent, t + h[i].start, (size_t) len);
		}
		item[indent + len] = '\0';
		if (item[0] == '-')
			item[0] = '\xD0';           /* a leading hyphen would make a separator */
		if (item[0] == '\0')
			strcpy(item, "#");
		PStr(p, item);
		AppendMenu(m, "\px");           /* the text after: no metacharacters */
		SetMenuItemText(m, (short) (i + 1), p);
		EnableItem(m, (short) (i + 1));
		gAt[i] = h[i].start;
		if (h[i].line <= s)
			current = i + 1;
	}
	EdUnlockText(ed);
	DisposePtr((Ptr) h);
	gCount = (short) count;
	if (count == 0) {
		AppendMenu(m, "\pNo Headings");
		DisableItem(m, 1);
	} else if (current > 0) {
		CheckItem(m, (short) current, true);
	}
}

void HeadingsGo(Doc *d, short item)
{
	if (d != NULL && item >= 1 && item <= gCount)
		DocShowLine(d, gAt[item - 1]);
}
