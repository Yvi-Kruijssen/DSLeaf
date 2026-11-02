// The "Page" tab of the reading box (docs: ui.md, decision 057): jump to a page number of the
// whole book. Only offered when every chapter has been counted. The number is
// entered digit by digit: Left and Right pick a digit, Up and Down change it,
// A goes there and B closes. No DS code here, so it is tested on a PC.
#ifndef DSLEAF_PAGE_MENU_H
#define DSLEAF_PAGE_MENU_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	int value;   // the page chosen, 1 to total
	int total;   // pages in the book
	int digits;  // digits of the total, so the number is shown with that many
	int cursor;  // the digit being changed: 0 is the first (most significant) one
	bool changed;
} PageMenu;

typedef enum { PAGE_EVENT_NONE, PAGE_EVENT_CLOSE, PAGE_EVENT_GO } PageEvent;

// Starts on the page the reader is on, with the cursor on the last digit.
void page_menu_open(PageMenu *menu, int current, int total);

// Applies one frame of input. PAGE_EVENT_GO means the reader should go to `menu->value`.
PageEvent page_menu_update(PageMenu *menu, uint32_t actions);

// A touch on a digit: moves the cursor there and, with `delta` +1 or -1, changes that digit (a tap above or
// below it). The value stays in 1 .. total. Returns true if anything changed.
bool page_menu_tap(PageMenu *menu, int digit, int delta);

// The digit of the number at `index` (0 is the most significant of `digits`).
int page_menu_digit(const PageMenu *menu, int index);

#endif
