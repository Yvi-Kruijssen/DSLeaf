// The sample page in Settings (docs: decision 051).
//
// Lays out a short built-in text (the opening of Pride and Prejudice, public
// domain) with the page size and paragraph style that a book would get, so
// the user can see a font, line spacing, margins, indent and orientation
// before opening a book. No DS code here, so it is tested on a PC. Drawing is
// in preview_view.c.
#ifndef DSLEAF_PREVIEW_H
#define DSLEAF_PREVIEW_H

#include "layout.h"
#include "render.h"

typedef struct {
	Page page[2];
	int pages; // 1 in single-screen mode, 2 in dual-screen mode
	int cols, rows;
} Preview;

// Lays out `pages` (1 or 2) pages of `cols` x `rows`. If the sample runs out
// before the last page, that page is empty.
void preview_build(Preview *p, int cols, int rows, int pages, const LayoutStyle *style);

// ---- Drawing (preview_view.c, needs the DS) ----

// Draws the pages like the reader does: text, then a status row with "Preview".
// With one page it goes on the bottom screen, with two the first is on top.
void preview_draw(const Preview *p);

#endif
