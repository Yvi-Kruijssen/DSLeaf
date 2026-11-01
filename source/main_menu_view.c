#include "main_menu.h"

#include <string.h>

#define TEXT_END_COL 31 // leave a one column margin on the right

static void draw_button(Screen screen, int col, const char *label, int width, bool focused,
                        bool disabled) {
	// "[Library]" normally, ">Library<" when focused or pressed. The label is
	// centred in the space between the two marks.
	int inner = width - 2;
	int len = (int)strlen(label);
	int pad_left = (inner - len) / 2;

	Style style = disabled ? STYLE_DIM : (focused ? STYLE_HIGHLIGHT : STYLE_NORMAL);
	render_putc(screen, col, MENU_BUTTON_ROW, focused ? '>' : '[', style);
	for (int i = 0; i < inner; i++) {
		int k = i - pad_left;
		render_putc(screen, col + 1 + i, MENU_BUTTON_ROW, (k >= 0 && k < len) ? label[k] : ' ', style);
	}
	render_putc(screen, col + width - 1, MENU_BUTTON_ROW, focused ? '<' : ']', style);
}

bool main_menu_draw(MainMenu *m, Screen screen) {
	if (!m->redraw) return false;
	m->redraw = false;

	render_clear(screen);

	const char *title = "Recent Reads";
	render_puts(screen, (32 - (int)strlen(title)) / 2, MENU_TITLE_ROW, title, STYLE_HIGHLIGHT);
	for (int c = 1; c < 31; c++) render_putc(screen, c, MENU_TITLE_ROW + 1, '-', STYLE_HIGHLIGHT);

	if (m->count == 0) {
		render_puts(screen, 2, MENU_BOOK_ROW(0), "No recent books", STYLE_DIM);
		render_puts(screen, 2, MENU_BOOK_ROW(1), "Use Library to find one", STYLE_DIM);
	}
	for (int i = 0; i < m->count; i++) {
		bool selected = m->focus == FOCUS_BOOK && m->book == i;
		int row = MENU_BOOK_ROW(i);
		if (selected) render_putc(screen, 1, row, '>', STYLE_HIGHLIGHT);
		render_puts_utf8_clip(screen, 3, row, m->title[i],
		                      selected ? STYLE_HIGHLIGHT : STYLE_NORMAL, TEXT_END_COL);
	}

	bool open_enabled = m->focus == FOCUS_BOOK;
	bool open_pressed = m->press_frames > 0;
	draw_button(screen, MENU_OPEN_COL, "Open", MENU_OPEN_W, open_pressed, !open_enabled && !open_pressed);
	draw_button(screen, MENU_LIBRARY_COL, "Library", MENU_LIBRARY_W,
	            m->focus == FOCUS_LIBRARY && !open_pressed, false);
	draw_button(screen, MENU_SETTINGS_COL, "Settings", MENU_SETTINGS_W,
	            m->focus == FOCUS_SETTINGS && !open_pressed, false);
	if (m->notice[0]) render_puts_utf8_clip(screen, 1, 23, m->notice, STYLE_ERROR, TEXT_END_COL);
	return true;
}
