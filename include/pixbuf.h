// Drawing text into a pixel buffer (docs: decision 044).
//
// Fonts that the tile hardware cannot draw (such as 6x12) are drawn pixel by
// pixel into a 256x192 buffer of one byte per pixel, which the renderer then
// copies to the DS bitmap layer. This file has no DS code, so it is tested on
// a PC.
//
// Text is placed on a grid of cells, one glyph per cell. The grid is centred on
// the screen. In rotated mode (decision 027) the page is turned so that it reads
// upright with the device held rotated 90 degrees counter-clockwise: columns run
// down the screen and lines run from the right edge towards the left.
#ifndef DSLEAF_PIXBUF_H
#define DSLEAF_PIXBUF_H

#include <stdbool.h>
#include <stdint.h>

#include "font.h"

#define PIX_W 256
#define PIX_H 192

// Size of the cell grid for a font.
int pix_cols(const Font *font, bool rotated);
int pix_rows(const Font *font, bool rotated);

// Draws one glyph in a cell: the cell is cleared to 0, then the glyph's set
// pixels get `value`. Cells outside the grid are ignored.
void pix_draw_glyph(uint8_t *fb, const Font *font, bool rotated, int col, int row, uint8_t glyph,
                    uint8_t value);

#endif
