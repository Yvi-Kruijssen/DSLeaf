// Settings saved in /DSLeaf/config.txt (docs: storage.md, decisions 003, 017, 025, 027).
#ifndef DSLEAF_CONFIG_H
#define DSLEAF_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#define CONFIG_PATH "/DSLeaf/config.txt"
#define CONFIG_FONT_MAX 64 // file name in /DSLeaf/Fonts, or "default"

typedef enum {
	DISPLAY_DUAL,
	DISPLAY_SINGLE, // Game Boy Macro: everything on the bottom screen
} DisplayMode;

typedef struct {
	DisplayMode display;
	bool invert;
	bool rotate;
	char font[CONFIG_FONT_MAX];
} Config;

void config_set_defaults(Config *config);

// Reads settings from text. Unknown keys, bad values and bad lines are ignored,
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
