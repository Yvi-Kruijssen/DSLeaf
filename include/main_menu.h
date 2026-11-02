// Main menu (docs: ui.md, decisions 014, 015, 016).
//
// A "Recent Reads" list of up to 3 books with Open, Library and Settings
// buttons below. The logic here has no DS dependencies. Drawing is separate,
// in main_menu_view.c.
#ifndef DSLEAF_MAIN_MENU_H
#define DSLEAF_MAIN_MENU_H

#include <stdbool.h>
#include <stdint.h>

#include "recent.h"
#include "render.h"

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
	char notice[40];  // a warning for the bottom row, such as "No SD card", or empty
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

// Sets the warning shown in red on the bottom row, or clears it with NULL. The
// text is cut to fit. init clears it, so set it again after that.
void main_menu_set_notice(MainMenu *menu, const char *text);

// Makes the next main_menu_draw redraw everything, for example after another
// screen was shown on top.
void main_menu_invalidate(MainMenu *menu);

// Draws the menu if anything changed. Always draws on one screen, never
// rotated. Returns true when it drew.
bool main_menu_draw(MainMenu *menu, Screen screen);

// Positions, shared by the logic (touch) and the drawing (decision 053). The
// book titles are tile text on the 8x8 grid, everything else is a picture in
// pixels. Each book has a band of paper, 24 pixels high, with a ruled line.
#define MENU_BOOK_ROW(i) (5 + 3 * (i))        // the text row of book i
#define MENU_BAND_Y(i)   (32 + 24 * (i))      // top of the band of book i
#define MENU_BAND_H      24
#define MENU_PANEL_H     100                  // the panel starts at y 15 (paper.h)
#define MENU_BUTTON_Y    136
#define MENU_NOTICE_ROW  21                   // the warning under the buttons
#define MENU_BUTTON_H    24
#define MENU_BUTTON_W    72
#define MENU_BUTTON_X(i) (10 + 82 * (i))      // 0 Open, 1 Library, 2 Settings: x 10 to 245

#endif
