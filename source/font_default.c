// The built-in font. font8x8 by Daniel Hepper, public domain, based on the
// IBM VGA fonts as collected by Marcel Sondaar. See third_party/font8x8.
// Source commit: 8e279d2d864e79128e96188a6b9526cfa3fbfef9
#include "font.h"

#include <string.h>

// These headers define the glyph arrays, so include them in one file only.
#include "font8x8_latin.h"

void font_load_default(Font *font) {
	_Static_assert(sizeof(font8x8_basic) + sizeof(font8x8_control) + sizeof(font8x8_ext_latin)
	                   == FONT_GLYPHS * 8,
	               "font8x8 sets must cover U+0000..U+00FF");

	memset(font, 0, sizeof *font);
	font->width = 8;
	font->height = 8;
	for (int g = 0; g < 128; g++) memcpy(font->glyph[g], font8x8_basic[g], 8);
	for (int g = 0; g < 32; g++) memcpy(font->glyph[0x80 + g], font8x8_control[g], 8);
	for (int g = 0; g < 96; g++) memcpy(font->glyph[0xA0 + g], font8x8_ext_latin[g], 8);
}
