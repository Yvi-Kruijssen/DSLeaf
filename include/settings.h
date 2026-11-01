// The Settings screen (docs: ui.md, decision 046). The logic has no DS code and
// is tested on a PC. Drawing is in settings_view.c.
//
// Up and Down move between settings, Left and Right change the value (A moves
// it forward as well), B goes back. Values wrap around.
#ifndef DSLEAF_SETTINGS_H
#define DSLEAF_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "font.h"

typedef enum {
	SETTING_SCREEN,  // dual or single
	SETTING_THEME,
	SETTING_FONT,
	SETTING_GAP,     // blank lines between paragraphs
	SETTING_INDENT,  // first-line indent
	SETTING_MARGIN,  // empty columns at each side
	SETTING_COUNT,
} Setting;

typedef struct {
	int selected;
	bool redraw;                       // something changed on screen
	FontChoice fonts[FONT_CHOICES_MAX];
	int font_count;
} SettingsMenu;

typedef enum {
	SETTINGS_EVENT_NONE,
	SETTINGS_EVENT_CHANGED, // `setting` has a new value in the config
	SETTINGS_EVENT_BACK,
} SettingsEventKind;

typedef struct {
	SettingsEventKind kind;
	Setting setting;
} SettingsEvent;

// Reads the list of fonts (the built-in one and the files in /DSLeaf/Fonts).
void settings_open(SettingsMenu *menu);

// Applies one frame of input to the menu and the config. The caller saves the
// config when a value changed.
SettingsEvent settings_update(SettingsMenu *menu, Config *config, uint32_t actions);

// Name of a setting, such as "Theme".
const char *settings_label(Setting setting);

// The current value as text, such as "Sepia" or "2 lines". For the font this is
// its name without ".fnt", or "(missing)" if the file is gone.
void settings_value_text(const SettingsMenu *menu, const Config *config, Setting setting, char *out, size_t cap);

// The size of the font in use, as listed (8x8 if it is not in the list).
void settings_font_size(const SettingsMenu *menu, const Config *config, int *width, int *height);

// A short explanation of a setting, in two lines of at most 30 characters.
void settings_help(Setting setting, const char **line1, const char **line2);

// ---- Drawing (settings_view.c, needs the DS) ----

#include "render.h"

// Draws the screen if anything changed. Returns true when it drew.
bool settings_draw(SettingsMenu *menu, const Config *config, Screen screen);

#endif
