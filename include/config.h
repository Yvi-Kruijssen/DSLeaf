// Settings saved in /DSLeaf/config.txt (docs: storage.md, decisions 003, 017, 025, 027).
#ifndef DSLEAF_CONFIG_H
#define DSLEAF_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "theme.h"

#define CONFIG_PATH "/DSLeaf/config.txt"
#define CONFIG_FONT_MAX 64 // file name in /DSLeaf/Fonts, or "default" (the built-in font)
// The font a new installation asks for: the 6x12 font built into the program (decisions 058 and 073), so it is
// always there.
#define CONFIG_FONT_DEFAULT "builtin-6x12"
#define CONFIG_GAP_MAX 2       // blank lines between paragraphs
#define CONFIG_INDENT_MAX 4    // first-line indent, in columns
#define CONFIG_MARGIN_MAX 4    // empty columns at each side of the text
#define CONFIG_LINE_MAX 3      // extra pixel rows between lines of a pixel font

typedef enum {
	DISPLAY_DUAL,
	DISPLAY_SINGLE, // Game Boy Macro: everything on the bottom screen
} DisplayMode;

typedef enum {
	FOOTER_BOTH,    // page count and percentage
	FOOTER_PAGE,    // page count only
	FOOTER_PERCENT, // percentage only
	FOOTER_NONE,    // neither
	FOOTER_COUNT,
} FooterInfo;

// The space above and below the text on the reading screens (decision 062): 8, 16 or 24 pixels,
// the step being a row of the text grid. Stored as a step 0 to 2.
#define CONFIG_VMARGIN_MAX 2
#define CONFIG_VMARGIN_DEFAULT 1 // 16 pixels

typedef struct {
	DisplayMode display;
	Theme theme;
	bool rotate;
	uint8_t paragraph_gap;    // 0 to CONFIG_GAP_MAX
	uint8_t paragraph_indent; // 0 to CONFIG_INDENT_MAX
	uint8_t margin;           // 0 to CONFIG_MARGIN_MAX
	uint8_t line_spacing;     // 0 to CONFIG_LINE_MAX, pixel fonts only
	FooterInfo footer;        // what the reading footer shows (decision 056)
	uint8_t vmargin;          // 0 to CONFIG_VMARGIN_MAX: 8, 16 or 24 pixels above and below the text (decision 062)
	char font[CONFIG_FONT_MAX];
} Config;

void config_set_defaults(Config *config);

// The space above and below the text on the reading screens in pixels: 8, 16 or 24 (decision 062).
int config_vmargin_px(const Config *config);

// Reads settings from text. `invert=0` and `invert=1` from older versions still
// work and mean the normal and the dark theme. Unknown keys, bad values and bad lines are ignored,
// so a damaged or hand-edited file never stops the app. Values not mentioned
// keep what `config` already holds.
void config_parse(Config *config, const char *text);

// Writes the settings as text. Returns the length, or -1 if `cap` is too small.
int config_format(const Config *config, char *buf, size_t cap);

// Loads the file. Starts from the defaults. Returns false if there was no
// readable file, which is normal on a first run.
bool config_load(Config *config);

// Saves the file safely. Returns false on failure.
bool config_save(const Config *config);

#endif
