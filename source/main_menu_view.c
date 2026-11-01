// The main menu, drawn like the mockup (decision 053): a paper panel with ruled
// lines for the Recent Reads, and three bevelled buttons. The panel, lines,
// arrow and buttons are a picture in the bitmap layer, the book titles are tile text.
#include "main_menu.h"

#include <string.h>

#define TEXT_END_COL 30 // the titles stop before the panel edge
#define PANEL_X 4
#define PANEL_W 248
#define RULE_X 12
#define RULE_W 232

// A filled rectangle with rounded corners: `inset` is how far in the first rows start.
static void round_rect(Screen screen, int x, int y, int w, int h, const int *inset, int n, BackdropColour colour) {
	for (int row = 0; row < h; row++) {
		int k = row < n ? inset[row] : (h - 1 - row < n ? inset[h - 1 - row] : 0);
		render_backdrop_rect(screen, x + k, y + row, w - 2 * k, 1, colour);
	}
}

static const int corner_outer[] = { 3, 1, 1 };
static const int corner_inner[] = { 2, 1, 0 };
static const int corner_btn[] = { 4, 2, 1, 1 };    // the outline of a button
static const int corner_face[] = { 3, 1, 0, 0 };   // inside it

static void draw_panel(Screen screen) {
	round_rect(screen, PANEL_X, MENU_PANEL_Y, PANEL_W, MENU_PANEL_H, corner_outer, 3, BD_EDGE);
	round_rect(screen, PANEL_X + 1, MENU_PANEL_Y + 1, PANEL_W - 2, MENU_PANEL_H - 2, corner_inner, 3, BD_PANEL);

	// The title line, and the paper lines: each sits just below the text of its book.
	render_backdrop_rect(screen, RULE_X, 20, RULE_W, 2, BD_ACCENT);
	for (int i = 0; i < RECENT_MAX; i++) {
		render_backdrop_rect(screen, RULE_X, MENU_BOOK_ROW(i) * 8 + 11, RULE_W, 1, BD_RULE);
	}
}

// The green arrow in front of the book with the cursor.
static void draw_arrow(Screen screen, int row) {
	int mid = row * 8 + 4;
	for (int dy = -4; dy <= 4; dy++) {
		int w = 5 - (dy < 0 ? -dy : dy);
		render_backdrop_rect(screen, 10, mid + dy, w + 1, 1, BD_ACCENT);
	}
}

// A button like the mockup's: soft rounded corners, a thin outline, a light top edge and a
// soft shadow under it. `focused` has the green outline and a lighter face, `pressed` is
// pushed in (no shadow, the label moves down a pixel).
static void draw_button(Screen screen, int index, const char *label, bool focused, bool pressed, bool disabled) {
	int x = MENU_BUTTON_X(index), y = MENU_BUTTON_Y, w = MENU_BUTTON_W, h = MENU_BUTTON_H;

	round_rect(screen, x, y, w, h, corner_btn, 4, focused ? BD_ACCENT : BD_BTN_EDGE);
	round_rect(screen, x + 1, y + 1, w - 2, h - 2, corner_face, 4, BD_BTN_SHADE);
	BackdropColour face = focused || pressed ? BD_BTN_FOCUS : BD_BTN;
	round_rect(screen, x + 1, y + 1, w - 2, pressed ? h - 2 : h - 4, corner_face, 4, face);
	if (!pressed) render_backdrop_rect(screen, x + 4, y + 1, w - 8, 1, BD_BTN_LIGHT);

	int len = (int)strlen(label);
	BackdropColour ink = disabled ? BD_BTN_DIM : (focused ? BD_BTN_HI : BD_BTN_TEXT);
	render_backdrop_text(screen, x + (w - len * 8) / 2, y + (h - 8) / 2 + (pressed ? 0 : -1), label, ink);
}

bool main_menu_draw(MainMenu *m, Screen screen) {
	if (!m->redraw) return false;
	m->redraw = false;

	render_clear(screen);
	render_backdrop_begin(screen);
	draw_panel(screen);

	const char *title = "Recent Reads";
	render_puts(screen, (32 - (int)strlen(title)) / 2, MENU_TITLE_ROW, title, STYLE_ACCENT);

	if (m->count == 0) {
		render_puts(screen, 3, MENU_BOOK_ROW(0), "No recent books", STYLE_DIM);
		render_puts(screen, 3, MENU_BOOK_ROW(1), "Use Library to find one", STYLE_DIM);
	}
	for (int i = 0; i < m->count; i++) {
		bool selected = m->focus == FOCUS_BOOK && m->book == i;
		int row = MENU_BOOK_ROW(i);
		if (selected) draw_arrow(screen, row);
		render_puts_utf8_clip(screen, 3, row, m->title[i], selected ? STYLE_ACCENT : STYLE_NORMAL, TEXT_END_COL);
	}

	bool open_enabled = m->focus == FOCUS_BOOK;
	bool open_pressed = m->press_frames > 0;
	draw_button(screen, 0, "Open", open_pressed, open_pressed, !open_enabled && !open_pressed);
	draw_button(screen, 1, "Library", m->focus == FOCUS_LIBRARY && !open_pressed, false, false);
	draw_button(screen, 2, "Settings", m->focus == FOCUS_SETTINGS && !open_pressed, false, false);
	if (m->notice[0]) render_puts_utf8_clip(screen, 1, 23, m->notice, STYLE_ERROR, 31);
	return true;
}
