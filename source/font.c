#include "font.h"

#include <string.h>

#include "cjk.h"
#include "font_ext.h"
#include "text.h"

int font_safe_tile_rows(const Font *font) {
	int rows = (font->safe + 7) / 8;
	return rows < 0 ? 0 : (rows > 4 ? 4 : rows);
}

bool font_is_tile(const Font *font) {
	return font->width == 8 && (font->height == 8 || font->height == 16);
}

void font_set_bookmark_glyph(Font *font) {
	uint8_t *left = font->glyph[FONT_GLYPH_BOOKMARK], *right = font->glyph[FONT_GLYPH_BOOKMARK_2];
	memset(left, 0, 16);
	memset(right, 0, 16);
	int w = font->width, h = font->height;
	if (w < 3 || h < 4) return;

	// The ribbon is spread over two cells. Its width is odd, so the V cut in its lower edge can end
	// in a single pixel: the cut is as wide as the ribbon less two legs of two pixels in the last row,
	// and narrows by one pixel on each side for every row up (a 45 degree V). A tall font leaves a
	// pixel of space above the ribbon.
	int rw = 2 * w - 3;
	int x0 = 1;
	int top = h >= 12 ? 1 : 0;
	int bottom = h - 1;
	for (int y = top; y <= bottom; y++) {
		int k = bottom - y;                 // rows up from the bottom edge
		int cut = rw - 4 - 2 * k;           // pixels cut out of the middle: the last is a single pixel
		for (int x = 0; x < rw; x++) {
			bool cut_out = cut > 0 && x >= (rw - cut) / 2 && x < (rw - cut) / 2 + cut;
			if (cut_out) continue;
			int px = x0 + x; // 0 .. 2w-1 across the pair of cells
			if (px < w) left[y] |= (uint8_t)(1u << px);
			else right[y] |= (uint8_t)(1u << (px - w));
		}
	}
}

bool font_from_bytes(Font *font, const uint8_t *data, size_t size) {
	int width = 8, height;
	size_t skip = 0;

	if (size >= 8 && memcmp(data, "DSLF", 4) == 0) {
		width = data[5];
		height = data[6];
		if (data[4] != 1 || width < 1 || width > 8 || height < 1 || height > 16) return false;
		if (size != 8 + (size_t)FONT_GLYPHS * (size_t)height) return false;
		skip = 8;
	} else if (size == FONT_GLYPHS * 8) {
		height = 8;
	} else if (size == FONT_GLYPHS * 16) {
		height = 16;
	} else {
		return false;
	}

	memset(font, 0, sizeof *font);
	font->width = width;
	font->height = height;
	for (int g = 0; g < FONT_GLYPHS; g++) {
		memcpy(font->glyph[g], data + skip + (size_t)g * (size_t)height, (size_t)height);
	}
	return true;
}

void font_glyph_to_tile(const uint8_t glyph[8], bool rotated, uint32_t tile[8]) {
	memset(tile, 0, 8 * sizeof(uint32_t));

	for (int py = 0; py < 8; py++) {
		for (int px = 0; px < 8; px++) {
			if (!((glyph[py] >> px) & 1)) continue;

			int x = px, y = py;
			if (rotated) {
				x = 7 - py;
				y = px;
			}
			tile[y] |= 1u << (x * 4);
		}
	}
}

int font_glyph_to_tiles(const Font *font, int glyph, bool rotated, uint32_t out[16]) {
	const uint8_t *rows = font->glyph[glyph];
	if (font->height == 8) {
		font_glyph_to_tile(rows, rotated, out);
		return 1;
	}

	if (!rotated) {
		font_glyph_to_tile(rows, false, out);          // upper half
		font_glyph_to_tile(rows + 8, false, out + 8);  // lower half
	} else {
		// Turned counter-clockwise, the lower half of the glyph is on the left.
		font_glyph_to_tile(rows + 8, true, out);
		font_glyph_to_tile(rows, true, out + 8);
	}
	return 2;
}

const uint8_t *font_ext_rows_of(uint32_t cp) {
	for (int i = 0; i < font_ext_range_count; i++) {
		const FontRange *r = &font_ext_ranges[i];
		if (cp >= r->first && cp < (uint32_t)r->first + r->count) return &font_ext_rows[((size_t)r->offset + (cp - r->first)) * 12];
	}
	return NULL;
}

int font_scripts(const Font *font) {
	int scripts = font->has_ext ? SCRIPT_ALPHABETS : 0;
	// The CJK glyphs are drawn in pixels, two cells wide: they need a pixel font whose cell they fit (decision 092).
	if (cjk_loaded() && !font_is_tile(font) && cjk_height() <= font->height && cjk_width() <= 2 * font->width) scripts |= SCRIPT_CJK;
	return scripts;
}
