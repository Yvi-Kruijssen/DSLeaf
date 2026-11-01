#include "font.h"

#include <string.h>

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
