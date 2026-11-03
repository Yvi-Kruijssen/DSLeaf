#include "render.h"

#include <nds.h>

#include "logo_bin.h" // generated from data/logo.bin by the build
#include "pixbuf.h"
#include "picsink.h"
#include "popup.h"
#include "text.h"

#define GRID_W 32 // tile map is 32x32, the visible area is 32x24
#define TILE_MAP_BASE 8 // in 2 KB units, after the 16 KB of tile data at 0 (256 glyphs of up to 2 tiles)
#define BITMAP_BASE 2   // in 16 KB units: the 64 KB bitmap layer starts at 32 KB, after the tiles and the map

static const Font *font;
static bool rotated;
static bool pixel; // the font is drawn into the bitmap layer instead of with tiles
static Theme theme;

// The bitmap layer: text is drawn into a buffer in main RAM, then copied to VRAM,
// which only takes 16 and 32 bit writes. Pixel values are palette entries, 0 is
// the background and a style's text colour is style * 16 + 1.
static uint8_t fb[2][PIX_W * PIX_H] __attribute__((aligned(4)));
static bool fb_dirty[2];
static u16 *bitmap[2];
static int bitmap_id[2];

// The startup logo takes over the bitmap layer of a screen until it is hidden.
// Its 64 colours go to palette entries 128 to 191, after those of the text
// themes, and the pixel numbers in logo_bin are added to 128.
#define LOGO_PALETTE_BASE 128
#define LOGO_COLOURS 64
static bool logo[2];

// The picture behind the menus: drawn into the same buffers as pixel text, with
// colours in palette entries 192 up, after the logo's.
#define BACKDROP_PALETTE_BASE 192
static bool backdrop[2];
static bool logo_green;         // palette entry 0 is the logo's green for the start-up slide
static bool picture_palette[2]; // the palette holds a picture's colours (decision 075)
static bool popup_rot[2];     // the picture is drawn turned, like a rotated page (decision 066)
static const Font *ui_font;   // the font of the text in pictures, the built-in 8x8 font

static u16 *map[2];
// What the tile map should hold. All text is drawn here and copied to the map by render_present, in
// the vertical blank, together with the change of layers and the bitmap, so a screen that is redrawn
// changes in one go and does not flash while it is being drawn (decision 080).
static u16 map_shadow[2][GRID_W * GRID_W];
static bool map_dirty[2];
static bool layers_pending; // the layers shown must be set again at the next render_present
static u16 *gfx[2];
// Palette memory only takes 16 and 32 bit writes. The pointers are volatile so that a loop copying a colour table
// cannot be turned into a memcpy, which writes single bytes when the table is not 4-byte aligned (the dark
// theme row of the picture colours is not) and then nothing changes (decision 068).
static volatile u16 *palette[2];
static int bg_id[2];

typedef struct {
	u16 bg;
	u16 fg[STYLE_COUNT];
} Palette;

static const Palette palettes[THEME_COUNT] = {
	[THEME_NORMAL] = {
		RGB15(30, 29, 25),
		{ RGB15(3, 6, 4), RGB15(14, 16, 13), RGB15(24, 3, 3), RGB15(5, 16, 7), RGB15(10, 17, 5) },
	},
	[THEME_DARK] = { // black with neutral grey text, the green kept only as the accent (decision 064)
		RGB15(0, 0, 0),
		{ RGB15(29, 29, 29), RGB15(15, 15, 15), RGB15(31, 10, 10), RGB15(14, 26, 12), RGB15(20, 26, 11) },
	},
	[THEME_SEPIA] = {
		RGB15(27, 24, 18),
		{ RGB15(7, 5, 3), RGB15(15, 12, 9), RGB15(17, 5, 4), RGB15(12, 8, 3), RGB15(12, 8, 3) },
	},
};

static const u16 backdrop_colours[THEME_COUNT][BD_COUNT] = {
	// BACK is the dark green of the logo screen and ACCENT its light green (decision 053).
	[THEME_NORMAL] = {
		RGB15(3, 7, 6), RGB15(30, 29, 25), RGB15(17, 20, 15), RGB15(25, 25, 21), RGB15(10, 17, 5), RGB15(28, 28, 26),
		RGB15(31, 31, 30), RGB15(21, 22, 20), RGB15(30, 31, 27), RGB15(3, 6, 4), RGB15(15, 16, 14), RGB15(5, 16, 7),
		RGB15(19, 20, 18), RGB15(22, 25, 18), RGB15(17, 18, 15),
	},
	[THEME_DARK] = { // black and neutral greys (decision 064); only the accent and the focus colour are green
		RGB15(0, 0, 0), RGB15(2, 2, 2), RGB15(9, 9, 9), RGB15(5, 5, 5), RGB15(20, 26, 11), RGB15(7, 7, 7),
		RGB15(12, 12, 12), RGB15(1, 1, 1), RGB15(9, 9, 9), RGB15(29, 29, 29), RGB15(12, 12, 12), RGB15(20, 26, 11),
		RGB15(10, 10, 10), RGB15(18, 18, 18), RGB15(3, 3, 3),
	},
	[THEME_SEPIA] = { // a dark brown background and warm hint text, not the logo's green (decision 078)
		RGB15(7, 4, 2), RGB15(27, 24, 18), RGB15(15, 11, 7), RGB15(23, 20, 14), RGB15(12, 8, 3), RGB15(25, 22, 17),
		RGB15(30, 28, 23), RGB15(18, 15, 10), RGB15(28, 25, 18), RGB15(7, 5, 3), RGB15(16, 13, 10), RGB15(12, 8, 3),
		RGB15(17, 14, 10), RGB15(25, 20, 13), RGB15(18, 14, 9),
	},
};

static void apply_backdrop(int s) {
	for (int i = 0; i < BD_COUNT; i++) palette[s][BACKDROP_PALETTE_BASE + i] = backdrop_colours[theme][i];
}

static void apply_theme(void) {
	for (int s = 0; s < 2; s++) {
		apply_backdrop(s);
		picture_palette[s] = false;
	}
	const Palette *t = &palettes[theme];
	for (int s = 0; s < 2; s++) {
		for (int style = 0; style < STYLE_COUNT; style++) {
			if (!(logo_green && style == 0)) palette[s][style * 16 + 0] = t->bg;
			palette[s][style * 16 + 1] = t->fg[style];
		}
	}
}

// Tiles used by one glyph: a tall font (16 rows) uses two stacked tiles.
static int tiles_per_glyph(void) { return font && font->height == 16 ? 2 : 1; }

// VRAM only takes 16 and 32 bit writes, so copy tiles word by word. Tile
// number g * tiles_per_glyph is the first tile of glyph g.
static void upload_tiles(void) {
	int per = tiles_per_glyph();
	for (int s = 0; s < 2; s++) {
		volatile u32 *dst = (volatile u32 *)gfx[s];
		for (int g = 0; g < FONT_GLYPHS; g++) {
			uint32_t tiles[16];
			int n = font_glyph_to_tiles(font, g, rotated, tiles);
			for (int i = 0; i < n * 8; i++) dst[g * per * 8 + i] = tiles[i];
		}
	}
}

// Shows the tile layer or the bitmap layer on both screens, not both. Sets them now.
static void apply_layers(void) {
	// A picture behind the text (backdrop) shows the bitmap layer and the tile text together.
	u32 top = pixel || logo[SCREEN_TOP] ? DISPLAY_BG2_ACTIVE : DISPLAY_BG0_ACTIVE;
	u32 bottom = pixel || logo[SCREEN_BOTTOM] ? DISPLAY_BG2_ACTIVE : DISPLAY_BG0_ACTIVE;
	if (!pixel && backdrop[SCREEN_TOP] && !logo[SCREEN_TOP]) top |= DISPLAY_BG0_ACTIVE | DISPLAY_BG2_ACTIVE;
	if (!pixel && backdrop[SCREEN_BOTTOM] && !logo[SCREEN_BOTTOM]) bottom |= DISPLAY_BG0_ACTIVE | DISPLAY_BG2_ACTIVE;
	videoSetMode(MODE_5_2D | top);
	videoSetModeSub(MODE_5_2D | bottom);
}

// The same, but at the next render_present (in the vertical blank).
static void show_layer(void) { layers_pending = true; }

void render_init(void) {
	powerOn(POWER_ALL_2D);
	lcdMainOnTop();

	vramSetBankA(VRAM_A_MAIN_BG);
	vramSetBankC(VRAM_C_SUB_BG);
	apply_layers();

	bg_id[SCREEN_TOP] = bgInit(0, BgType_Text4bpp, BgSize_T_256x256, TILE_MAP_BASE, 0);
	bg_id[SCREEN_BOTTOM] = bgInitSub(0, BgType_Text4bpp, BgSize_T_256x256, TILE_MAP_BASE, 0);
	bitmap_id[SCREEN_TOP] = bgInit(2, BgType_Bmp8, BgSize_B8_256x256, BITMAP_BASE, 0);
	bitmap_id[SCREEN_BOTTOM] = bgInitSub(2, BgType_Bmp8, BgSize_B8_256x256, BITMAP_BASE, 0);

	for (int s = 0; s < 2; s++) {
		map[s] = bgGetMapPtr(bg_id[s]);
		gfx[s] = bgGetGfxPtr(bg_id[s]);
		bitmap[s] = bgGetGfxPtr(bitmap_id[s]);
	}
	for (int s = 0; s < 2; s++) { // the text is in front of the picture
		bgSetPriority(bg_id[s], 0);
		bgSetPriority(bitmap_id[s], 1);
	}
	bgUpdate(); // writes the (unscaled) transform of the bitmap layers
	palette[SCREEN_TOP] = BG_PALETTE;
	palette[SCREEN_BOTTOM] = BG_PALETTE_SUB;

	apply_theme();
}

void render_clear(Screen screen) {
	if (picture_palette[screen]) {
		picture_palette[screen] = false;
		apply_backdrop(screen); // the picture's colours were over those of the menus
	}
	if (backdrop[screen]) {
		backdrop[screen] = false; // a cleared screen is plain text again
		show_layer();
	}
	if (pixel) {
		memset(fb[screen], 0, sizeof fb[screen]);
		fb_dirty[screen] = true;
		return;
	}
	u16 blank = (u16)(' ' * tiles_per_glyph()); // the space glyph is empty
	for (int i = 0; i < GRID_W * GRID_W; i++) map_shadow[screen][i] = blank;
	map_dirty[screen] = true;
}

// Waits for the vertical blank unless it has already begun, so that what follows is not seen half done.
static void wait_for_blank(void) {
	if (REG_VCOUNT < 192) swiWaitForVBlank();
}

// Makes what was drawn visible: the tile map, the layers shown, and the buffer of the bitmap layer.
void render_present(Screen screen) {
	bool copy = (pixel || backdrop[screen]) && fb_dirty[screen] && !logo[screen];
	if (!copy && !map_dirty[screen] && !layers_pending) return;
	wait_for_blank();

	if (layers_pending) {
		layers_pending = false;
		apply_layers();
	}
	if (map_dirty[screen]) {
		map_dirty[screen] = false;
		volatile u32 *dst = (volatile u32 *)map[screen];
		const u32 *src = (const u32 *)map_shadow[screen];
		for (int i = 0; i < GRID_W * GRID_W / 2; i++) dst[i] = src[i];
	}
	if (copy) {
		fb_dirty[screen] = false;
		volatile u32 *dst = (volatile u32 *)bitmap[screen];
		const u32 *src = (const u32 *)fb[screen];
		for (int i = 0; i < PIX_W * PIX_H / 4; i++) dst[i] = src[i];
	}
}

static void restart(void) {
	pixel = font && !font_is_tile(font);
	if (font && !pixel) upload_tiles();
	apply_layers();
	render_clear(SCREEN_TOP);
	render_clear(SCREEN_BOTTOM);
	render_present(SCREEN_TOP);
	render_present(SCREEN_BOTTOM);
}

void render_set_font(const Font *new_font) {
	font = new_font;
	restart();
}

void render_set_view(const Font *new_font, bool value) {
	font = new_font;
	rotated = value;
	restart();
}

void render_set_rotated(bool value) {
	rotated = value;
	restart();
}

bool render_is_rotated(void) { return rotated; }

void render_set_theme(Theme value) {
	theme = value < THEME_COUNT ? value : THEME_NORMAL;
	if (palette[0]) apply_theme();
}

Theme render_theme(void) { return theme; }

// Tile rows kept free at the top and the bottom for the font's safe margin (decision 062).
static int safe_rows(void) { return font ? font_safe_tile_rows(font) : 0; }

int render_cols(void) { return pixel ? pix_cols(font, rotated) : (rotated ? 24 - 2 * safe_rows() : 32); }
int render_rows(void) { return pixel ? pix_rows(font, rotated) : (rotated ? 32 : 24 - 2 * safe_rows()) / tiles_per_glyph(); }

void render_putc(Screen screen, int col, int row, Glyph glyph, Style style) {
	if (col < 0 || col >= render_cols() || row < 0 || row >= render_rows()) return;
	if (glyph == GLYPH_FILL) return;

	if (pixel) {
		pix_draw_glyph(fb[screen], font, rotated, col, row, glyph, (uint8_t)(style * 16 + 1));
		fb_dirty[screen] = true;
		return;
	}

	if (glyph >= FONT_GLYPHS) glyph = '?'; // the tile fonts have Latin-1 only
	int per = tiles_per_glyph();
	u16 tile = (u16)(glyph * per) | (u16)(style << 12);

	if (!rotated) {
		// A tall glyph is two tiles, one above the other.
		int y = row * per + safe_rows();
		map_shadow[screen][y * GRID_W + col] = tile;
		if (per == 2) map_shadow[screen][(y + 1) * GRID_W + col] = tile + 1;
		map_dirty[screen] = true;
		return;
	}

	// Rotated: the page runs down the screen, so a page column becomes a
	// screen row and page rows count from the right-hand edge. A tall glyph
	// turned on its side is two tiles side by side, the left one first.
	int y = col + safe_rows();
	int x = GRID_W - per * (row + 1);
	map_shadow[screen][y * GRID_W + x] = tile;
	if (per == 2) map_shadow[screen][y * GRID_W + x + 1] = tile + 1;
	map_dirty[screen] = true;
}

int render_puts(Screen screen, int col, int row, const char *str, Style style) {
	for (; *str && col < render_cols(); str++, col++) {
		render_putc(screen, col, row, (uint8_t)*str, style);
	}
	return col;
}

int render_puts_utf8_clip(Screen screen, int col, int row, const char *str, Style style,
                          int end_col) {
	if (end_col > render_cols()) end_col = render_cols();
	while (*str && col < end_col) {
		uint8_t glyphs[3];
		int n = text_utf8_next(&str, glyphs);
		for (int i = 0; i < n && col < end_col; i++) render_putc(screen, col++, row, glyphs[i], style);
	}
	return col;
}

int render_puts_utf8(Screen screen, int col, int row, const char *str, Style style) {
	return render_puts_utf8_clip(screen, col, row, str, style, render_cols());
}

// ---- The picture behind the text ----

void render_set_ui_font(const Font *f) { ui_font = f; }

static const Font *picture_font(void) { return ui_font ? ui_font : font; }

void render_backdrop_begin(Screen screen) {
	if (pixel) return; // the bitmap layer holds the page then
	memset(fb[screen], BACKDROP_PALETTE_BASE + BD_BACK, sizeof fb[screen]);
	popup_rot[screen] = false;
	fb_dirty[screen] = true;
	backdrop[screen] = true;
	show_layer();
}

void render_popup_begin(Screen screen, int x, int y, int w, int h) {
	popup_rot[screen] = rotated;
	if (!pixel) {
		// The bitmap layer is behind the tile text, so the cells of the page under the pop-up are made
		// empty (the space glyph), and everything else in the bitmap layer is transparent (colour 0).
		memset(fb[screen], 0, sizeof fb[screen]);
		int sx, sy, sw, sh;
		popup_to_screen(rotated, x, y, w, h, &sx, &sy, &sw, &sh);
		u16 blank = (u16)(' ' * tiles_per_glyph());
		for (int cy = sy / 8; cy <= (sy + sh - 1) / 8 && cy < GRID_W; cy++) {
			for (int cx = sx / 8; cx <= (sx + sw - 1) / 8 && cx < GRID_W; cx++) map_shadow[screen][cy * GRID_W + cx] = blank;
		}
	}
	fb_dirty[screen] = true;
	backdrop[screen] = true;
	show_layer();
}

static void picture_pixel(Screen screen, int lx, int ly, BackdropColour colour) {
	int x, y;
	popup_pixel_to_screen(popup_rot[screen], lx, ly, &x, &y);
	if (x >= 0 && x < PIX_W && y >= 0 && y < PIX_H) fb[screen][y * PIX_W + x] = (uint8_t)(BACKDROP_PALETTE_BASE + colour);
}

void render_backdrop_rect(Screen screen, int x, int y, int w, int h, BackdropColour colour) {
	if (!backdrop[screen]) return;
	popup_to_screen(popup_rot[screen], x, y, w, h, &x, &y, &w, &h);
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > PIX_W) w = PIX_W - x;
	if (y + h > PIX_H) h = PIX_H - y;
	if (w <= 0 || h <= 0) return;
	for (int row = 0; row < h; row++) memset(&fb[screen][(y + row) * PIX_W + x], BACKDROP_PALETTE_BASE + colour, (size_t)w);
	fb_dirty[screen] = true;
}

// Draws text of glyph codes at a pixel position, `scale` times as big. UTF-8 text is turned into glyph
// codes first. Stops at `max_width` pixels (0: no limit). Returns the width drawn.
static int picture_text(Screen screen, int x, int y, const char *text, BackdropColour colour, int scale, bool utf8, int max_width) {
	const Font *f = picture_font();
	if (!backdrop[screen] || !f || scale < 1) return 0;
	int start = x;
	fb_dirty[screen] = true;
	while (*text) {
		uint8_t glyphs[3];
		int n = 1;
		if (utf8) n = text_utf8_next(&text, glyphs);
		else glyphs[0] = (uint8_t)*text++;
		for (int i = 0; i < n; i++) {
			if (max_width > 0 && x - start + f->width * scale > max_width) return x - start;
			const uint8_t *bits = f->glyph[glyphs[i]];
			for (int py = 0; py < f->height; py++) {
				for (int px = 0; px < f->width; px++) {
					if (!((bits[py] >> px) & 1)) continue;
					for (int sy = 0; sy < scale; sy++) {
						for (int sx = 0; sx < scale; sx++) picture_pixel(screen, x + px * scale + sx, y + py * scale + sy, colour);
					}
				}
			}
			x += f->width * scale;
		}
	}
	return x - start;
}

void render_backdrop_text(Screen screen, int x, int y, const char *text, BackdropColour colour) {
	picture_text(screen, x, y, text, colour, 1, false, 0);
}

void render_backdrop_text_scaled(Screen screen, int x, int y, const char *text, BackdropColour colour, int scale) {
	picture_text(screen, x, y, text, colour, scale, false, 0);
}

int render_backdrop_text_utf8(Screen screen, int x, int y, const char *text, BackdropColour colour, int max_width) {
	return picture_text(screen, x, y, text, colour, 1, true, max_width);
}

// ---- The startup logo ----

void render_show_logo(Screen screen) {
	// logo_bin: 64 palette entries of 2 bytes, then 256 x 192 colour numbers.
	const u16 *entries = (const u16 *)logo_bin;
	volatile u16 *pal = screen == SCREEN_TOP ? BG_PALETTE : BG_PALETTE_SUB;
	for (int i = 0; i < LOGO_COLOURS; i++) pal[LOGO_PALETTE_BASE + i] = entries[i];

	const u32 *src = (const u32 *)(logo_bin + LOGO_COLOURS * 2);
	volatile u32 *dst = (volatile u32 *)bitmap[screen];
	for (int i = 0; i < PIX_W * PIX_H / 4; i++) {
		dst[i] = src[i] + 0x80808080u; // four pixels at a time: every colour number is below 64
	}

	logo[screen] = true;
	apply_layers();
}

void render_hide_logo(Screen screen) {
	logo[screen] = false;
	fb_dirty[screen] = true; // the bitmap layer was overwritten, so a pixel font has to copy its page again
	apply_layers();
}

void render_logo_start(void) {
	render_show_logo(SCREEN_TOP);
	render_show_logo(SCREEN_BOTTOM);
	// What shows where the bitmap layer has nothing is colour 0. Make it the logo's own background
	// green (the colour of its first pixel) so the screen above the logo, and what the logo leaves
	// behind, is that green.
	const u16 *entries = (const u16 *)logo_bin;
	u16 green = entries[logo_bin[LOGO_COLOURS * 2]];
	logo_green = true;
	for (int s = 0; s < 2; s++) {
		palette[s][0] = green;
		bgWrapOff(bitmap_id[s]);
	}
	render_logo_slide(0);
}

void render_logo_slide(int pixels) {
	bgSetScroll(bitmap_id[SCREEN_BOTTOM], 0, pixels);
	bgSetScroll(bitmap_id[SCREEN_TOP], 0, pixels - PIX_H);
	bgUpdate();
}

void render_logo_slide_end(void) {
	bgSetScroll(bitmap_id[SCREEN_BOTTOM], 0, 0);
	bgSetScroll(bitmap_id[SCREEN_TOP], 0, 0);
	bgUpdate();
	logo_green = false;
	apply_theme(); // entry 0 is the theme's background again
}

void render_fade(Screen screen, int level) {
	if (level < -16) level = -16;
	if (level > 0) level = 0;
	setBrightness(screen == SCREEN_TOP ? 1 : 2, level);
}

// ---- Pictures ----

void render_picture_area(int *x, int *y, int *w, int *h) {
	int row_h;
	if (pixel) {
		pix_page_origin(font, rotated, x, y, w, h, &row_h);
	} else {
		row_h = tiles_per_glyph() * 8;
		*w = render_cols() * 8;
		*h = render_rows() * row_h;
		*x = rotated ? safe_rows() * 8 : 0;
		*y = rotated ? 0 : safe_rows() * 8;
	}
	*h -= row_h + 2; // the status row is the last row
}

void render_picture_begin(Screen screen) {
	render_clear(screen);
	uint16_t colours[PIC_PAL_COUNT];
	picsink_palette(colours);
	for (int i = 0; i < PIC_PAL_COUNT; i++) palette[screen][PIC_PAL_BASE + i] = colours[i];
	picture_palette[screen] = true;
	if (!pixel) {
		memset(fb[screen], 0, sizeof fb[screen]);
		backdrop[screen] = true;
		popup_rot[screen] = rotated;
		show_layer();
	}
	fb_dirty[screen] = true;
}

void render_picture_row(Screen screen, int x, int y, int w, const uint8_t *idx) {
	if (!picture_palette[screen]) return;
	for (int i = 0; i < w; i++) {
		int sx, sy;
		if (rotated) {
			popup_pixel_to_screen(true, x + i, y, &sx, &sy);
		} else {
			sx = x + i;
			sy = y;
		}
		if (sx >= 0 && sx < PIX_W && sy >= 0 && sy < PIX_H) fb[screen][sy * PIX_W + sx] = idx[i];
	}
	fb_dirty[screen] = true;
}
