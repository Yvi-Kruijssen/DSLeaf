#include <stdio.h>
#include <string.h>

#include "reader.h"

// The empty columns at each side of the text: whatever the grid has left over.
static int margin_of(const Reader *r) {
	return (render_cols() - r->cols) / 2;
}

static void draw_page(const Reader *r, const Page *page, Screen screen) {
	int margin = margin_of(r);
	render_clear(screen);
	for (int l = 0; l < page->line_count; l++) {
		const LayoutLine *line = &page->line[l];
		for (int k = 0; k < line->len; k++) {
			render_putc(screen, margin + k, l, line->glyph[k], STYLE_NORMAL);
		}
	}
}

// A star in the top right margin of a page that has a bookmark. With no margin
// there is no room, so the status row gets the star instead (see draw_status).
static bool page_starred(const Reader *r, int slot, const BookMarks *marks) {
	return marks && reader_page_marked(r, slot, marks);
}

static void draw_mark(const Reader *r, int slot, Screen screen, const BookMarks *marks) {
	int margin = margin_of(r);
	if (margin > 0 && page_starred(r, slot, marks)) {
		render_putc(screen, margin + r->cols, 0, '*', STYLE_HIGHLIGHT);
	}
}

// Text at the left and right of the status row.
static void draw_status(const Reader *r, Screen screen, const char *left, const char *right, bool starred) {
	int row = r->rows; // the row below the text
	int margin = margin_of(r);
	if (left) {
		char text[48];
		snprintf(text, sizeof text, "%s%s", starred && margin == 0 ? "*" : "", left);
		render_puts(screen, margin, row, text, STYLE_DIM);
	}
	if (right) {
		int col = margin + r->cols - (int)strlen(right);
		render_puts(screen, col, row, right, STYLE_DIM);
	}
}

bool reader_draw(Reader *r, bool dual, const BookMarks *marks) {
	if (!r->redraw) return false;
	r->redraw = false;

	char chapter[24], percent[8], page[24];
	snprintf(chapter, sizeof chapter, "Ch %d/%d", reader_chapter_number(r), reader_chapter_total(r));
	const char *left = r->notice[0] ? r->notice : chapter; // a notice replaces the chapter for a moment
	snprintf(percent, sizeof percent, "%d%%", reader_percent(r));

	if (!dual) {
		draw_page(r, &r->view[0], SCREEN_BOTTOM);
		draw_mark(r, 0, SCREEN_BOTTOM, marks);
		snprintf(page, sizeof page, "p%d/%d %s", reader_page_number(r, 0), reader_page_total(r), percent);
		draw_status(r, SCREEN_BOTTOM, left, page, page_starred(r, 0, marks));
		return true;
	}

	draw_page(r, &r->view[0], SCREEN_TOP);
	draw_mark(r, 0, SCREEN_TOP, marks);
	snprintf(page, sizeof page, "p%d/%d", reader_page_number(r, 0), reader_page_total(r));
	draw_status(r, SCREEN_TOP, left, page, page_starred(r, 0, marks));

	render_clear(SCREEN_BOTTOM);
	if (r->view_count > 1) {
		draw_page(r, &r->view[1], SCREEN_BOTTOM);
		draw_mark(r, 1, SCREEN_BOTTOM, marks);
		snprintf(page, sizeof page, "p%d/%d", reader_page_number(r, 1), reader_page_total(r));
		draw_status(r, SCREEN_BOTTOM, percent, page, page_starred(r, 1, marks));
	} else {
		draw_status(r, SCREEN_BOTTOM, percent, NULL, false); // the last page of an odd number of pages
	}
	return true;
}
