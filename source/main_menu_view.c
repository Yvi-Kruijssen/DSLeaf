// The main menu, drawn like the mockup (decision 053): a paper panel with ruled
// lines for the Recent Reads, and three bevelled buttons. The panel, lines,
// arrow and buttons are a picture in the bitmap layer, the book titles are tile text.
#include "main_menu.h"
#include "paper.h"

#include <string.h>

#define TEXT_END_COL 30 // the titles stop before the panel edge

bool main_menu_draw(MainMenu *m, Screen screen) {
	if (!m->redraw) return false;
	m->redraw = false;

	render_clear(screen);
	paper_begin(screen, MENU_PANEL_H);
	paper_title(screen, "Recent Reads");
	for (int i = 0; i < RECENT_MAX; i++) paper_rule(screen, MENU_BOOK_ROW(i)); // the paper lines

	if (m->count == 0) {
		render_puts(screen, 3, MENU_BOOK_ROW(0), "No recent books", STYLE_DIM);
		render_puts(screen, 3, MENU_BOOK_ROW(1), "Use Library to find one", STYLE_DIM);
	}
	for (int i = 0; i < m->count; i++) {
		bool selected = m->focus == FOCUS_BOOK && m->book == i;
		int row = MENU_BOOK_ROW(i);
		if (selected) paper_arrow(screen, row);
		render_puts_utf8_clip(screen, 3, row, m->title[i], selected ? STYLE_ACCENT : STYLE_NORMAL, TEXT_END_COL);
	}

	bool open_enabled = m->focus == FOCUS_BOOK;
	bool open_pressed = m->press_frames > 0;
	paper_button(screen, MENU_BUTTON_X(0), MENU_BUTTON_Y, MENU_BUTTON_W, MENU_BUTTON_H, "Open", open_pressed, open_pressed, !open_enabled && !open_pressed);
	paper_button(screen, MENU_BUTTON_X(1), MENU_BUTTON_Y, MENU_BUTTON_W, MENU_BUTTON_H, "Library", m->focus == FOCUS_LIBRARY && !open_pressed, false, false);
	paper_button(screen, MENU_BUTTON_X(2), MENU_BUTTON_Y, MENU_BUTTON_W, MENU_BUTTON_H, "Settings", m->focus == FOCUS_SETTINGS && !open_pressed, false, false);
	if (m->notice[0]) render_puts_utf8_clip(screen, 1, 23, m->notice, STYLE_ERROR, 31);
	return true;
}
