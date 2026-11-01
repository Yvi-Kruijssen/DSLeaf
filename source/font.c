#include "font.h"

#include <string.h>

bool font_is_tile(const Font *font) {
	return font->width == 8 && (font->height == 8 || font->height == 16);
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
