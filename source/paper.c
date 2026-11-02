#include "paper.h"

#include <string.h>

// A filled rectangle with rounded corners: `inset` is how far in the first rows start.
static void round_rect(Screen screen, int x, int y, int w, int h, const int *inset, int n, BackdropColour colour) {
	for (int row = 0; row < h; row++) {
		int k = row < n ? inset[row] : (h - 1 - row < n ? inset[h - 1 - row] : 0);
		render_backdrop_rect(screen, x + k, y + row, w - 2 * k, 1, colour);
	}
}

static const int corner_outer[] = { 3, 1, 1 };
static const int corner_inner[] = { 2, 1, 0 };
static const int corner_btn[] = { 4, 2, 1, 1 };   // the outline of a button
static const int corner_face[] = { 3, 1, 0, 0 };  // inside it

void paper_begin(Screen screen, int height) {
	render_backdrop_begin(screen);
	round_rect(screen, PAPER_PANEL_X, PAPER_PANEL_Y, PAPER_PANEL_W, height, corner_outer, 3, BD_EDGE);
	round_rect(screen, PAPER_PANEL_X + 1, PAPER_PANEL_Y + 1, PAPER_PANEL_W - 2, height - 2, corner_inner, 3, BD_PANEL);
}

void paper_title(Screen screen, const char *title) {
	render_backdrop_text(screen, (256 - (int)strlen(title) * 8) / 2, PAPER_TITLE_Y, title, BD_ACCENT);
	render_backdrop_rect(screen, PAPER_RULE_X, PAPER_TITLE_LINE_Y, PAPER_RULE_W, 2, BD_ACCENT);
}

void paper_rule(Screen screen, int row) {
	render_backdrop_rect(screen, PAPER_RULE_X, row * 8 + 11, PAPER_RULE_W, 1, BD_RULE);
}

void paper_band(Screen screen, int row) {
	render_backdrop_rect(screen, PAPER_RULE_X - 4, row * 8 - 1, PAPER_RULE_W + 8, 10, BD_RULE);
}

void paper_arrow(Screen screen, int row) {
	int mid = row * 8 + 4;
	for (int dy = -4; dy <= 4; dy++) {
		int w = 5 - (dy < 0 ? -dy : dy);
		render_backdrop_rect(screen, PAPER_PANEL_X + 6, mid + dy, w + 1, 1, BD_ACCENT);
	}
}

// A button like the mockup's: soft rounded corners, a thin outline, a light top edge and a
// soft shadow under it.
void paper_button(Screen screen, int x, int y, int w, int h, const char *label, bool focused, bool pressed,
                  bool disabled) {
	round_rect(screen, x, y, w, h, corner_btn, 4, focused ? BD_ACCENT : BD_BTN_EDGE);
	round_rect(screen, x + 1, y + 1, w - 2, h - 2, corner_face, 4, BD_BTN_SHADE);
	BackdropColour face = focused || pressed ? BD_BTN_FOCUS : BD_BTN;
	round_rect(screen, x + 1, y + 1, w - 2, pressed ? h - 2 : h - 4, corner_face, 4, face);
	if (!pressed) render_backdrop_rect(screen, x + 4, y + 1, w - 8, 1, BD_BTN_LIGHT);

	int len = (int)strlen(label);
	BackdropColour ink = disabled ? BD_BTN_DIM : (focused ? BD_BTN_HI : BD_BTN_TEXT);
	render_backdrop_text(screen, x + (w - len * 8) / 2, y + (h - 8) / 2 + (pressed ? 0 : -1), label, ink);
}

void paper_hint(Screen screen, int x, int y, const char *text) {
	render_backdrop_text(screen, x, y, text, BD_HINT);
}
