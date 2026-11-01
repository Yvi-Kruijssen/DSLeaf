#include "message_box.h"

#define BOX_COL 2
#define BOX_ROW 7
#define BOX_W 28
#define BOX_H 9

static void put_line(Screen screen, int row, const char *text, Style style) {
	render_puts_utf8_clip(screen, BOX_COL + 2, row, text, style, BOX_COL + BOX_W - 2);
}

void message_box_draw(Screen screen, const char *heading, const char *line1, const char *line2) {
	for (int y = 0; y < BOX_H; y++) {
		for (int x = 0; x < BOX_W; x++) {
			bool edge_y = y == 0 || y == BOX_H - 1;
			bool edge_x = x == 0 || x == BOX_W - 1;
			char c = ' ';
			if (edge_y && edge_x) c = '+';
			else if (edge_y) c = '-';
			else if (edge_x) c = '|';
			render_putc(screen, BOX_COL + x, BOX_ROW + y, (uint8_t)c, STYLE_NORMAL);
		}
	}

	put_line(screen, BOX_ROW + 1, heading, STYLE_HIGHLIGHT);
	if (line1) put_line(screen, BOX_ROW + 3, line1, STYLE_NORMAL);
	if (line2) put_line(screen, BOX_ROW + 4, line2, STYLE_ERROR);
	put_line(screen, BOX_ROW + 7, "Press A or B", STYLE_DIM);
}
