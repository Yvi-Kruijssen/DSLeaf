// The "paper" look shared by the menus (docs: ui.md, decisions 053 and 059): a cream panel
// on the dark green background, a title with a green line, ruled lines, a green arrow and
// soft buttons. Everything here is a picture in the bitmap layer behind the 8x8 text, drawn
// after render_clear and render_backdrop_begin (paper_begin does the second one). Needs the DS.
#ifndef DSLEAF_PAPER_H
#define DSLEAF_PAPER_H

#include <stdbool.h>

#include "render.h"

#define PAPER_PANEL_X 4
#define PAPER_PANEL_Y 4
#define PAPER_PANEL_W 248
#define PAPER_RULE_X 12
#define PAPER_RULE_W 232

// Starts the picture and draws the panel, `height` pixels high from y = 4.
void paper_begin(Screen screen, int height);

// The title, in the accent colour on text row 1, with the line below it.
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
