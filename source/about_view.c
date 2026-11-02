// The About screen on paper (decision 077): the panel and title of the other menus, the text in
// 28 columns, headings in the accent colour, and the page at the bottom right.
#include <stdio.h>
#include <string.h>

#include "about.h"
#include "paper.h"

#define PANEL_H 149
#define FIRST_ROW 5
#define TEXT_COL 2
#define HINT_Y 167

bool about_draw(About *a, Screen screen) {
	if (!a->redraw) return false;
	a->redraw = false;

	render_clear(screen);
	paper_begin(screen, PANEL_H);
	paper_title(screen, "About");

	for (int i = 0; i < ABOUT_ROWS; i++) {
		char text[ABOUT_COLS + 1];
		bool heading;
		about_line(a, a->top + i, text, &heading);
		render_puts(screen, TEXT_COL, FIRST_ROW + i, text, heading ? STYLE_ACCENT : STYLE_NORMAL);
	}
	char page[16];
	snprintf(page, sizeof page, "%d/%d", about_page(a), about_pages(a));
	render_puts(screen, 30 - (int)strlen(page), FIRST_ROW + ABOUT_ROWS, page, STYLE_DIM);

	paper_hint(screen, 12, HINT_Y, "Up/Down scroll  B back");
	return true;
}
