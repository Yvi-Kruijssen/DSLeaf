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
#include "theme.h"

typedef enum {
	SCREEN_TOP,
	SCREEN_BOTTOM,
} Screen;

typedef enum {
	STYLE_NORMAL,
	STYLE_DIM,       // status line, secondary text
	STYLE_ERROR,     // red, for " - error"
	STYLE_HIGHLIGHT, // selected item
	STYLE_ACCENT,    // the main menu's title and selected book (the logo's green, decision 053)
	STYLE_COUNT,
} Style;

// ---- A picture behind the tile text (decision 053) ----
//
// The menus can draw simple graphics (panels, ruled lines, buttons) in the
// bitmap layer, which shows behind the tile text. Colours are chosen from a
// fixed set that follows the theme. Only for the tile fonts (the bitmap layer
// is used by pixel fonts otherwise). Any render_clear of the screen removes
// the picture again, so it is drawn after the clear.
typedef enum {
	BD_BACK,      // outside the panel
	BD_PANEL,     // the page (the same as the text background)
	BD_EDGE,      // outline of the panel
	BD_RULE,      // thin ruled lines
	BD_ACCENT,    // green: title line, cursor arrow, focus outline
	BD_BTN,       // button face
	BD_BTN_LIGHT, // lit bevel edge
	BD_BTN_SHADE, // shaded bevel edge and outline
	BD_BTN_FOCUS, // face of the focused button
	BD_BTN_TEXT,  // label
	BD_BTN_DIM,   // label of a greyed out button
	BD_BTN_HI,    // label of the focused button
	BD_BTN_EDGE,  // thin outline of a button
	BD_HINT,      // small text on the dark background below a panel
	BD_SHADOW,    // the shadow of a pop-up over the page
	BD_COUNT,
} BackdropColour;

// Fills the screen with BD_BACK and shows the picture behind the text.
void render_backdrop_begin(Screen screen);
void render_backdrop_rect(Screen screen, int x, int y, int w, int h, BackdropColour colour);
// Draws 8x8 text at a pixel position (not on the cell grid). The font is the menu font set with
// render_set_ui_font, whatever font the book uses.
void render_backdrop_text(Screen screen, int x, int y, const char *text, BackdropColour colour);

// The same at `scale` times the size (2 for large digits).
void render_backdrop_text_scaled(Screen screen, int x, int y, const char *text, BackdropColour colour, int scale);

// The same for UTF-8 text, cut at `max_width` pixels. Returns the width drawn in pixels.
int render_backdrop_text_utf8(Screen screen, int x, int y, const char *text, BackdropColour colour, int max_width);

// The font the pictures use for their text (the built-in 8x8 menu font). Call once.
void render_set_ui_font(const Font *font);

// ---- A pop-up over the reading page (decision 066) ----
//
// Starts a pop-up on `screen`: the cells of the page under the rectangle are blanked (tile fonts), or
// the picture is drawn over the page (pixel fonts), so the render_backdrop calls that follow draw
// the pop-up on top of the page. Coordinates are those of the page as it is held (decision 049): when
// the page is rotated, everything drawn with the render_backdrop calls is turned with it. The rectangle
// is the area that will be drawn (with its shadow). The pop-up goes away when the page is drawn again
// (render_clear).
void render_popup_begin(Screen screen, int x, int y, int w, int h);

// ---- Pictures (decision 075) ----
//
// A picture page shows a picture in the area of the page above the status row. The picture is
// drawn into the bitmap layer with its own palette (picsink.h), which replaces the picture colours
// of the menus until the screen is cleared again.

// The area of the page (as the reader holds it, see popup.h) for a picture: the text rows without
// the status row.
void render_picture_area(int *x, int *y, int *w, int *h);

// Starts a picture on `screen`: clears it and loads the picture palette. The status row is drawn
// with the usual calls afterwards.
void render_picture_begin(Screen screen);

// Puts one row of `w` palette entries (picsink.h) at (x, y) of the page.
void render_picture_row(Screen screen, int x, int y, int w, const uint8_t *idx);

// Sets up both engines. Call once at startup.
void render_init(void);

// Uses `font` for all text. The font must stay valid until it is replaced.
// Existing screen contents are cleared. Fonts that are 8 wide and 8 or 16 high
// use the tile layer, any other size uses the bitmap layer.
void render_set_font(const Font *font);

// Sets the font and the rotation in one go, and clears both screens. Prefer this
// to the two calls below when changing both.
void render_set_view(const Font *font, bool rotated);

// Switches between the normal and the rotated grid, and clears both screens.
void render_set_rotated(bool rotated);
bool render_is_rotated(void);

// Sets the colour theme for both screens (decision 042).
void render_set_theme(Theme theme);
Theme render_theme(void);

// Current grid size, which depends on the rotation.
int render_cols(void);
int render_rows(void);

void render_clear(Screen screen);

// The startup logo (decision 047). It covers the whole screen until it is hidden,
// and the other screen is not affected. Whatever was on the screen before comes
// back when the logo is hidden (a pixel font's page is copied again by the next
// render_present).
void render_show_logo(Screen screen);
void render_hide_logo(Screen screen);

// Fades one screen: 0 is normal and -16 is black.
void render_fade(Screen screen, int level);

// Makes what was drawn visible. Fonts drawn pixel by pixel are copied to the
// screen here, once per change. Tile fonts show at once, so this does nothing
// for them. Call it for both screens after drawing a frame.
void render_present(Screen screen);

// Draws one glyph (a Latin-1 code). Cells outside the grid are ignored.
void render_putc(Screen screen, int col, int row, uint8_t glyph, Style style);

// Draws a Latin-1 string on one row, clipped at the right edge.
// Returns the column after the last character drawn.
int render_puts(Screen screen, int col, int row, const char *str, Style style);

// Same for UTF-8 text, using the fallback mapping in text.h.
int render_puts_utf8(Screen screen, int col, int row, const char *str, Style style);

// UTF-8 text that stops before column `end_col` instead of the grid edge.
int render_puts_utf8_clip(Screen screen, int col, int row, const char *str, Style style,
                          int end_col);

#endif
