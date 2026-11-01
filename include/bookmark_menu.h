// The bookmark menu (docs: ui.md, decision 045): a box over the bottom screen that
// lists the book's bookmarks. The logic has no DS code and is tested on a PC.
//
// Controls (menu context, decision 033): Up and Down move, Left and Right move a
// page, A jumps to the bookmark, Select deletes it and B closes the menu.
#ifndef DSLEAF_BOOKMARK_MENU_H
#define DSLEAF_BOOKMARK_MENU_H

#include <stdbool.h>
#include <stdint.h>

#include "bookmarks.h"
#include "render.h"

typedef struct {
	int selected;
	int top;       // first bookmark shown
	bool changed;  // the cursor moved, so the box needs drawing again
} BookmarkMenu;

typedef enum {
	BM_EVENT_NONE,
	BM_EVENT_CLOSE,
	BM_EVENT_JUMP,   // go to bookmark `index`
	BM_EVENT_DELETE, // delete bookmark `index`, then call bookmark_menu_clamp
} BmEventKind;

typedef struct {
	BmEventKind kind;
	int index;
} BmEvent;

// Starts with the cursor on the first bookmark at or after the current place
// (or the last one if all are earlier).
void bookmark_menu_open(BookmarkMenu *menu, const BookMarks *marks, int chapter, uint32_t offset, int visible);

// `visible` is how many bookmarks fit in the box.
BmEvent bookmark_menu_update(BookmarkMenu *menu, const BookMarks *marks, uint32_t actions, int visible);

// Keeps the cursor inside the list after a bookmark was deleted.
void bookmark_menu_clamp(BookmarkMenu *menu, const BookMarks *marks, int visible);

// ---- Drawing (bookmark_menu_view.c, needs the DS) ----

// How many bookmarks fit in the box on the current screen grid.
int bookmark_menu_visible(void);

// Draws the box centred on the screen. `first_chapter` is the book's first
// chapter with text, so the chapter numbers match the status row.
void bookmark_menu_draw(const BookmarkMenu *menu, const BookMarks *marks, int first_chapter, Screen screen);

#endif
