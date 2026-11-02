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

static bool page_starred(const Reader *r, int slot, const BookMarks *marks) {
	return marks && reader_page_marked(r, slot, marks);
}

// The status row (decision 054): the bookmark icon first when the page has a bookmark (a
// glyph of the reading font), then a text at the left, and a text at the right.
static void draw_status(const Reader *r, Screen screen, const char *left, const char *right, bool starred) {
	int row = r->rows; // the row below the text
	int margin = margin_of(r);
	int icon = starred ? 3 : 0; // the icon (two cells) and a space

	char l[48], rt[48];
	reader_status_fit(left ? left : "", right, r->cols - icon, r->notice[0] != '\0', l, rt, sizeof l);
	if (starred) {
		render_putc(screen, margin, row, FONT_GLYPH_BOOKMARK, STYLE_HIGHLIGHT);
		render_putc(screen, margin + 1, row, FONT_GLYPH_BOOKMARK_2, STYLE_HIGHLIGHT);
	}
	if (l[0]) render_puts(screen, margin + icon, row, l, STYLE_DIM);
	if (rt[0]) render_puts(screen, margin + r->cols - (int)strlen(rt), row, rt, STYLE_DIM);
}

bool reader_draw(Reader *r, bool dual, const BookMarks *marks, FooterInfo footer) {
	if (!r->redraw) return false;
	r->redraw = false;

	// Page numbers count the whole book (decision 054). Until every chapter has been counted the
	// total is not known, and "calculating..." is shown instead of a number that keeps changing (decision 056).
	char percent[8], page0[24], page1[24], right[40];
	bool known = reader_book_pages_exact(r);
	int total = reader_book_pages(r);
	if (known) {
		snprintf(page0, sizeof page0, "%d / %d", reader_book_page(r, 0), total);
		snprintf(page1, sizeof page1, "%d / %d", reader_book_page(r, 1), total);
	} else {
		snprintf(page0, sizeof page0, "calculating...");
		snprintf(page1, sizeof page1, "calculating...");
	}
	snprintf(percent, sizeof percent, "%d%%", reader_percent(r));
	const char *pct = known ? percent : NULL; // hidden while calculating, like the page count (decision 056)
	const char *notice = r->notice[0] ? r->notice : NULL; // a notice takes the place of the left text for a moment

	if (!dual) {
		draw_page(r, &r->view[0], SCREEN_BOTTOM);
		reader_footer_text(right, sizeof right, footer, page0, pct);
		draw_status(r, SCREEN_BOTTOM, notice, right, page_starred(r, 0, marks));
		return true;
	}

	// Two screens: each shows the number of its own page, and the percentage is on the bottom one, at the right
	// like in single-screen mode.
	draw_page(r, &r->view[0], SCREEN_TOP);
	reader_footer_text(right, sizeof right, footer, page0, NULL);
	draw_status(r, SCREEN_TOP, notice, right, page_starred(r, 0, marks));

	render_clear(SCREEN_BOTTOM);
	if (r->view_count > 1) {
		draw_page(r, &r->view[1], SCREEN_BOTTOM);
		reader_footer_text(right, sizeof right, footer, page1, pct);
		draw_status(r, SCREEN_BOTTOM, NULL, right, page_starred(r, 1, marks));
	} else {
		reader_footer_text(right, sizeof right, footer, NULL, pct); // the last page of an odd number of pages
		draw_status(r, SCREEN_BOTTOM, NULL, right, false);
	}
	return true;
}
