// Main menu (docs: ui.md, decisions 014, 015, 016).
//
// A "Recent Reads" list of up to 3 books with Open, Library and Settings
// buttons below. The logic here has no DS dependencies. Drawing is separate,
// in main_menu_view.c.
#ifndef DSLEAF_MAIN_MENU_H
#define DSLEAF_MAIN_MENU_H

#include <stdbool.h>
#include <stdint.h>

#include "render.h"

#define RECENT_MAX 3
#define TITLE_MAX 96 // bytes of UTF-8, including the terminator

typedef enum {
	FOCUS_BOOK,     // one of the books has the cursor, Open is enabled
	FOCUS_LIBRARY,  // the button row, Open is greyed out
	FOCUS_SETTINGS,
} MenuFocus;

typedef enum {
	MENU_EVENT_NONE,
	MENU_EVENT_OPEN_BOOK,     // `index` is the book in the list
	MENU_EVENT_OPEN_LIBRARY,
	MENU_EVENT_OPEN_SETTINGS,
	MENU_EVENT_REMOVE_BOOK,   // already removed from the menu, `index` is where it was
} MenuEventKind;

typedef struct {
	MenuEventKind kind;
	int index;
} MainMenuEvent;

typedef struct {
	char title[RECENT_MAX][TITLE_MAX];
	int count;
	MenuFocus focus;
	int book;         // focused book when focus is FOCUS_BOOK
	int press_frames; // frames left of the Open button press effect
	int press_book;
	bool redraw;      // something changed since the last draw
} MainMenu;

// Starts with the given books (UTF-8 titles, newest first). At most
// RECENT_MAX are used. Focus starts on the first book, or on Library when the
// list is empty.
void main_menu_init(MainMenu *menu, const char *const *titles, int count);

// Call once per frame with the actions from the input layer. A tap position
// is optional (touch is only an extra way to do the same things).
// Returns what the caller should do, if anything.
MainMenuEvent main_menu_update(MainMenu *menu, uint32_t actions, bool tapped, int tap_x,
                               int tap_y);

// Draws the menu if anything changed. Always draws on one screen, never
// rotated. Returns true when it drew.
bool main_menu_draw(MainMenu *menu, Screen screen);

// Grid positions, shared by the logic (touch) and the drawing.
#define MENU_TITLE_ROW   1
#define MENU_BOOK_ROW(i) (4 + 2 * (i))
#define MENU_BUTTON_ROW  21
#define MENU_OPEN_COL    1
#define MENU_OPEN_W      8
#define MENU_LIBRARY_COL 10
#define MENU_LIBRARY_W   9
#define MENU_SETTINGS_COL 20
#define MENU_SETTINGS_W  10

#endif
