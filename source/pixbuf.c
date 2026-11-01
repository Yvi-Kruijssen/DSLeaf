#include "pixbuf.h"

int pix_cols(const Font *font, bool rotated) {
	return (rotated ? PIX_H : PIX_W) / font->width;
}

int pix_rows(const Font *font, bool rotated) {
	return (rotated ? PIX_W : PIX_H) / font->height;
}

void pix_draw_glyph(uint8_t *fb, const Font *font, bool rotated, int col, int row, uint8_t glyph,
                    uint8_t value) {
	int cols = pix_cols(font, rotated), rows = pix_rows(font, rotated);
	if (col < 0 || col >= cols || row < 0 || row >= rows) return;

	int w = font->width, h = font->height;
	const uint8_t *bits = font->glyph[glyph];

	// The cell is cleared first, so a space really erases what was there. This
	// is how overlays such as a menu box cover the page underneath.
	if (!rotated) {
		int cx = (PIX_W - cols * w) / 2 + col * w;
		int cy = (PIX_H - rows * h) / 2 + row * h;
		for (int py = 0; py < h; py++) {
			for (int px = 0; px < w; px++) fb[(cy + py) * PIX_W + cx + px] = 0;
		}
	} else {
		int xs0 = (PIX_W - rows * h) / 2, ys0 = (PIX_H - cols * w) / 2;
		for (int px = 0; px < w; px++) {
			for (int py = 0; py < h; py++) fb[(ys0 + col * w + px) * PIX_W + xs0 + (rows * h - 1 - (row * h + py))] = 0;
		}
	}

	if (!rotated) {
		// The grid is centred, which leaves a few spare pixels around it.
		int x0 = (PIX_W - cols * w) / 2 + col * w;
		int y0 = (PIX_H - rows * h) / 2 + row * h;
		for (int py = 0; py < h; py++) {
			uint8_t *line = fb + (y0 + py) * PIX_W + x0;
			for (int px = 0; px < w; px++) {
				if ((bits[py] >> px) & 1) line[px] = value;
			}
		}
		return;
	}

	// Turned on its side: a pixel at (x, y) on the page lands at screen
	// (page_right - y, x), so the first line of the page is at the right edge.
	int page_w = cols * w, page_h = rows * h;
	int xs = (PIX_W - page_h) / 2;  // screen x of the page's left-most line edge
	int ys = (PIX_H - page_w) / 2;  // screen y of the page's first column
	for (int py = 0; py < h; py++) {
		int page_y = row * h + py;
		int sx = xs + (page_h - 1 - page_y);
		for (int px = 0; px < w; px++) {
			if (!((bits[py] >> px) & 1)) continue;
			fb[(ys + col * w + px) * PIX_W + sx] = value;
		}
	}
}
