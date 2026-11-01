// 8x8 bitmap fonts (docs: decisions 009, 011, 013, 025).
#ifndef DSLEAF_FONT_H
#define DSLEAF_FONT_H

#include <stdbool.h>
#include <stdint.h>

#define FONT_GLYPHS 256

// 256 glyphs indexed by Unicode code point U+0000..U+00FF (Latin-1).
// Each glyph is 8 rows, top to bottom. Bit 0 of a row is the leftmost pixel,
// the same convention as font8x8.
typedef struct {
	uint8_t glyph[FONT_GLYPHS][8];
} Font;

// Fills `font` with the built-in font8x8 (public domain).
void font_load_default(Font *font);

// Converts one glyph to a 4bpp DS tile (8 rows of 8 pixels, pixel 0 in the
// low nibble). Set pixels become colour index 1, clear pixels index 0.
// With `rotated`, the glyph is turned so that text reads upright when the
// device is held rotated 90 degrees counter-clockwise (decision 027).
void font_glyph_to_tile(const uint8_t glyph[8], bool rotated, uint32_t tile[8]);

#endif
