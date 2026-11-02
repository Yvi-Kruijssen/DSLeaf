#include "pixbuf.h"

// Distance between two lines: the glyph height plus the extra gap.
static int pitch(const Font *font) {
	int gap = font->line_gap;
	return font->height + (gap > 0 ? gap : 0);
}

// The pixels kept free at the top and bottom of the screen (decision 062), at most 40.
static int safe(const Font *font) {
	return font->safe < 0 ? 0 : (font->safe > 40 ? 40 : font->safe);
}

// The height of the screen the page may use, and where it starts.
static int avail(const Font *font) { return PIX_H - 2 * safe(font); }

int pix_cols(const Font *font, bool rotated) {
	return rotated ? avail(font) / font->width : PIX_W / font->width;
}

int pix_rows(const Font *font, bool rotated) {
	return rotated ? PIX_W / pitch(font) : avail(font) / pitch(font);
}

void pix_draw_glyph(uint8_t *fb, const Font *font, bool rotated, int col, int row, uint8_t glyph,
                    uint8_t value) {
	int cols = pix_cols(font, rotated), rows = pix_rows(font, rotated);
	if (col < 0 || col >= cols || row < 0 || row >= rows) return;

	int w = font->width, h = font->height, p = pitch(font);
	const uint8_t *bits = font->glyph[glyph];

	// The cell is cleared first, so a space really erases what was there. This
	// is how overlays such as a menu box cover the page underneath.
	if (!rotated) {
		int cx = (PIX_W - cols * w) / 2 + col * w;
		int cy = safe(font) + (avail(font) - rows * p) / 2 + row * p;
		for (int py = 0; py < p; py++) {
			for (int px = 0; px < w; px++) fb[(cy + py) * PIX_W + cx + px] = 0;
		}
	} else {
		int xs0 = (PIX_W - rows * p) / 2, ys0 = safe(font) + (avail(font) - cols * w) / 2;
		for (int px = 0; px < w; px++) {
			for (int py = 0; py < p; py++) fb[(ys0 + col * w + px) * PIX_W + xs0 + (rows * p - 1 - (row * p + py))] = 0;
		}
	}

	if (!rotated) {
		// The grid is centred, which leaves a few spare pixels around it.
		int x0 = (PIX_W - cols * w) / 2 + col * w;
		int y0 = safe(font) + (avail(font) - rows * p) / 2 + row * p;
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
	int page_w = cols * w, page_h = rows * p;
	int xs = (PIX_W - page_h) / 2;  // screen x of the page's left-most line edge
	int ys = safe(font) + (avail(font) - page_w) / 2;  // screen y of the page's first column
	for (int py = 0; py < h; py++) {
		int page_y = row * p + py;
		int sx = xs + (page_h - 1 - page_y);
		for (int px = 0; px < w; px++) {
			if (!((bits[py] >> px) & 1)) continue;
			fb[(ys + col * w + px) * PIX_W + sx] = value;
		}
	}
}
