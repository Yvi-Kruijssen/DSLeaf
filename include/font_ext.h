// The extra letters of the built-in 6x12 font: Greek and Cyrillic (docs: decision 091).
//
// A glyph code of 0x100 or more is the Unicode code point itself. The built-in 6x12 font has the Greek and Cyrillic letters
// of Terminus 6x12 in the ranges below, 12 bytes (rows from the top, bit 0 leftmost) for each glyph. Fonts read from the card
// have none. Layout asks `font_ext_has` which letters it may use, so that the rest is written in Latin letters instead.
#ifndef DSLEAF_FONT_EXT_H
#define DSLEAF_FONT_EXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
	uint16_t first;  // first code point of the run
	uint16_t count;  // glyphs in the run
	uint16_t offset; // of its first glyph, counted in glyphs, in font_ext_rows
} FontRange;

extern const FontRange font_ext_ranges[];
extern const int font_ext_range_count;
extern const unsigned char font_ext_rows[];

// The 12 rows of a glyph, or NULL if the built-in font has none for that code point.
const uint8_t *font_ext_rows_of(uint32_t cp);

static inline bool font_ext_has(uint32_t cp) { return font_ext_rows_of(cp) != NULL; }

#endif
