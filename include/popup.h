// The pop-up menu over the reading page (docs: ui.md, decision 066): its size and place, how a
// picture drawn in "page" coordinates is turned when the page is rotated, and the key hints.
// No DS code here, so it is tested on a PC. Drawing is in bookmark_menu_view.c.
//
// The pop-up is drawn in coordinates of the page as the reader holds it, called logical here:
// 256 x 192 for a normal page and 192 x 256 for a rotated one (decision 049). `popup_to_screen`
// turns a rectangle of the logical page into a rectangle of the real screen.
#ifndef DSLEAF_POPUP_H
#define DSLEAF_POPUP_H

#include <stdbool.h>

#include "bookmark_menu.h"

#define POPUP_HEADER_H 16   // the tab bar (it was 22 pixels, then 18; decisions 072 and 083)
#define POPUP_ROW_H 16      // one row of a list
#define POPUP_FOOTER_H 24   // the key hints on one line (a rotated pop-up is narrower and has two lines: 36)
#define POPUP_FOOTER2_H 36
#define POPUP_TOP_GAP 2     // between the tab bar and the first row

typedef struct {
	int x, y, w, h;    // the panel, in logical coordinates
	int list_y;        // where the list starts inside the panel (relative to y)
	int footer_h;      // the height of the hint area: one line, or two when the pop-up is narrow
	int rows;          // rows of a list that fit
	int logical_w, logical_h;
} PopupGeometry;

// The panel for a normal or a rotated page. It stays 15 pixels or more from the top and the bottom of
// the screen (the screen protector of decision 061) and 10 from the sides.
PopupGeometry popup_geometry(bool rotated);

// The rectangle (x, y, w, h) of the logical page on the real screen. Not rotated it is the same.
// Rotated, the page is turned like the text of decision 049: logical (x, y) is at screen
// (255 - y, x), so a logical rectangle becomes a screen rectangle of w and h swapped.
void popup_to_screen(bool rotated, int x, int y, int w, int h, int *sx, int *sy, int *sw, int *sh);

// The same for one pixel.
void popup_pixel_to_screen(bool rotated, int x, int y, int *sx, int *sy);

// The inverse of popup_pixel_to_screen: the logical pixel under a point of the screen (a touch).
void popup_screen_to_pixel(bool rotated, int sx, int sy, int *x, int *y);

typedef struct {
	int x, y, w, h;
} PopupRect;

// ---- The tab bar ----
//
// The bar is the top strip of the panel, over its whole width. The L and the R button (they switch tabs) are the two
// top corners of the pop-up, shaped like the shoulder buttons of a DS Lite, and the tabs are between them. The tab in
// use reaches up to the top edge of the pop-up and is joined to the panel below it. Each tab has an icon and a name.
// When the names do not all fit, the tab in use keeps its name and the others show only their icon.
#define POPUP_SHOULDER_W 21  // the L and R buttons, from the top of the pop-up down to the line under the strip
#define POPUP_ICON_W 8
#define POPUP_TAB_PAD 4      // space at the end of a tab
#define POPUP_TAB_ICON_ONLY_W 22
#define POPUP_TAB_GAP 2
#define POPUP_TABS_MAX 3

typedef struct {
	PopupRect box;     // the area of the tab on the bar (the tab in use is drawn a little higher)
	ReadingTab tab;
	const char *name;  // NULL when only the icon is shown
	bool active;
} PopupTabBox;

// Lays out the bar for `tabs` tabs (2 or 3) with `active` in use. Returns how many boxes were filled (`tabs`).
// `prev` and `next` get the L and R buttons.
int popup_tab_layout(const PopupGeometry *g, int tabs, ReadingTab active, PopupTabBox out[POPUP_TABS_MAX], PopupRect *prev,
                     PopupRect *next);

// The names of the tabs: level 0 is the one used when there is room (Contents, Marks, Page), level 1 the short one
// (Index, Marks, Page).
const char *popup_tab_name(ReadingTab tab, int level);

// ---- The key hints ----
//
// The keys of a tab as small buttons with what they do, most important first. The shoulder buttons are on the tab bar.
typedef enum { POPUP_GO, POPUP_BACK, POPUP_DELETE } PopupAction;

typedef struct {
	const char *keys;  // the letters on the button
	const char *label;
	PopupAction action;
} PopupHint;

#define POPUP_HINTS_MAX 3

// Fills `out` and returns how many: Go and Back for every tab, and Del (Select) for the bookmarks.
int popup_hints(ReadingTab tab, PopupHint out[POPUP_HINTS_MAX]);

typedef struct {
	PopupRect box;     // the button and its label
	PopupHint hint;
} PopupHintBox;

// Where the hints are drawn: from the left of the hint area, going on to the second line (when the pop-up has two)
// and dropping those that do not fit. Returns how many are placed.
int popup_hint_layout(const PopupGeometry *g, ReadingTab tab, PopupHintBox out[POPUP_HINTS_MAX]);

// The width of the position text ("5/37") needed at the right of the first hint line, and whether it fits there.
bool popup_position_fits(const PopupGeometry *g, ReadingTab tab, int text_width);

// ---- The Page tab ----
//
// The number in large digits: the left of the first digit and the top of the digits. Each digit is POPUP_DIGIT_W wide.
#define POPUP_DIGIT_W 18
void popup_digit_layout(const PopupGeometry *g, int digits, int *x0, int *y0);

// ---- Touch ----
typedef enum {
	POPUP_HIT_NONE,       // inside the pop-up, on nothing
	POPUP_HIT_OUTSIDE,    // outside: closes the pop-up
	POPUP_HIT_TAB,        // index: the tab
	POPUP_HIT_TAB_PREV,   // the L button
	POPUP_HIT_TAB_NEXT,   // the R button
	POPUP_HIT_ROW,        // index: the row of the list (0 is the first one shown)
	POPUP_HIT_SCROLL_UP,  // the upper half of the scrollbar: a page up
	POPUP_HIT_SCROLL_DOWN,
	POPUP_HIT_HINT,       // index: a PopupAction
	POPUP_HIT_DIGIT,      // index: the digit, to move the cursor there
	POPUP_HIT_DIGIT_UP,   // index: the digit, the area above it (one more in that place)
	POPUP_HIT_DIGIT_DOWN, // index: the digit, the area below it (one less)
} PopupHitKind;

typedef struct {
	PopupHitKind kind;
	int index;
} PopupHit;

// What a touch at the logical point (x, y) hits. `tabs` is the number of tabs and `digits` the digits of the
// number on the Page tab (only used for that tab).
PopupHit popup_hit(const PopupGeometry *g, ReadingTab tab, int tabs, int digits, int x, int y);

#endif
