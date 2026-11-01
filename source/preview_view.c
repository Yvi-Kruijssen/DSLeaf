#include <string.h>

#include "preview.h"

static void draw_page(const Preview *p, int index, Screen screen) {
	int margin = (render_cols() - p->cols) / 2;
	const Page *page = &p->page[index];
	render_clear(screen);
	for (int l = 0; l < page->line_count; l++) {
		for (int k = 0; k < page->line[l].len; k++) render_putc(screen, margin + k, l, page->line[l].glyph[k], STYLE_NORMAL);
	}
	// The status row, like the reader's. It is left out if the page is too narrow for it.
	if (p->cols >= 12) {
		render_puts(screen, margin, p->rows, "Preview", STYLE_DIM);
		char num[4] = { 'p', (char)('1' + index), '\0' };
		render_puts(screen, margin + p->cols - 2, p->rows, num, STYLE_DIM);
	}
}

void preview_draw(const Preview *p) {
	if (p->pages == 2) {
		draw_page(p, 0, SCREEN_TOP);
		draw_page(p, 1, SCREEN_BOTTOM);
	} else {
		draw_page(p, 0, SCREEN_BOTTOM);
	}
}
