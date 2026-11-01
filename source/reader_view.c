#include <stdio.h>
#include <string.h>

#include "reader.h"

static void draw_page(const Page *page, Screen screen) {
	render_clear(screen);
	for (int l = 0; l < page->line_count; l++) {
		const LayoutLine *line = &page->line[l];
		for (int k = 0; k < line->len; k++) {
			render_putc(screen, READER_MARGIN + k, l, line->glyph[k], STYLE_NORMAL);
		}
	}
}

// Text at the left and right of the status row.
static void draw_status(const Reader *r, Screen screen, const char *left, const char *right) {
	int row = r->rows; // the row below the text
	if (left) render_puts(screen, READER_MARGIN, row, left, STYLE_DIM);
	if (right) {
		int col = READER_MARGIN + r->cols - (int)strlen(right);
		render_puts(screen, col, row, right, STYLE_DIM);
	}
}

bool reader_draw(Reader *r, bool dual) {
	if (!r->redraw) return false;
	r->redraw = false;

	char chapter[24], percent[8], page[24];
	snprintf(chapter, sizeof chapter, "Ch %d/%d", reader_chapter_number(r), reader_chapter_total(r));
	snprintf(percent, sizeof percent, "%d%%", reader_percent(r));

	if (!dual) {
		draw_page(&r->view[0], SCREEN_BOTTOM);
		snprintf(page, sizeof page, "p%d/%d %s", reader_page_number(r, 0), reader_page_total(r), percent);
		draw_status(r, SCREEN_BOTTOM, chapter, page);
		return true;
	}

	draw_page(&r->view[0], SCREEN_TOP);
	snprintf(page, sizeof page, "p%d/%d", reader_page_number(r, 0), reader_page_total(r));
	draw_status(r, SCREEN_TOP, chapter, page);

	render_clear(SCREEN_BOTTOM);
	if (r->view_count > 1) {
		draw_page(&r->view[1], SCREEN_BOTTOM);
		snprintf(page, sizeof page, "p%d/%d", reader_page_number(r, 1), reader_page_total(r));
		draw_status(r, SCREEN_BOTTOM, percent, page);
	} else {
		draw_status(r, SCREEN_BOTTOM, percent, NULL); // the last page of an odd number of pages
	}
	return true;
}
