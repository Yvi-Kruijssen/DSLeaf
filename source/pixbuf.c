#include "pixbuf.h"

#include "cjk.h"
#include "font_ext.h"

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

// What to draw for a glyph code: its rows (bit 0 the leftmost pixel, one 16 bit number for each row), how many pixels wide
// and high it is, how many cells wide it is and how many pixel rows from the top of the cell it starts. A Latin-1 glyph
// is the font's; for the built-in 6x12 font a Greek or Cyrillic letter is too; a wide character (decision 092) comes from
// the CJK font and takes two cells. A letter the font has no glyph for is shown as a question mark.
typedef struct {
	uint16_t rows[16];
	int width, height, cells, top;
} GlyphBits;

static void glyph_bits(const Font *font, Glyph glyph, GlyphBits *g) {
	g->width = font->width;
	g->height = font->height;
	g->cells = 1;
	g->top = 0;
	if (glyph >= FONT_GLYPHS && cjk_is_wide(glyph) && cjk_loaded() && cjk_rows(glyph, g->rows)) {
		g->width = cjk_width() < 2 * font->width ? cjk_width() : 2 * font->width;
		g->height = cjk_height() < font->height ? cjk_height() : font->height;
		g->cells = 2;
		g->top = (font->height - g->height) / 2;
		return;
	}
	const uint8_t *bytes;
	if (glyph < FONT_GLYPHS) {
		bytes = font->glyph[glyph];
	} else {
		bytes = font->has_ext ? font_ext_rows_of(glyph) : NULL;
		if (!bytes) bytes = font->glyph['?'];
	}
	for (int y = 0; y < 16; y++) g->rows[y] = bytes[y];
}

void pix_draw_glyph(uint8_t *fb, const Font *font, bool rotated, int col, int row, Glyph glyph,
                    uint8_t value) {
	int cols = pix_cols(font, rotated), rows = pix_rows(font, rotated);
	if (col < 0 || col >= cols || row < 0 || row >= rows || glyph == GLYPH_FILL) return;

	int w = font->width, p = pitch(font);
	GlyphBits g;
	glyph_bits(font, glyph, &g);
	int span = g.cells;
	if (col + span > cols) span = cols - col; // a wide character at the end of a line loses its right half
	int cw = w * span; // pixels wide of the cells the glyph covers

	// The cells are cleared first, so a space really erases what was there. This
	// is how overlays such as a menu box cover the page underneath.
	if (!rotated) {
		int cx = (PIX_W - cols * w) / 2 + col * w;
		int cy = safe(font) + (avail(font) - rows * p) / 2 + row * p;
		for (int py = 0; py < p; py++) {
			for (int px = 0; px < cw; px++) fb[(cy + py) * PIX_W + cx + px] = 0;
		}
	} else {
		int xs0 = (PIX_W - rows * p) / 2, ys0 = safe(font) + (avail(font) - cols * w) / 2;
		for (int px = 0; px < cw; px++) {
			for (int py = 0; py < p; py++) fb[(ys0 + col * w + px) * PIX_W + xs0 + (rows * p - 1 - (row * p + py))] = 0;
		}
	}

	int gw = g.width < cw ? g.width : cw;
	if (!rotated) {
		// The grid is centred, which leaves a few spare pixels around it.
		int x0 = (PIX_W - cols * w) / 2 + col * w;
		int y0 = safe(font) + (avail(font) - rows * p) / 2 + row * p;
		for (int py = 0; py < g.height; py++) {
			uint8_t *line = fb + (y0 + g.top + py) * PIX_W + x0;
			for (int px = 0; px < gw; px++) {
				if ((g.rows[py] >> px) & 1) line[px] = value;
			}
		}
		return;
	}

	// Turned on its side: a pixel at (x, y) on the page lands at screen
	// (page_right - y, x), so the first line of the page is at the right edge.
	int page_w = cols * w, page_h = rows * p;
	int xs = (PIX_W - page_h) / 2;  // screen x of the page's left-most line edge
	int ys = safe(font) + (avail(font) - page_w) / 2;  // screen y of the page's first column
	for (int py = 0; py < g.height; py++) {
		int page_y = row * p + g.top + py;
		int sx = xs + (page_h - 1 - page_y);
		for (int px = 0; px < gw; px++) {
			if (!((g.rows[py] >> px) & 1)) continue;
			fb[(ys + col * w + px) * PIX_W + sx] = value;
		}
	}
}

void pix_page_origin(const Font *font, bool rotated, int *x, int *y, int *w, int *h, int *row_h) {
	int cols = pix_cols(font, rotated), rows = pix_rows(font, rotated);
	int pw = cols * font->width, ph = rows * pitch(font);
	*w = pw;
	*h = ph;
	*row_h = pitch(font);
	if (!rotated) {
		*x = (PIX_W - pw) / 2;
		*y = safe(font) + (avail(font) - ph) / 2;
	} else {
		*x = safe(font) + (avail(font) - pw) / 2;
		*y = (PIX_W - ph) / 2;
	}
}
