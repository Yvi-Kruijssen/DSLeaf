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
	// The order of the list on screen (decision 060).
	SETTING_SCREEN,  // dual or single
	SETTING_ORIENTATION, // normal or rotated reading
	SETTING_THEME,
	SETTING_FONT,
	SETTING_FOOTER,  // page count and/or percentage in the reading footer
	SETTING_MARGIN,  // empty columns at each side
	SETTING_VMARGIN, // space above and below the text on the reading screens: 8, 16 or 24 pixels
	SETTING_INDENT,  // first-line indent
	SETTING_LINE,    // extra pixel rows between lines (pixel fonts only)
	SETTING_GAP,     // blank lines between paragraphs
	// Three buttons below the list, side by side: Left and Right move between them.
	SETTING_PREVIEW, // shows a sample page (A)
	SETTING_RESET,   // back to the defaults (asks for a second A)
	SETTING_ABOUT,   // the About screen: licences and notices (decision 077)
	SETTING_COUNT,
} Setting;

// Where things are on the Settings screen, in pixels (touch and drawing share them, decision 078).
#define SETTINGS_FIRST_ROW 5   // text row of the first setting
#define SETTINGS_VALUE_END 29  // the value ends at this column
#define SETTINGS_BUTTON_Y 124
#define SETTINGS_BUTTON_H 16
#define SETTINGS_BUTTON_W 70
#define SETTINGS_BUTTON_X0 16
#define SETTINGS_BUTTON_X1 93
#define SETTINGS_BUTTON_X2 170
#define SETTINGS_BACK_Y 160    // a touch from here down goes back (the hint line)

typedef struct {
	int selected;
	bool redraw;                       // something changed on screen
	bool confirm_reset;                // A was pressed once on Reset
	FontChoice fonts[FONT_CHOICES_MAX];
	int font_count;
} SettingsMenu;

typedef enum {
	SETTINGS_EVENT_NONE,
	SETTINGS_EVENT_CHANGED, // `setting` has a new value in the config
	SETTINGS_EVENT_PREVIEW, // show the sample page
	SETTINGS_EVENT_ABOUT,   // show the About screen
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

// What a touch at (x, y) does (decision 078): a row that is not selected gets selected (nothing else
// happens), a touch on the selected row changes its value, left or right of the middle of the value,
// a button is selected and pressed, and the hint line at the bottom goes back. Returns the action for
// settings_update (ACT_NONE if the touch only moved the selection).
uint32_t settings_touch(SettingsMenu *menu, const Config *config, int x, int y);

// A short explanation of a setting, in two lines of at most 30 characters.
void settings_help(Setting setting, const char **line1, const char **line2);

// ---- Drawing (settings_view.c, needs the DS) ----

#include "render.h"

// Draws the screen if anything changed. Returns true when it drew.
bool settings_draw(SettingsMenu *menu, const Config *config, Screen screen);

#endif
