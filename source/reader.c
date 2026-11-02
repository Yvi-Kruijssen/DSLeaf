#include "reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pixbuf.h"

void reader_geometry(const Font *font, int margin, bool rotated, int *cols, int *rows) {
	// The grid: 32 by 24 8x8 tiles for a tile font (24 by 32 when rotated), otherwise
	// as many cells as fit in the 256 by 192 pixel screen, which pixbuf.c works out.
	int grid_cols, grid_rows;
	if (font_is_tile(font)) {
		grid_cols = rotated ? 24 : 32;
		grid_rows = (rotated ? 32 : 24) / (font->height / 8);
	} else {
		grid_cols = pix_cols(font, rotated);
		grid_rows = pix_rows(font, rotated);
	}

	if (margin < 0) margin = 0;
	*cols = grid_cols - 2 * margin;
	if (*cols > LAYOUT_COLS_MAX) *cols = LAYOUT_COLS_MAX;
	if (*cols < 8) *cols = 8; // never narrower than a few words
	*rows = grid_rows - 1; // the status row
	if (*rows > LAYOUT_ROWS_MAX) *rows = LAYOUT_ROWS_MAX;
}

static Page scratch; // used while counting pages, too big for the stack

// The chapter being counted in the background, between steps (decision 055). It has a buffer of its
// own, so it does not disturb the pages being read.
static ChapterText counter;
static bool counter_open;
static int counter_chapter;
static uint32_t counter_sum;

static void counter_close(void) {
	if (counter_open) ct_close(&counter);
	counter_open = false;
}

// The page counts of chapters are only right for the layout they were made with.
static void forget_pages(Reader *r) {
	counter_close();
	if (!r->chapter_pages) return;
	for (int i = 0; i <= r->book.chapter_count; i++) {
		if (r->chapter_pages[i] != READER_NO_TEXT) r->chapter_pages[i] = 0; // having no text does not depend on the layout
	}
	memset(r->window_pages, 0, sizeof r->window_pages);
	r->windows_chapter = -1;
}

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

	if (!r->chapter_pages) return;
	// Remember the pages of this window. When the last window of a chapter is reached and all the
	// windows before it have been seen, the chapter is known whole.
	if (r->windows_chapter != r->chapter) {
		memset(r->window_pages, 0, sizeof r->window_pages);
		r->windows_chapter = r->chapter;
	}
	if (r->text.window < READER_WINDOWS_MAX) r->window_pages[r->text.window] = (uint16_t)r->page_count;
	if (!ct_has_next(&r->text) && r->text.window < READER_WINDOWS_MAX) {
		uint32_t sum = 0;
		bool all = true;
		for (int w = 0; w <= r->text.window; w++) {
			if (r->window_pages[w] == 0) all = false;
			sum += r->window_pages[w];
		}
		if (all) {
			uint16_t n = sum >= READER_NO_TEXT ? READER_NO_TEXT - 1 : (uint16_t)sum;
			if (r->chapter_pages[r->chapter] != n) r->pages_changed = true;
			r->chapter_pages[r->chapter] = n;
		}
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

// Whether a chapter has text. The answer is kept for the page numbers: a chapter without text counts no pages.
static bool has_text(Reader *r, int c) {
	if (r->chapter_pages) {
		if (r->chapter_pages[c] == READER_NO_TEXT) return false;
		if (r->chapter_pages[c] != 0) return true;
	}
	bool text = epub_chapter_has_text(&r->book, c);
	if (!text && r->chapter_pages) {
		r->chapter_pages[c] = READER_NO_TEXT;
		r->pages_changed = true;
	}
	return text;
}

// The next chapter, in the given direction, that has any text. -1 if none.
static int find_chapter(Reader *r, int from, int step) {
	for (int c = from + step; c >= 0 && c < r->book.chapter_count; c += step) {
		if (has_text(r, c)) return c;
	}
	return -1;
}

EpubResult reader_open(Reader *r, const char *path, int chapter, uint32_t offset, int cols, int rows,
                       int per_view, const LayoutStyle *style) {
	memset(r, 0, sizeof *r);
	LayoutStyle fallback = LAYOUT_STYLE_DEFAULT;
	r->style = style ? *style : fallback;
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
	r->windows_chapter = -1;
	r->chapter_pages = calloc((size_t)r->book.chapter_count + 1, sizeof *r->chapter_pages);
	if (!r->chapter_pages) {
		free(r->chapter_start);
		r->chapter_start = NULL;
		epub_close(&r->book);
		return EPUB_ERR_MEMORY;
	}

	if (chapter < 0 || chapter >= r->book.chapter_count) {
		chapter = 0;
		offset = 0;
	}
	// A chapter with nothing to read (a cover picture) at the start is not counted.
	r->first_chapter = has_text(r, 0) ? 0 : find_chapter(r, 0, 1);
	if (r->first_chapter < 0) r->first_chapter = 0;
	if (offset == 0 && chapter < r->first_chapter) chapter = r->first_chapter;
	if (offset == 0 && chapter != r->first_chapter && !has_text(r, chapter)) {
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
	counter_close();
	if (r->open) ct_close(&r->text);
	r->open = false;
	free(r->chapter_start);
	r->chapter_start = NULL;
	free(r->chapter_pages);
	r->chapter_pages = NULL;
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
	forget_pages(r);
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
	forget_pages(r); // a new page size makes other pages
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

// Pages per byte of chapter file, as a fraction pages / bytes. It comes from the chapters that are
// known, mixed with a first guess: in the three test books a page took about one byte of chapter
// file for each cell of the page (columns times rows), at every page size, so that guess counts as
// if READER_PRIOR_BYTES of file had been seen. Real chapters outweigh it as they are laid out.
#define READER_PRIOR_BYTES (128 * 1024)

static void page_density(const Reader *r, uint64_t *pages, uint64_t *bytes) {
	uint64_t cells = (uint64_t)(r->cols > 0 ? r->cols : 1) * (uint64_t)(r->rows > 0 ? r->rows : 1);
	uint64_t p = 0, b = 0;
	for (int i = 0; i < r->book.chapter_count; i++) {
		if (r->chapter_pages[i] && r->chapter_pages[i] != READER_NO_TEXT) {
			p += r->chapter_pages[i];
			b += r->book.chapter_size[i];
		}
	}
	// pages = p + prior / cells and bytes = b + prior, both multiplied by `cells` to stay in integers.
	*pages = p * cells + READER_PRIOR_BYTES;
	*bytes = (b + READER_PRIOR_BYTES) * cells;
}

// The pages before the current chapter and in the whole book.
static void book_counts(const Reader *r, uint64_t *before, uint64_t *total) {
	uint64_t p, b;
	page_density(r, &p, &b);
	uint64_t known_before = 0, known_total = 0, bytes_before = 0, bytes_total = 0;
	for (int i = 0; i < r->book.chapter_count; i++) {
		uint16_t n = r->chapter_pages[i];
		if (n == READER_NO_TEXT) continue; // no pages at all
		if (n) {
			known_total += n;
			if (i < r->chapter) known_before += n;
		} else {
			bytes_total += r->book.chapter_size[i];
			if (i < r->chapter) bytes_before += r->book.chapter_size[i];
		}
	}
	*before = known_before + (b ? bytes_before * p / b : 0);
	*total = known_total + (b ? bytes_total * p / b : 0);
}

// Pages of the chapter in the windows before the current one: exact when they were all seen,
// otherwise the windows are taken to be as long as this one.
static uint64_t pages_before_window(const Reader *r) {
	if (r->windows_chapter != r->chapter) return (uint64_t)r->text.window * (uint64_t)r->page_count;
	uint64_t sum = 0;
	for (int w = 0; w < r->text.window; w++) {
		if (w >= READER_WINDOWS_MAX || r->window_pages[w] == 0) return (uint64_t)r->text.window * (uint64_t)r->page_count;
		sum += r->window_pages[w];
	}
	return sum;
}

int reader_book_page(const Reader *r, int slot) {
	if (!r->open || !r->chapter_pages) return 1;
	uint64_t before, total;
	book_counts(r, &before, &total);
	uint64_t in_chapter = pages_before_window(r) + (uint64_t)(r->page + slot + 1);
	uint64_t n = before + in_chapter;
	return n > 0x7FFFFFFF ? 0x7FFFFFFF : (int)n;
}

int reader_book_pages(const Reader *r) {
	if (!r->open || !r->chapter_pages) return 1;
	uint64_t before, total;
	book_counts(r, &before, &total);
	int here = reader_book_page(r, r->view_count > 0 ? r->view_count - 1 : 0);
	return total < (uint64_t)here ? here : (total > 0x7FFFFFFF ? 0x7FFFFFFF : (int)total);
}

// ---- Position, bookmarks and notices ----

bool reader_goto(Reader *r, int chapter, uint32_t offset) {
	if (!r->open || chapter < 0 || chapter >= r->book.chapter_count) return false;

	uint32_t base = r->text.base;
	// An offset at the very end of the window is the start of the next one, if there is a next one.
	bool inside = offset >= base && (offset < base + r->text.len || (offset == base + r->text.len && !ct_has_next(&r->text)));
	if (chapter == r->chapter && inside) {
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

static void copy_cut(char *dst, size_t cap, const char *src, int max) {
	size_t n = src ? strlen(src) : 0;
	if ((int)n > max) n = (size_t)(max < 0 ? 0 : max);
	if (n >= cap) n = cap - 1;
	if (src && n > 0) memcpy(dst, src, n);
	dst[n] = '\0';
}

void reader_status_fit(const char *left, const char *right, int cols, bool prefer_left, char *out_left,
                       char *out_right, size_t cap) {
	int ln = left ? (int)strlen(left) : 0, rn = right ? (int)strlen(right) : 0;

	if (ln + 1 + rn <= cols || ln == 0 || rn == 0) {
		copy_cut(out_left, cap, left, cols);
		copy_cut(out_right, cap, right, cols - (ln > 0 ? ln + 1 : 0));
		return;
	}
	if (prefer_left) {
		copy_cut(out_left, cap, left, cols);
		out_right[0] = '\0';
	} else {
		out_left[0] = '\0';
		copy_cut(out_right, cap, right, cols);
	}
}

bool reader_book_pages_exact(const Reader *r) {
	if (!r->open || !r->chapter_pages) return false;
	for (int i = 0; i < r->book.chapter_count; i++) {
		if (r->chapter_pages[i] == 0) return false;
	}
	return true;
}

// ---- Counting the rest of the book ----

// The pages of a window of text, laid out like paginate does.
static int count_window(const Reader *r, const ChapterText *ct) {
	PageStart s = layout_first_page();
	int n = 0;
	while (n < READER_PAGES_MAX) {
		n++;
		layout_page(ct->buf, ct->len, s, r->cols, r->rows, &r->style, &scratch);
		if (scratch.at_end) break;
		s = scratch.next;
	}
	return n;
}

// The chapter to count next: the first unknown one before the current chapter, else the first after it.
static int next_unknown(const Reader *r) {
	for (int i = 0; i < r->book.chapter_count; i++) {
		if (r->chapter_pages[i] == 0 && i < r->chapter) return i;
	}
	for (int i = r->chapter + 1; i < r->book.chapter_count; i++) {
		if (r->chapter_pages[i] == 0) return i;
	}
	// The current chapter itself when it is a long one that has not been seen whole.
	return r->chapter_pages[r->chapter] == 0 ? r->chapter : -1;
}

bool reader_count_step(Reader *r) {
	if (!r->open || !r->chapter_pages) return false;

	if (!counter_open) {
		int c = next_unknown(r);
		if (c < 0) return false;
		if (!has_text(r, c)) return true; // marked as having no text
		memset(&counter, 0, sizeof counter);
		if (!ct_open(&counter, &r->book, c, 0)) {
			r->chapter_pages[c] = READER_NO_TEXT; // it cannot be read, so it counts no pages (and is not tried again)
			r->pages_changed = true;
			return true;
		}
		counter_open = true;
		counter_chapter = c;
		counter_sum = 0;
	} else if (!ct_next_window(&counter)) {
		counter_close(); // it could not go on: start this chapter again next time
		return true;
	}

	counter_sum += (uint32_t)count_window(r, &counter);
	if (!ct_has_next(&counter)) {
		uint16_t n = counter_sum >= READER_NO_TEXT ? READER_NO_TEXT - 1 : (uint16_t)counter_sum;
		if (n == 0) n = 1;
		r->chapter_pages[counter_chapter] = n;
		r->pages_changed = true;
		counter_close();
	}
	return true;
}

bool reader_goto_book_page(Reader *r, int page) {
	if (!r->open || !reader_book_pages_exact(r)) return false;

	uint64_t total = 0;
	for (int i = 0; i < r->book.chapter_count; i++) total += r->chapter_pages[i] == READER_NO_TEXT ? 0 : r->chapter_pages[i];
	if (total == 0) return false;
	if (page < 1) page = 1;
	if ((uint64_t)page > total) page = (int)total;

	// The chapter that holds the page, and the page's place in it.
	uint64_t before = 0;
	int chapter = -1;
	for (int i = 0; i < r->book.chapter_count; i++) {
		if (r->chapter_pages[i] == READER_NO_TEXT) continue;
		if ((uint64_t)(page - 1) < before + r->chapter_pages[i]) {
			chapter = i;
			break;
		}
		before += r->chapter_pages[i];
	}
	if (chapter < 0) return false;
	int k = (int)((uint64_t)(page - 1) - before);

	if (!load_chapter(r, chapter, 0)) return false;
	// A long chapter is several windows: skip the ones before the page.
	while (k >= r->page_count && ct_has_next(&r->text)) {
		k -= r->page_count;
		ct_next_window(&r->text);
		paginate(r);
	}
	show_page(r, k < r->page_count ? k : r->page_count - 1);
	return true;
}

int reader_export_pages(const Reader *r, uint16_t *out, int cap) {
	if (!r->open || !r->chapter_pages || cap < r->book.chapter_count) return 0;
	int known = 0;
	for (int i = 0; i < r->book.chapter_count; i++) {
		out[i] = r->chapter_pages[i];
		known += out[i] != 0;
	}
	return known > 0 ? r->book.chapter_count : 0;
}

int reader_import_pages(Reader *r, const uint16_t *pages, int count) {
	if (!r->open || !r->chapter_pages || count != r->book.chapter_count) return 0;
	int taken = 0;
	for (int i = 0; i < count; i++) {
		if (r->chapter_pages[i] == 0 && pages[i] != 0) {
			r->chapter_pages[i] = pages[i];
			taken++;
		}
	}
	return taken;
}
