// The reader: an open book, a position in it, and the pages around it
// (docs: ui.md, decisions 026, 041). No DS code here, so it is tested on a PC.
//
// The text of the current chapter window is laid out into pages when it is
// loaded, so the number of pages is known and any page can be shown. Moving
// past the end of a window or chapter loads the next one.
#ifndef DSLEAF_READER_H
#define DSLEAF_READER_H

#include <stdbool.h>
#include <stdint.h>

#include "chaptertext.h"
#include "epub.h"
#include "font.h"
#include "layout.h"

#define READER_PAGES_MAX 1024
#define READER_VIEW_MAX 2 // pages shown at once: one per screen

typedef struct {
	Epub book;
	ChapterText text;
	bool open;
	int chapter;

	int cols, rows;
	int per_view;                   // 1 (single screen) or 2 (both screens)
	LayoutStyle style;              // paragraph spacing and indent
	int first_chapter;              // the first chapter with text, shown as chapter 1

	PageStart start[READER_PAGES_MAX]; // where each page of the current window starts
	int page_count;
	int page;                       // first page being shown, a multiple of per_view

	Page view[READER_VIEW_MAX];
	int view_count;                 // pages in `view` that hold something
	bool redraw;

	uint32_t *chapter_start;        // bytes of chapter files before each chapter, for the percentage
	uint32_t total_bytes;
} Reader;

// Page size for a font on a normal (not rotated) screen: the grid minus a one
// column margin on each side, and minus one row for the status row. For an 8x8
// font that is 30 columns and 23 rows, for 8x16 it is 30 by 11 (decision 043)
// and for 6x12 it is 40 by 15 (decision 044). Needs no DS code.
void reader_geometry(const Font *font, int *cols, int *rows);

// Opens a book at a chapter and an offset in that chapter's text (0 for the
// start). A chapter without text is skipped when the offset is 0. Returns
// EPUB_OK or the reason it failed.
EpubResult reader_open(Reader *r, const char *path, int chapter, uint32_t offset, int cols, int rows,
                       int per_view);
void reader_close(Reader *r);

// Page forward or back by one view (one page, or two when both screens are
// used). They move on into the next window or chapter, and return false only
// at the very end or start of the book.
bool reader_next(Reader *r);
bool reader_prev(Reader *r);

// Changes the page size or the number of pages shown, keeping the position.
void reader_resize(Reader *r, int cols, int rows, int per_view);

// Where the first page shown begins: the chapter, and the offset within it.
int reader_chapter(const Reader *r);
uint32_t reader_offset(const Reader *r);

// Chapter numbers as shown to the reader. A cover page that only holds a
// picture is not counted, so the first page of text is chapter 1.
int reader_chapter_number(const Reader *r);
int reader_chapter_total(const Reader *r);

// Changes the paragraph spacing and indent, keeping the position.
void reader_set_style(Reader *r, LayoutStyle style);

// How far through the book, 0 to 100.
int reader_percent(const Reader *r);

// Page number in the current window, starting at 1, and how many there are.
int reader_page_number(const Reader *r, int slot);
int reader_page_total(const Reader *r);

// ---- Drawing (reader_view.c, needs the DS) ----
#include "render.h"

// The text area on the 32 column grid has a one column margin each side (decision
// 026). The status row is the row below the text.
#define READER_MARGIN 1

// Draws the pages if anything changed. With `dual` the first page goes on the
// top screen and the second on the bottom one. Otherwise one page is drawn on
// the bottom screen. Returns true when it drew.
bool reader_draw(Reader *r, bool dual);

#endif
