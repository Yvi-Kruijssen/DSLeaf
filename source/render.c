#include "render.h"

#include <nds.h>

#include "logo_bin.h" // generated from data/logo.bin by the build
#include "pixbuf.h"
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

static u16 *map[2];
static u16 *gfx[2];
static u16 *palette[2];
static int bg_id[2];

typedef struct {
	u16 bg;
	u16 fg[STYLE_COUNT];
} Palette;

static const Palette palettes[THEME_COUNT] = {
	[THEME_NORMAL] = {
		RGB15(30, 29, 25),
		{ RGB15(3, 6, 4), RGB15(14, 16, 13), RGB15(24, 3, 3), RGB15(5, 16, 7), RGB15(14, 22, 8) },
	},
	[THEME_DARK] = {
		RGB15(2, 5, 3),
		{ RGB15(30, 29, 25), RGB15(15, 18, 14), RGB15(31, 10, 10), RGB15(14, 26, 12), RGB15(20, 26, 11) },
	},
	[THEME_SEPIA] = {
		RGB15(27, 24, 18),
		{ RGB15(7, 5, 3), RGB15(15, 12, 9), RGB15(17, 5, 4), RGB15(12, 8, 3), RGB15(12, 8, 3) },
	},
};

static const u16 backdrop_colours[THEME_COUNT][BD_COUNT] = {
	// BACK is the dark green of the logo screen and ACCENT its light green (decision 053).
	[THEME_NORMAL] = {
		RGB15(3, 7, 6), RGB15(30, 29, 25), RGB15(17, 20, 15), RGB15(25, 25, 21), RGB15(14, 22, 8), RGB15(28, 28, 26),
		RGB15(31, 31, 30), RGB15(21, 22, 20), RGB15(30, 31, 27), RGB15(3, 6, 4), RGB15(15, 16, 14), RGB15(5, 16, 7),
		RGB15(19, 20, 18), RGB15(22, 25, 18),
	},
	[THEME_DARK] = {
		RGB15(3, 7, 6), RGB15(2, 5, 3), RGB15(9, 14, 10), RGB15(6, 10, 7), RGB15(20, 26, 11), RGB15(8, 11, 9),
		RGB15(13, 17, 14), RGB15(4, 6, 5), RGB15(10, 15, 10), RGB15(30, 29, 25), RGB15(13, 16, 12), RGB15(20, 26, 11),
		RGB15(12, 17, 13), RGB15(18, 22, 15),
	},
	[THEME_SEPIA] = {
		RGB15(3, 7, 6), RGB15(27, 24, 18), RGB15(15, 11, 7), RGB15(23, 20, 14), RGB15(12, 8, 3), RGB15(25, 22, 17),
		RGB15(30, 28, 23), RGB15(18, 15, 10), RGB15(28, 25, 18), RGB15(7, 5, 3), RGB15(16, 13, 10), RGB15(12, 8, 3),
		RGB15(17, 14, 10), RGB15(21, 22, 16),
	},
};

static void apply_theme(void) {
	for (int s = 0; s < 2; s++) {
		for (int i = 0; i < BD_COUNT; i++) palette[s][BACKDROP_PALETTE_BASE + i] = backdrop_colours[theme][i];
	}
	const Palette *t = &palettes[theme];
	for (int s = 0; s < 2; s++) {
		for (int style = 0; style < STYLE_COUNT; style++) {
			palette[s][style * 16 + 0] = t->bg;
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

// Shows the tile layer or the bitmap layer on both screens, not both.
static void show_layer(void) {
	// A picture behind the text (backdrop) shows the bitmap layer and the tile text together.
	u32 top = pixel || logo[SCREEN_TOP] ? DISPLAY_BG2_ACTIVE : DISPLAY_BG0_ACTIVE;
	u32 bottom = pixel || logo[SCREEN_BOTTOM] ? DISPLAY_BG2_ACTIVE : DISPLAY_BG0_ACTIVE;
	if (!pixel && backdrop[SCREEN_TOP] && !logo[SCREEN_TOP]) top |= DISPLAY_BG0_ACTIVE | DISPLAY_BG2_ACTIVE;
	if (!pixel && backdrop[SCREEN_BOTTOM] && !logo[SCREEN_BOTTOM]) bottom |= DISPLAY_BG0_ACTIVE | DISPLAY_BG2_ACTIVE;
	videoSetMode(MODE_5_2D | top);
	videoSetModeSub(MODE_5_2D | bottom);
}

void render_init(void) {
	powerOn(POWER_ALL_2D);
	lcdMainOnTop();

	vramSetBankA(VRAM_A_MAIN_BG);
	vramSetBankC(VRAM_C_SUB_BG);
	show_layer();

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
	for (int i = 0; i < GRID_W * GRID_W; i++) map[screen][i] = blank;
}

// Copies the buffer of a screen to the bitmap layer, if it changed.
void render_present(Screen screen) {
	if (!(pixel || backdrop[screen]) || !fb_dirty[screen] || logo[screen]) return;
	fb_dirty[screen] = false;

	volatile u32 *dst = (volatile u32 *)bitmap[screen];
	const u32 *src = (const u32 *)fb[screen];
	for (int i = 0; i < PIX_W * PIX_H / 4; i++) dst[i] = src[i];
}

static void restart(void) {
	pixel = font && !font_is_tile(font);
	if (font && !pixel) upload_tiles();
	show_layer();
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

int render_cols(void) { return pixel ? pix_cols(font, rotated) : (rotated ? 24 : 32); }
int render_rows(void) { return pixel ? pix_rows(font, rotated) : (rotated ? 32 : 24) / tiles_per_glyph(); }

void render_putc(Screen screen, int col, int row, uint8_t glyph, Style style) {
	if (col < 0 || col >= render_cols() || row < 0 || row >= render_rows()) return;

	if (pixel) {
		pix_draw_glyph(fb[screen], font, rotated, col, row, glyph, (uint8_t)(style * 16 + 1));
		fb_dirty[screen] = true;
		return;
	}

	int per = tiles_per_glyph();
	u16 tile = (u16)(glyph * per) | (u16)(style << 12);

	if (!rotated) {
		// A tall glyph is two tiles, one above the other.
		int y = row * per;
		map[screen][y * GRID_W + col] = tile;
		if (per == 2) map[screen][(y + 1) * GRID_W + col] = tile + 1;
		return;
	}

	// Rotated: the page runs down the screen, so a page column becomes a
	// screen row and page rows count from the right-hand edge. A tall glyph
	// turned on its side is two tiles side by side, the left one first.
	int y = col;
	int x = GRID_W - per * (row + 1);
	map[screen][y * GRID_W + x] = tile;
	if (per == 2) map[screen][y * GRID_W + x + 1] = tile + 1;
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

void render_backdrop_begin(Screen screen) {
	if (pixel) return; // the bitmap layer holds the page then
	memset(fb[screen], BACKDROP_PALETTE_BASE + BD_BACK, sizeof fb[screen]);
	fb_dirty[screen] = true;
	backdrop[screen] = true;
	show_layer();
}

void render_backdrop_rect(Screen screen, int x, int y, int w, int h, BackdropColour colour) {
	if (!backdrop[screen]) return;
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > PIX_W) w = PIX_W - x;
	if (y + h > PIX_H) h = PIX_H - y;
	if (w <= 0 || h <= 0) return;
	for (int row = 0; row < h; row++) memset(&fb[screen][(y + row) * PIX_W + x], BACKDROP_PALETTE_BASE + colour, (size_t)w);
	fb_dirty[screen] = true;
}

void render_backdrop_text(Screen screen, int x, int y, const char *text, BackdropColour colour) {
	if (!backdrop[screen] || !font) return;
	for (; *text; text++, x += font->width) {
		const uint8_t *bits = font->glyph[(uint8_t)*text];
		for (int py = 0; py < font->height; py++) {
			for (int px = 0; px < font->width; px++) {
				if (((bits[py] >> px) & 1) && x + px >= 0 && x + px < PIX_W && y + py >= 0 && y + py < PIX_H) {
					fb[screen][(y + py) * PIX_W + x + px] = (uint8_t)(BACKDROP_PALETTE_BASE + colour);
				}
			}
		}
	}
	fb_dirty[screen] = true;
}

// ---- The startup logo ----

void render_show_logo(Screen screen) {
	// logo_bin: 64 palette entries of 2 bytes, then 256 x 192 colour numbers.
	const u16 *entries = (const u16 *)logo_bin;
	u16 *pal = screen == SCREEN_TOP ? BG_PALETTE : BG_PALETTE_SUB;
	for (int i = 0; i < LOGO_COLOURS; i++) pal[LOGO_PALETTE_BASE + i] = entries[i];

	const u32 *src = (const u32 *)(logo_bin + LOGO_COLOURS * 2);
	volatile u32 *dst = (volatile u32 *)bitmap[screen];
	for (int i = 0; i < PIX_W * PIX_H / 4; i++) {
		dst[i] = src[i] + 0x80808080u; // four pixels at a time: every colour number is below 64
	}

	logo[screen] = true;
	show_layer();
}

void render_hide_logo(Screen screen) {
	logo[screen] = false;
	fb_dirty[screen] = true; // the bitmap layer was overwritten, so a pixel font has to copy its page again
	show_layer();
}

void render_fade(Screen screen, int level) {
	if (level < -16) level = -16;
	if (level > 0) level = 0;
	setBrightness(screen == SCREEN_TOP ? 1 : 2, level);
}
