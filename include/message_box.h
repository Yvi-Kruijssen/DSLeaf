// A simple message box drawn over whatever is on a screen (docs: ui.md, decision 023).
#ifndef DSLEAF_MESSAGE_BOX_H
#define DSLEAF_MESSAGE_BOX_H

#include "render.h"

// Draws a 28 column box in the middle of the screen with a heading and up to
// two lines of UTF-8 text. Long lines are cut. Closing it is up to the caller,
// which has to redraw the screen underneath.
void message_box_draw(Screen screen, const char *heading, const char *line1, const char *line2);

#endif
