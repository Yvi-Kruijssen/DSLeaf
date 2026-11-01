#include "render.h"

#include <nds.h>

#include "text.h"

#define GRID_W 32 // tile map is 32x32, the visible area is 32x24
#define TILE_MAP_BASE 4 // in 2 KB units, after the 8 KB of tile data at 0

static const Font *font;
static bool rotated;
static bool inverted;

static u16 *map[2];
static u16 *gfx[2];
static u16 *palette[2];
static int bg_id[2];

typedef struct {
	u16 bg;
	u16 fg[STYLE_COUNT];
} Theme;

static const Theme light = {
	RGB15(30, 29, 25),
	{ RGB15(3, 6, 4), RGB15(14, 16, 13), RGB15(24, 3, 3), RGB15(5, 16, 7) },
};
static const Theme dark = {
	RGB15(2, 5, 3),
	{ RGB15(30, 29, 25), RGB15(15, 18, 14), RGB15(31, 10, 10), RGB15(14, 26, 12) },
};

static void apply_theme(void) {
	const Theme *t = inverted ? &dark : &light;
	for (int s = 0; s < 2; s++) {
		for (int style = 0; style < STYLE_COUNT; style++) {
			palette[s][style * 16 + 0] = t->bg;
			palette[s][style * 16 + 1] = t->fg[style];
		}
	}
}

// VRAM only takes 16 and 32 bit writes, so copy tiles word by word.
static void upload_tiles(void) {
	for (int s = 0; s < 2; s++) {
		volatile u32 *dst = (volatile u32 *)gfx[s];
		for (int g = 0; g < FONT_GLYPHS; g++) {
			uint32_t tile[8];
			font_glyph_to_tile(font->glyph[g], rotated, tile);
			for (int i = 0; i < 8; i++) dst[g * 8 + i] = tile[i];
		}
	}
}

void render_init(void) {
	powerOn(POWER_ALL_2D);
	lcdMainOnTop();

	vramSetBankA(VRAM_A_MAIN_BG);
	vramSetBankC(VRAM_C_SUB_BG);
	videoSetMode(MODE_0_2D | DISPLAY_BG0_ACTIVE);
	videoSetModeSub(MODE_0_2D | DISPLAY_BG0_ACTIVE);

	bg_id[SCREEN_TOP] = bgInit(0, BgType_Text4bpp, BgSize_T_256x256, TILE_MAP_BASE, 0);
	bg_id[SCREEN_BOTTOM] = bgInitSub(0, BgType_Text4bpp, BgSize_T_256x256, TILE_MAP_BASE, 0);

	for (int s = 0; s < 2; s++) {
		map[s] = bgGetMapPtr(bg_id[s]);
		gfx[s] = bgGetGfxPtr(bg_id[s]);
	}
	palette[SCREEN_TOP] = BG_PALETTE;
	palette[SCREEN_BOTTOM] = BG_PALETTE_SUB;

	apply_theme();
}

void render_clear(Screen screen) {
	u16 blank = ' ';
	for (int i = 0; i < GRID_W * GRID_W; i++) map[screen][i] = blank;
}

void render_set_font(const Font *new_font) {
	font = new_font;
	upload_tiles();
	render_clear(SCREEN_TOP);
	render_clear(SCREEN_BOTTOM);
}

void render_set_rotated(bool value) {
	rotated = value;
	if (font) upload_tiles();
	render_clear(SCREEN_TOP);
	render_clear(SCREEN_BOTTOM);
}

bool render_is_rotated(void) { return rotated; }

void render_set_inverted(bool value) {
	inverted = value;
	if (palette[0]) apply_theme();
}

bool render_is_inverted(void) { return inverted; }

int render_cols(void) { return rotated ? 24 : 32; }
int render_rows(void) { return rotated ? 32 : 24; }

void render_putc(Screen screen, int col, int row, uint8_t glyph, Style style) {
	if (col < 0 || col >= render_cols() || row < 0 || row >= render_rows()) return;

	// Rotated: the page runs down the screen, so a page column becomes a
	// screen row and page rows count from the right-hand edge.
	int x = rotated ? 31 - row : col;
	int y = rotated ? col : row;
	map[screen][y * GRID_W + x] = glyph | (style << 12);
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
