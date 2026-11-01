// Renderer demo. B toggles invert, SELECT toggles the rotated grid and
// START exits. Replaced by the real app as it is built.
#include <nds.h>
#include <stdio.h>

#include "render.h"

static Font font;

static void draw_ruler(Screen screen) {
	for (int c = 0; c < render_cols(); c++) {
		render_putc(screen, c, 0, '0' + c % 10, STYLE_DIM);
	}
	for (int r = 1; r < render_rows(); r++) {
		render_putc(screen, 0, r, '0' + r % 10, STYLE_DIM);
	}
}

static void draw_demo(void) {
	char buf[40];

	draw_ruler(SCREEN_TOP);
	render_puts(SCREEN_TOP, 2, 2, "DSLeaf", STYLE_HIGHLIGHT);
	render_puts(SCREEN_TOP, 2, 4, "Normal text", STYLE_NORMAL);
	render_puts(SCREEN_TOP, 2, 5, "Dim text", STYLE_DIM);
	render_puts(SCREEN_TOP, 2, 6, "The Great Gatsby", STYLE_NORMAL);
	render_puts(SCREEN_TOP, 2 + 16, 6, " - error", STYLE_ERROR);
	render_puts_utf8(SCREEN_TOP, 2, 8, "\xE2\x80\x9C" "Curly" "\xE2\x80\x9D \xE2\x80\x93 caf\xC3\xA9\xE2\x80\xA6", STYLE_NORMAL);
	render_puts_utf8(SCREEN_TOP, 2, 9, "\xE4\xB8\xAD unknown -> ?", STYLE_NORMAL);

	snprintf(buf, sizeof buf, "Grid %dx%d", render_cols(), render_rows());
	render_puts(SCREEN_TOP, 2, 11, buf, STYLE_NORMAL);
	render_puts(SCREEN_TOP, 2, 13, "B  invert colours", STYLE_DIM);
	render_puts(SCREEN_TOP, 2, 14, "SELECT  rotate", STYLE_DIM);
	render_puts(SCREEN_TOP, 2, 15, "START  exit", STYLE_DIM);
}

static void draw_table(void) {
	// 16 rows of 16 glyphs, each followed by a space where the grid allows it.
	int cols = render_cols();
	int rows = render_rows();
	for (int g = 0; g < 256; g++) {
		int col = (g % 16) * 2;
		int row = g / 16;
		if (col + 1 < cols && row < rows) {
			render_putc(SCREEN_BOTTOM, col, row, (uint8_t)g, STYLE_NORMAL);
		}
	}
	if (rows > 17) render_puts(SCREEN_BOTTOM, 0, 17, "U+0000 - U+00FF", STYLE_DIM);
}

int main(void) {
	render_init();
	font_load_default(&font);
	render_set_font(&font);

	draw_demo();
	draw_table();

	while (pmMainLoop()) {
		swiWaitForVBlank();
		scanKeys();
		u32 down = keysDown();

		if (down & KEY_START) break;
		if (down & KEY_B) render_set_inverted(!render_is_inverted());
		if (down & KEY_SELECT) {
			render_set_rotated(!render_is_rotated());
			draw_demo();
			draw_table();
		}
	}

	return 0;
}
