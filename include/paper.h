// The "paper" look shared by the menus (docs: ui.md, decisions 053 and 059): a cream panel
// on the dark green background, a title with a green line, ruled lines, a green arrow and
// soft buttons. Everything here is a picture in the bitmap layer behind the 8x8 text, drawn
// after render_clear and render_backdrop_begin (paper_begin does the second one). Needs the DS.
#ifndef DSLEAF_PAPER_H
#define DSLEAF_PAPER_H

#include <stdbool.h>

#include "render.h"

// The menus keep 15 pixels free above and below, and 10 at the sides (decision 061): a glass
// protector on a Game Boy Macro can hide the edges of the screen. Nothing they draw is outside
// x 10 to 245 and y 15 to 176.
#define PAPER_SAFE_TOP 15
#define PAPER_SAFE_BOTTOM 15
#define PAPER_SAFE_SIDE 10
#define PAPER_PANEL_X PAPER_SAFE_SIDE
#define PAPER_PANEL_Y PAPER_SAFE_TOP
#define PAPER_PANEL_W (256 - 2 * PAPER_SAFE_SIDE)
#define PAPER_TITLE_Y 20      // the title is drawn at a pixel position (not on the text grid), and the line under it
#define PAPER_TITLE_LINE_Y 31
#define PAPER_FIRST_ROW 5     // the first text row of a list
#define PAPER_RULE_X (PAPER_PANEL_X + 8)
#define PAPER_RULE_W (PAPER_PANEL_W - 16)

// Starts the picture and draws the panel, `height` pixels high from y = 4.
void paper_begin(Screen screen, int height);

// The title, in the accent colour, centred, with the line below it.
void paper_title(Screen screen, const char *title);

// A thin ruled line just below the text on `row` (the paper lines of a list).
void paper_rule(Screen screen, int row);

// A band behind the text on `row`, for the selected line of a list.
void paper_band(Screen screen, int row);

// The green arrow in front of the text on `row`.
void paper_arrow(Screen screen, int row);

// A soft button. `focused` has the green outline and a lighter face, `pressed` is pushed in
// and `disabled` has dim text. The label is centred in the button.
void paper_button(Screen screen, int x, int y, int w, int h, const char *label, bool focused, bool pressed,
                  bool disabled);

// Small 8x8 text on the dark background below the panel, at a pixel position.
void paper_hint(Screen screen, int x, int y, const char *text);

#endif
