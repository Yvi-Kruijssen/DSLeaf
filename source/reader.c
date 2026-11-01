#include "reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void reader_geometry(const Font *font, int *cols, int *rows) {
	// The grid: 32 by 24 8x8 tiles for a tile font, otherwise as many cells as fit in 256 by 192 pixels.
	int grid_cols = font_is_tile(font) ? 32 : 256 / font->width;
	int grid_rows = font_is_tile(font) ? 24 / (font->height / 8) : 192 / font->height;

	*cols = grid_cols - 2 * READER_MARGIN;
	if (*cols > LAYOUT_COLS_MAX) *cols = LAYOUT_COLS_MAX;
	*rows = grid_rows - 1; // the status row
	if (*rows > LAYOUT_ROWS_MAX) *rows = LAYOUT_ROWS_MAX;
}

static Page scratch; // used while counting pages, too big for the stack

// Lays out the whole current window to find where every page starts.
static void paginate(Reader *r) {
	PageStart s = layout_first_page();
	r->page_count = 0;

	while (r->page_count < READER_PAGES_MAX) {
		r->start[r->page_count++] = s;
		layout_page(r->text.buf, r->text.len, s, r->cols, r->rows, &r->style, &scratch);
		if (scratch.at_end) break;
		s = scratch.next;
	}
}

static int last_view_start(const Reader *r) {
	return (r->page_count - 1) / r->per_view * r->per_view;
}

// The page of the current window that holds `offset` (an offset within the window).
static int page_for(const Reader *r, uint32_t offset) {
	int p = 0;
	for (int i = 0; i < r->page_count; i++) {
		if (r->start[i].offset <= offset) p = i;
		else break;
	}
	return p;
}

static void layout_view(Reader *r) {
	r->view_count = 0;
	for (int i = 0; i < r->per_view; i++) {
		int p = r->page + i;
		if (p >= r->page_count) break;
		layout_page(r->text.buf, r->text.len, r->start[p], r->cols, r->rows, &r->style, &r->view[i]);
		r->view_count++;
	}
	r->redraw = true;
}

static void show_page(Reader *r, int page) {
	r->notice[0] = '\0';
	r->page = page / r->per_view * r->per_view;
	layout_view(r);
}

// Loads a chapter and shows the page holding `offset` (a chapter offset).
static bool load_chapter(Reader *r, int chapter, uint32_t offset) {
	if (r->open) ct_close(&r->text);
	r->open = false;
	if (!ct_open(&r->text, &r->book, chapter, offset)) return false;
	r->open = true;
	r->chapter = chapter;
	paginate(r);

	uint32_t in_window = offset > r->text.base ? offset - r->text.base : 0;
	show_page(r, page_for(r, in_window));
	return true;
}

// The next chapter, in the given direction, that has any text. -1 if none.
static int find_chapter(Reader *r, int from, int step) {
	for (int c = from + step; c >= 0 && c < r->book.chapter_count; c += step) {
		if (epub_chapter_has_text(&r->book, c)) return c;
	}
	return -1;
}

EpubResult reader_open(Reader *r, const char *path, int chapter, uint32_t offset, int cols, int rows,
                       int per_view) {
	memset(r, 0, sizeof *r);
	LayoutStyle style = LAYOUT_STYLE_DEFAULT;
	r->style = style;
	r->cols = cols;
	r->rows = rows;
	r->per_view = per_view < 1 ? 1 : (per_view > READER_VIEW_MAX ? READER_VIEW_MAX : per_view);

	EpubResult result = epub_open(&r->book, path);
	if (result != EPUB_OK) return result;

	r->chapter_start = malloc(sizeof *r->chapter_start * ((size_t)r->book.chapter_count + 1));
	if (!r->chapter_start) {
		epub_close(&r->book);
		return EPUB_ERR_MEMORY;
	}
	for (int i = 0; i < r->book.chapter_count; i++) {
		r->chapter_start[i] = r->total_bytes;
		r->total_bytes += r->book.chapter_size[i];
	}
	r->chapter_start[r->book.chapter_count] = r->total_bytes;

	if (chapter < 0 || chapter >= r->book.chapter_count) {
		chapter = 0;
		offset = 0;
	}
	// A chapter with nothing to read (a cover picture) at the start is not counted.
	r->first_chapter = epub_chapter_has_text(&r->book, 0) ? 0 : find_chapter(r, 0, 1);
	if (r->first_chapter < 0) r->first_chapter = 0;
	if (offset == 0 && chapter < r->first_chapter) chapter = r->first_chapter;
	if (offset == 0 && chapter != r->first_chapter && !epub_chapter_has_text(&r->book, chapter)) {
		int next = find_chapter(r, chapter, 1);
		if (next >= 0) chapter = next;
	}
	if (!load_chapter(r, chapter, offset)) {
		reader_close(r);
		return EPUB_ERR_READ;
	}
	return EPUB_OK;
}

void reader_close(Reader *r) {
	if (r->open) ct_close(&r->text);
	r->open = false;
	free(r->chapter_start);
	r->chapter_start = NULL;
	if (r->book.chapter) epub_close(&r->book);
}

bool reader_next(Reader *r) {
	if (!r->open) return false;

	if (r->page + r->per_view < r->page_count) {
		show_page(r, r->page + r->per_view);
		return true;
	}
	if (ct_has_next(&r->text)) {
		ct_next_window(&r->text);
		paginate(r);
		show_page(r, 0);
		return true;
	}
	int next = find_chapter(r, r->chapter, 1);
	if (next < 0) return false;
	return load_chapter(r, next, 0);
}

bool reader_prev(Reader *r) {
	if (!r->open) return false;

	if (r->page > 0) {
		show_page(r, r->page - r->per_view);
		return true;
	}
	if (r->text.window > 0) {
		if (!ct_prev_window(&r->text)) return false;
		paginate(r);
		show_page(r, last_view_start(r));
		return true;
	}
	int prev = find_chapter(r, r->chapter, -1);
	if (prev < 0) return false;
	if (!load_chapter(r, prev, 0xFFFFFFFFu)) return false; // the last window of that chapter
	show_page(r, last_view_start(r));
	return true;
}

int reader_chapter(const Reader *r) { return r->chapter; }

int reader_chapter_number(const Reader *r) {
	int n = r->chapter - r->first_chapter + 1;
	return n < 1 ? 1 : n;
}

int reader_chapter_total(const Reader *r) {
	int n = r->book.chapter_count - r->first_chapter;
	return n < 1 ? 1 : n;
}

void reader_set_style(Reader *r, LayoutStyle style) {
	if (!r->open) return;
	uint32_t keep = reader_offset(r);
	r->style = style;
	paginate(r);
	uint32_t in_window = keep > r->text.base ? keep - r->text.base : 0;
	show_page(r, page_for(r, in_window));
}

uint32_t reader_offset(const Reader *r) {
	if (!r->open || r->page >= r->page_count) return 0;
	return r->text.base + r->start[r->page].offset;
}

void reader_resize(Reader *r, int cols, int rows, int per_view) {
	if (!r->open) return;
	uint32_t keep = reader_offset(r);
	r->cols = cols;
	r->rows = rows;
	r->per_view = per_view < 1 ? 1 : (per_view > READER_VIEW_MAX ? READER_VIEW_MAX : per_view);
	paginate(r);
	uint32_t in_window = keep > r->text.base ? keep - r->text.base : 0;
	show_page(r, page_for(r, in_window));
}

int reader_percent(const Reader *r) {
	if (!r->open || r->total_bytes == 0) return 0;

	// How far through this chapter: the position in the text, scaled by how much
	// of the chapter file the text read so far stands for.
	uint64_t text_end = (uint64_t)r->text.base + r->text.len;
	uint64_t here = (uint64_t)r->text.base + (r->page < r->page_count ? r->start[r->page].offset : 0);
	uint64_t permille = text_end ? here * (uint64_t)ct_progress(&r->text) / text_end : 0; // 0..1000

	uint64_t size = r->book.chapter_size[r->chapter];
	uint64_t done = r->chapter_start[r->chapter] + size * permille / 1000;
	int percent = (int)(done * 100 / r->total_bytes);
	return percent > 100 ? 100 : percent;
}

int reader_page_number(const Reader *r, int slot) { return r->page + slot + 1; }
int reader_page_total(const Reader *r) { return r->page_count; }

// ---- Position, bookmarks and notices ----

bool reader_goto(Reader *r, int chapter, uint32_t offset) {
	if (!r->open || chapter < 0 || chapter >= r->book.chapter_count) return false;

	uint32_t base = r->text.base;
	if (chapter == r->chapter && offset >= base && offset <= base + r->text.len) {
		show_page(r, page_for(r, offset - base)); // already loaded
		return true;
	}
	return load_chapter(r, chapter, offset);
}

void reader_page_range(const Reader *r, int slot, uint32_t *from, uint32_t *to) {
	if (!r->open || slot < 0 || slot >= r->view_count) {
		*from = *to = 0;
		return;
	}
	*from = r->text.base + r->view[slot].start.offset;
	*to = r->text.base + r->view[slot].next.offset;
}

bool reader_page_marked(const Reader *r, int slot, const BookMarks *marks) {
	uint32_t from, to;
	reader_page_range(r, slot, &from, &to);
	return marks_count_range(marks, r->chapter, from, to) > 0;
}

void reader_set_notice(Reader *r, const char *text) {
	snprintf(r->notice, sizeof r->notice, "%s", text);
	r->redraw = true;
}

void reader_snippet(const Reader *r, int slot, char *out, size_t cap) {
	size_t n = 0;
	if (cap == 0) return;
	out[0] = '\0';
	if (!r->open || slot < 0 || slot >= r->view_count) return;

	const Page *page = &r->view[slot];
	bool space = false;
	for (int l = 0; l < page->line_count && n + 4 < cap; l++) {
		if (n > 0) space = true; // a new line is a space between words
		for (int k = 0; k < page->line[l].len && n + 4 < cap; k++) {
			uint8_t g = page->line[l].glyph[k];
			if (g == ' ' || g < 0x20) {
				space = n > 0;
				continue;
			}
			if (space) out[n++] = ' ';
			space = false;
			// The glyphs are Latin-1, so write them out as UTF-8.
			if (g < 0x80) {
				out[n++] = (char)g;
			} else {
				out[n++] = (char)(0xC0 | (g >> 6));
				out[n++] = (char)(0x80 | (g & 0x3F));
			}
		}
		if (n >= 24) break; // enough to recognise the page
	}
	out[n] = '\0';
}

ToggleResult reader_toggle_bookmark(Reader *r, BookMarks *marks) {
	int removed = 0;
	for (int slot = 0; slot < r->view_count; slot++) {
		uint32_t from, to;
		reader_page_range(r, slot, &from, &to);
		removed += marks_remove_range(marks, r->chapter, from, to);
	}
	if (removed > 0) {
		reader_set_notice(r, "Bookmark removed");
		return TOGGLE_REMOVED;
	}

	uint32_t from, to;
	reader_page_range(r, 0, &from, &to);
	char text[MARK_TEXT_MAX * 2];
	reader_snippet(r, 0, text, sizeof text);

	if (marks_add(marks, r->chapter, from, text) == MARK_FULL) {
		reader_set_notice(r, "Bookmarks full");
		return TOGGLE_FULL;
	}
	reader_set_notice(r, "Bookmark added");
	return TOGGLE_ADDED;
}
