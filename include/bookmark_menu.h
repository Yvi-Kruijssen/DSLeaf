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
#include "page_menu.h"
#include "render.h"
#include "toc.h"

// The cursor of a list in a menu box: which entry is selected and which is the
// first one shown. Bookmarks and the table of contents both use it.
typedef struct {
	int selected;
	int top;       // first entry shown
	bool changed;  // the cursor moved, so the box needs drawing again
} ListMenu;

typedef ListMenu BookmarkMenu;

// The tabs of the box, switched with L and R (decision 048). The Page tab only exists when
// the page counts are complete (decision 057).
typedef enum { TAB_CONTENTS, TAB_BOOKMARKS, TAB_PAGE } ReadingTab;

// The tab before or after `tab` (`step` is 1 or -1), skipping the Page tab when `with_page` is false.
ReadingTab reading_tab_step(ReadingTab tab, int step, bool with_page);

// The box is narrower when the page is rotated (decision 049). These say what
// fits in a text area `width` characters wide: whether both tab names fit in the
// title row (otherwise only the one in use is shown, as "<Contents>"), and the
// longest key hint that fits.
bool reading_menu_full_header(int width, bool with_page);
const char *reading_menu_hint(ReadingTab tab, int width);

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

// Generic list: puts the cursor on entry `start` of a list with `count` entries,
// scrolled into view.
void list_menu_open(ListMenu *menu, int count, int start, int visible);

// Moves the cursor with Up, Down, Left and Right (a page), A picks the entry and
// B closes. With `can_delete`, Select deletes the entry. Gives BM_EVENT_JUMP for
// a pick, BM_EVENT_DELETE and BM_EVENT_CLOSE.
BmEvent list_menu_update(ListMenu *menu, int count, uint32_t actions, int visible, bool can_delete);

// Keeps the cursor inside a list of `count` entries.
void list_menu_clamp(ListMenu *menu, int count, int visible);

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

// Draws the bookmarks tab, centred on the screen. `first_chapter` is the book's
// first chapter with text, so the chapter numbers match the status row.
void bookmark_menu_draw(const BookmarkMenu *menu, const BookMarks *marks, int first_chapter, bool with_page, Screen screen);

// Draws the table of contents tab. `toc` may be NULL or empty for a book
// without one. `here` is the entry the reader is in, marked with a star.
void toc_menu_draw(const ListMenu *menu, const Toc *toc, int here, int bookmark_count, bool with_page, Screen screen);

// Draws the Page tab: the number being entered, the cursor under its digit and the page the reader is on.
void page_menu_draw(const PageMenu *menu, int current, int bookmark_count, Screen screen);

#endif
