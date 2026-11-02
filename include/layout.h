// Page layout (docs: ui.md, decisions 026, 041).
//
// Wraps the extracted text of a chapter into pages of fixed-width lines. The
// core is one pure function, layout_page: given where a page starts it fills
// in the page and says where the next page starts. Nothing is kept between
// calls, so any page can be laid out at any time, and the same input always
// gives the same pages. That is what makes bookmarks and changing the page size
// safe.
//
// The text is read from a compact buffer of records (see below) that
// chaptertext.c fills from the word stream.
#ifndef DSLEAF_LAYOUT_H
#define DSLEAF_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "textx.h"

// ---- The compact record format ----
// Records follow each other with no padding:
//   WORD:  REC_WORD, flags (bit 0: space before), length, then `length` bytes of UTF-8
//   BLOCK: REC_BLOCK, kind (BlockKind), level
//   BREAK: REC_BREAK
//   IMAGE: REC_IMAGE (a picture that gets a page of its own; chaptertext.c keeps its file name)
enum { REC_WORD = 1, REC_BLOCK = 2, REC_BREAK = 3, REC_IMAGE = 4 };
#define REC_FLAG_SPACE 1

// Size in bytes of the record at `p`, or 0 if its type is unknown.
size_t record_size(const uint8_t *p);

// Appends the record for `item` to `buf`. Returns its size, or 0 if it does not
// fit in `cap - len`. Items without a record (END) also give 0.
size_t record_append(uint8_t *buf, size_t len, size_t cap, const TextItem *item);

// ---- Layout ----
#define LAYOUT_COLS_MAX 48
#define LAYOUT_ROWS_MAX 40
#define LAYOUT_LIST_INDENT 2
#define LAYOUT_QUOTE_INDENT 2

// How paragraphs are set. Both are adjustable later in Settings (decision 028).
typedef struct {
	uint8_t paragraph_gap;    // blank lines between paragraphs
	uint8_t paragraph_indent; // first-line indent, in columns
} LayoutStyle;

// A blank line between paragraphs and no indent (decision 042).
#define LAYOUT_STYLE_DEFAULT { 1, 0 }

// Where a page begins. Not always at a block start, so it also remembers what
// kind of block the page starts in.
typedef struct {
	uint32_t offset;     // of the record the page starts with
	uint16_t skip;       // glyphs of that word already shown on the previous page
	uint8_t block;       // BlockKind in force
	uint8_t level;
	bool continuation;   // the block started on an earlier page, so no first-line indent
} PageStart;

typedef struct {
	uint8_t glyph[LAYOUT_COLS_MAX]; // glyph codes (Latin-1), indent and centring included as spaces
	uint8_t len;
} LayoutLine;

typedef struct {
	LayoutLine line[LAYOUT_ROWS_MAX];
	int line_count;
	PageStart start;
	PageStart next;  // where the following page starts
	bool at_end;     // nothing follows this page
	bool picture;    // the page is one picture (line_count is 0), see REC_IMAGE (decision 075)
	uint32_t picture_offset; // of its REC_IMAGE record
} Page;

// The very first page of a buffer.
PageStart layout_first_page(void);

// Lays out one page from `start` using lines of `cols` glyphs and `rows` lines
// per page. `cols` and `rows` are limited to LAYOUT_COLS_MAX and LAYOUT_ROWS_MAX.
void layout_page(const uint8_t *buf, size_t len, PageStart start, int cols, int rows,
                 const LayoutStyle *style, Page *page);

#endif
