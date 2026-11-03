// Turns an XHTML chapter into a stream of words and breaks (docs: formats.md,
// decisions 038, 040). The page layout reads this stream, so a chapter never
// has to be in memory whole.
//
// What comes out:
// - WORD: a run of text without spaces. `space_before` says whether the layout
//   should put a space in front of it. Words glue together across inline tags,
//   so "<i>Hel</i>lo" is one word, "Hello". A no-break space (U+00A0) stays
//   inside a word.
// - BLOCK: a new block starts (paragraph, heading, list item, quote, rule or table row).
//   One comes before the first word of every block. A rule is a block of its
//   own, so the next block starts after it.
// - LINEBREAK: a <br>.
// - IMAGE: an image was here. `text` has its file name. Nothing draws it yet.
// - ANCHOR: an element with an id starts here, and `text` is the id. Only given
//   when asked for with `want_anchors`. It marks a position between two other
//   items, so a link such as "chapter.xhtml#part2" can be turned into a place in the text.
// - END: the chapter is finished.
//
// What it leaves out: <head>, styles, scripts, math and SVG drawings (but an
// image inside an SVG still gives an IMAGE item), and page number markers
// (epub:type="pagebreak"). CSS is ignored. Whitespace inside <pre> is
// collapsed like everywhere else. Text is assumed to be UTF-8.
#ifndef DSLEAF_TEXTX_H
#define DSLEAF_TEXTX_H

#include <stdbool.h>
#include <stdint.h>

#include "xml.h"

#define TEXTX_WORD_MAX 64 // longer words are split into pieces of up to this many bytes
#define TEXTX_STACK 24

typedef enum {
	TEXT_END,
	TEXT_WORD,
	TEXT_BLOCK,
	TEXT_LINEBREAK,
	TEXT_IMAGE,
	TEXT_ANCHOR,
} TextItemKind;

typedef enum {
	BLOCK_PARAGRAPH,
	BLOCK_HEADING,
	BLOCK_LIST_ITEM,
	BLOCK_QUOTE,
	BLOCK_RULE,
	BLOCK_ROW,       // a table row: no blank line between rows, cells separated by "|" (decision 089)
} BlockKind;

typedef struct {
	TextItemKind kind;
	char text[XML_ATTR_VALUE_MAX]; // WORD: the word. IMAGE: the file name. Otherwise empty.
	bool space_before;             // WORD
	BlockKind block;               // BLOCK
	int level;                     // BLOCK: heading level 1 to 6, otherwise 0
} TextItem;

typedef struct {
	char name[12];
	BlockKind kind;
	int level;
} TextBlock;

typedef struct {
	XmlReader xml;
	bool have_event;
	XmlEvent event;
	size_t scan; // position in the current text piece

	char word[TEXTX_WORD_MAX + 1];
	int word_len;
	bool word_space;    // space before the word being built
	bool pending_space; // whitespace seen since the last word

	bool need_block;    // a BLOCK item is due before the next word
	bool block_start;   // no word yet in the current block
	int skip_depth;     // inside something that is left out
	bool skip_is_block; // the skipped part is a break in the text (not an inline marker)
	bool done;
	bool want_anchors;  // set this to get ANCHOR items
	bool anchor_done;   // the anchor of the current tag has been given
	int row_cells;      // cells started in the current table row

	TextBlock stack[TEXTX_STACK];
	int stack_depth;
} TextExtractor;

void textx_init(TextExtractor *tx, XmlReadFn read, void *user);

// Gets the next item. Returns false after the END item has been returned.
bool textx_next(TextExtractor *tx, TextItem *item);

#endif
