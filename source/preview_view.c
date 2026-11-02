#include <stdio.h>
#include <string.h>

#include "preview.h"
#include "reader.h"

// Made-up numbers for the status row: pages 123 and 124 of 1234, 10 per cent.
static void draw_page(const Preview *p, int index, Screen screen, FooterInfo footer, bool show_percent) {
	int margin = (render_cols() - p->cols) / 2;
	const Page *page = &p->page[index];
	render_clear(screen);
	for (int l = 0; l < page->line_count; l++) {
		for (int k = 0; k < page->line[l].len; k++) render_putc(screen, margin + k, l, page->line[l].glyph[k], STYLE_NORMAL);
	}

	// The status row as the reader draws it: the page number and/or the percentage at the right.
	char page_text[16], right[40], l[48], r[48];
	snprintf(page_text, sizeof page_text, "%d / 1234", 123 + index);
	reader_footer_text(right, sizeof right, footer, page_text, show_percent ? "10%" : NULL);
	reader_status_fit("", right, p->cols, false, l, r, sizeof l);
	if (r[0]) render_puts(screen, margin + p->cols - (int)strlen(r), p->rows, r, STYLE_DIM);
}

void preview_draw(const Preview *p, FooterInfo footer) {
	if (p->pages == 2) {
		draw_page(p, 0, SCREEN_TOP, footer, false);       // the top screen has the page number only,
		draw_page(p, 1, SCREEN_BOTTOM, footer, true);     // the bottom one the percentage as well, like when reading
	} else {
		draw_page(p, 0, SCREEN_BOTTOM, footer, true);
	}
}
