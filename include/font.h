// 8x8 bitmap fonts (docs: decisions 009, 011, 013, 025).
#ifndef DSLEAF_FONT_H
#define DSLEAF_FONT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FONT_GLYPHS 256

// 256 glyphs indexed by Unicode code point U+0000..U+00FF (Latin-1). Each glyph
// is `width` pixels wide (1 to 8) and `height` rows high (1 to 16). Rows go from
// the top. Bit 0 of a row is the leftmost pixel, the same convention as font8x8.
//
// Fonts that are 8 wide and 8 or 16 high ("tile fonts") are drawn with the DS
// tile hardware. Any other size is drawn pixel by pixel (decisions 043, 044).
typedef struct {
	int width;
	int height;
	int line_gap; // pixel fonts only: blank pixel rows below every line (decision 050). The loaders leave it 0
	int safe;     // pixels kept free at the top and bottom of the screen on the reading screens (decision 062). The loaders leave it 0
	uint8_t glyph[FONT_GLYPHS][16];
} Font;

// The glyph code that the reader's status row uses for the bookmark icon. No text
// produces it (it is a control character), so it can be redefined freely.
#define FONT_GLYPH_BOOKMARK 1       // the left half of the icon
#define FONT_GLYPH_BOOKMARK_2 2     // the right half, drawn in the next cell

// Draws a simple bookmark (a ribbon with a V cut in its lower edge) into the two glyphs
// FONT_GLYPH_BOOKMARK and FONT_GLYPH_BOOKMARK_2, which are drawn side by side, so it is
// twice as wide as one cell less a little space (decision 054). Works for any size
// from 3x4 up to 8x16.
void font_set_bookmark_glyph(Font *font);

// How many 8 pixel tile rows are kept free at the top and at the bottom of the screen for a font's
// `safe` pixels (decision 062): the pixels rounded up to whole tile rows, at most 4.
int font_safe_tile_rows(const Font *font);

// True for the fonts the tile hardware can draw: 8 wide, and 8 or 16 high.
bool font_is_tile(const Font *font);

// Fills `font` with the built-in font8x8 (public domain).
void font_load_default(Font *font);

// Reads a font from the contents of a .fnt file. Two kinds are understood:
// - Raw: exactly 2048 bytes (an 8x8 font) or 4096 bytes (an 8x16 font).
// - With a header: 8 bytes, "DSLF", version 1, width, height, 0, then 256 * height
//   bytes of glyph rows. This is how other sizes such as 6x12 are stored.
// Returns false for anything else.
bool font_from_bytes(Font *font, const uint8_t *data, size_t size);

// A font that can be chosen in Settings.
typedef struct {
	char name[64]; // "default" (8x8), "builtin-6x12", or the file name in /DSLeaf/Fonts
	int width, height;
} FontChoice;

#define FONT_CHOICES_MAX 16

// Lists the two built-in fonts first (8x8 and 6x12), then the valid fonts in /DSLeaf/Fonts sorted by
// name, ignoring case. Files that are not fonts, folders, and names that cannot
// be saved in config.txt are left out. Returns how many were listed (at least 1).
int font_list(FontChoice *out, int max);

// Loads the font called `name` from config.txt: "default" is the built-in
// font, anything else is a file in /DSLeaf/Fonts. If the file is missing or not
// a font, the built-in font is used and false is returned.
bool font_load_named(Font *font, const char *name);

// Converts one 8 row piece of a glyph to a 4bpp DS tile (8 rows of 8 pixels,
// pixel 0 in the low nibble). Set pixels become colour index 1, clear pixels
// index 0. With `rotated`, the piece is turned so that text reads upright when
// the device is held rotated 90 degrees counter-clockwise (decision 027).
void font_glyph_to_tile(const uint8_t glyph[8], bool rotated, uint32_t tile[8]);

// Converts a whole glyph to tiles, 8 words per tile, and returns how many tiles
// (1 for an 8 row font, 2 for a 16 row font). The tiles come in screen order:
// the upper then the lower half, or, rotated, the left then the right tile.
int font_glyph_to_tiles(const Font *font, int glyph, bool rotated, uint32_t out[16]);

#endif
