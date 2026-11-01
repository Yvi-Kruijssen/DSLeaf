// Tile-based text rendering on both screens (docs: architecture.md).
//
// Each screen is a grid of 8x8 cells, one glyph per cell. The normal grid is
// 32 columns by 24 rows. In rotated mode (decision 027) the grid is 24 columns
// by 32 rows, drawn so that it reads upright with the device turned 90 degrees
// counter-clockwise.
#ifndef DSLEAF_RENDER_H
#define DSLEAF_RENDER_H

#include <stdbool.h>
#include <stdint.h>

#include "font.h"

typedef enum {
	SCREEN_TOP,
	SCREEN_BOTTOM,
} Screen;

typedef enum {
	STYLE_NORMAL,
	STYLE_DIM,       // status line, secondary text
	STYLE_ERROR,     // red, for " - error"
	STYLE_HIGHLIGHT, // selected item
	STYLE_COUNT,
} Style;

// Sets up both engines. Call once at startup.
void render_init(void);

// Uses `font` for all text. The font must stay valid until it is replaced.
// Existing screen contents are cleared.
void render_set_font(const Font *font);

// Switches between the normal and the rotated grid, and clears both screens.
void render_set_rotated(bool rotated);
bool render_is_rotated(void);

// Swaps between the light and the dark colour theme (decision 017).
void render_set_inverted(bool inverted);
bool render_is_inverted(void);

// Current grid size, which depends on the rotation.
int render_cols(void);
int render_rows(void);

void render_clear(Screen screen);

// Draws one glyph (a Latin-1 code). Cells outside the grid are ignored.
void render_putc(Screen screen, int col, int row, uint8_t glyph, Style style);

// Draws a Latin-1 string on one row, clipped at the right edge.
// Returns the column after the last character drawn.
int render_puts(Screen screen, int col, int row, const char *str, Style style);

// Same for UTF-8 text, using the fallback mapping in text.h.
int render_puts_utf8(Screen screen, int col, int row, const char *str, Style style);

#endif
