// The About screen (docs: decision 077): what DSLeaf is, its licence, and the notices the fonts
// need. A page of text that scrolls. The logic has no DS code and is tested on a PC; drawing is in
// about_view.c.
//
// Up and Down scroll by a line, Left and Right (and L and R) by a page, B goes back.
#ifndef DSLEAF_ABOUT_H
#define DSLEAF_ABOUT_H

#include <stdbool.h>
#include <stdint.h>

#include "render.h"

#define ABOUT_COLS 28        // characters in a line of the panel
#define ABOUT_ROWS 13        // lines shown at once
#define ABOUT_LINES_MAX 400

// The text (about_text.c, made by tools/make_about.py). '#' starts a heading, "" is a blank line.
extern const char *const about_paragraphs[];
extern const int about_paragraph_count;

typedef struct {
	int top;       // first line shown
	bool redraw;
	int count;     // lines in all
	uint16_t para[ABOUT_LINES_MAX];  // paragraph of the line
	uint16_t from[ABOUT_LINES_MAX];  // first byte of the line inside its paragraph
	uint8_t len[ABOUT_LINES_MAX];
	bool heading[ABOUT_LINES_MAX];
} About;

// Wraps the text and shows it from the start.
void about_open(About *a);

// The text of one line (copied, NUL ended, at most ABOUT_COLS characters) and whether it is a heading.
void about_line(const About *a, int line, char *out, bool *heading);

// Applies one frame of input. Returns true when the screen should be left (B).
bool about_update(About *a, uint32_t actions);

// What a touch does (decision 078): the upper half of the text a page up, the lower half a page down, the hint
// line at the bottom goes back. Returns the action to give about_update.
uint32_t about_touch(int x, int y);

// Page numbers for the corner of the panel, starting at 1.
int about_page(const About *a);
int about_pages(const About *a);

// ---- Drawing (about_view.c, needs the DS) ----
bool about_draw(About *a, Screen screen);

#endif
