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

#include "bookmarks.h"
#include "config.h"
#include "chaptertext.h"
#include "epub.h"
#include "font.h"
#include "layout.h"

#define READER_PAGES_MAX 1024
#define READER_VIEW_MAX 2 // pages shown at once: one per screen
#define READER_WINDOWS_MAX 64   // windows of one chapter whose page counts are kept
#define READER_NO_TEXT 0xFFFF   // in chapter_pages: a chapter with nothing to read

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
	char notice[32];                // a short message for the status row, cleared by the next page turn

	uint32_t *chapter_start;        // bytes of chapter files before each chapter, for the percentage
	uint16_t *chapter_pages;        // pages of each chapter once all its windows were laid out, 0 if not known yet,
	                                // READER_NO_TEXT for a chapter without text (decision 054)
	uint16_t window_pages[READER_WINDOWS_MAX]; // pages of the windows of `windows_chapter` seen so far
	int windows_chapter;
	bool pages_changed;             // chapter_pages has new entries since the caller last cleared this
	uint32_t total_bytes;
} Reader;

// Page size for a font: the grid minus `margin` columns on each side, and minus
// one row for the status row. For an 8x8 font that is 30 columns and 23 rows, for
// 8x16 it is 30 by 11 (decision 043) and for 6x12 it is 40 by 15 (decision 044).
// With `rotated` (decision 049) the page is portrait: 22 by 31 for 8x8, 22 by 15
// for 8x16 and 30 by 20 for 6x12. Needs no DS code.
void reader_geometry(const Font *font, int margin, bool rotated, int *cols, int *rows);

// Opens a book at a chapter and an offset in that chapter's text (0 for the
// start). A chapter without text is skipped when the offset is 0. `style` is
// the paragraph spacing (NULL for the default). Returns EPUB_OK or the reason
// it failed.
EpubResult reader_open(Reader *r, const char *path, int chapter, uint32_t offset, int cols, int rows,
                       int per_view, const LayoutStyle *style);
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

// ---- Position, bookmarks and notices (decision 045) ----

// Jumps to a chapter and an offset in that chapter's text, as saved in a
// bookmark or as the last position. Returns false if the chapter does not exist.
bool reader_goto(Reader *r, int chapter, uint32_t offset);

// The part of the chapter text shown by page `slot` of the view, as offsets from
// the start of the chapter: from (included) up to to (not included).
void reader_page_range(const Reader *r, int slot, uint32_t *from, uint32_t *to);

// True if a bookmark falls on that page.
bool reader_page_marked(const Reader *r, int slot, const BookMarks *marks);

typedef enum { TOGGLE_ADDED, TOGGLE_REMOVED, TOGGLE_FULL } ToggleResult;

// A pressed: if any page shown has a bookmark they are all removed, otherwise
// one is added at the start of the first page. Sets a notice with the outcome.
ToggleResult reader_toggle_bookmark(Reader *r, BookMarks *marks);

// The first words of a page (as UTF-8), to recognise a bookmark by.
void reader_snippet(const Reader *r, int slot, char *out, size_t cap);

// Shows a short message in the status row until the next page turn.
void reader_set_notice(Reader *r, const char *text);

// What fits in a status row of `cols` columns. Both texts fit when they are short
// enough. If not, the notice (when `prefer_left`) or else the position on the
// right is kept and the other is dropped. A text that is longer than the row is
// cut. The outputs hold at most `cap` bytes.
void reader_status_fit(const char *left, const char *right, int cols, bool prefer_left, char *out_left,
                       char *out_right, size_t cap);

// How far through the book, 0 to 100.
int reader_percent(const Reader *r);

// Page number in the current window, starting at 1, and how many there are.
int reader_page_number(const Reader *r, int slot);
int reader_page_total(const Reader *r);

// Page number in the whole book, starting at 1, for the page in `slot` of the
// view, and the number of pages in the book (decision 054). A chapter that has
// been laid out whole counts exactly; chapters not seen yet (and the parts of a
// long chapter that are not loaded) are estimated from how many pages the
// chapters seen so far take per byte of their file. So the total settles as more
// of the book is read, and the number is exact for a book read from the start.
int reader_book_page(const Reader *r, int slot);
int reader_book_pages(const Reader *r);
// True when the number of pages is exact: every chapter has been laid out or is known to have no
// text. Until then the total is an estimate (shown with a "~").
bool reader_book_pages_exact(const Reader *r);

// Counting the rest of the book (decision 055). One call does a small piece of work: it lays
// out one window of a chapter whose pages are not known yet, chapters before the current one
// first (so the page number becomes exact), then the ones after it. Call it when the user is
// idle. Returns true if it did something, false when every chapter is known (or the book is
// not open). The numbers returned by reader_book_page(s) may change afterwards.
bool reader_count_step(Reader *r);

// The page counts of the chapters for saving (pagecache.h): `out` gets reader_chapter_total-all
// entries, i.e. one per chapter of the book. Returns how many, or 0 if nothing is known yet.
int reader_export_pages(const Reader *r, uint16_t *out, int cap);

// Takes saved counts (for the same layout) for the chapters that are not known yet. Returns how many were taken.
int reader_import_pages(Reader *r, const uint16_t *pages, int count);

// ---- Drawing (reader_view.c, needs the DS) ----
#include "render.h"

// The default margin is one column each side (decision 026), and the status row
// is the row below the text. The margin can be changed in Settings (decision 046).
#define READER_MARGIN 1

// Draws the pages if anything changed. With `dual` the first page goes on the
// top screen and the second on the bottom one. Otherwise one page is drawn on
// the bottom screen. Returns true when it drew.
bool reader_draw(Reader *r, bool dual, const BookMarks *marks, FooterInfo footer);

#endif
