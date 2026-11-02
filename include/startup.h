// The startup screen sequence (docs: ui.md, decisions 018, 021, 047).
//
// The logo shows on the bottom screen for 3 seconds, then slides up onto the top
// screen in 2 seconds (decision 084). Any button or a touch skips the wait, and
// one during the slide finishes it. A button that is still held from starting
// the program is ignored for the first few frames. Then the bottom screen fades
// out, is switched to the main menu, and fades back in. This file has no DS code and is tested on
// a PC. The caller counts frames (the DS shows about 60 a second).
#ifndef DSLEAF_STARTUP_H
#define DSLEAF_STARTUP_H

#include <stdbool.h>

#define STARTUP_SHOW_FRAMES 180 // 3 seconds
#define STARTUP_IGNORE_FRAMES 12 // input is ignored this long after starting
#define STARTUP_SLIDE_FRAMES 120 // 2 seconds
#define STARTUP_SLIDE_PIXELS 192 // the height of a screen
#define STARTUP_FADE_FRAMES 16   // each fade: from full brightness to black, or back

typedef enum {
	STARTUP_SHOW,
	STARTUP_SLIDE,
	STARTUP_FADE_OUT,
	STARTUP_FADE_IN,
	STARTUP_DONE,
} StartupPhase;

typedef struct {
	StartupPhase phase;
	int frame;         // frames spent in the slide or a fade
	int slide;         // pixels the logo has moved up, 0 to STARTUP_SLIDE_PIXELS (it eases in and out)
	bool slide_done;   // true for one call: the slide has just ended
	bool switch_now;   // true for one call: the screen is black, show the next screen now
} Startup;

void startup_init(Startup *s);

// Call once per frame. `elapsed` is the number of frames since the logo first
// showed (it includes time spent loading). `pressed` is true if a button or the
// touchscreen was pressed this frame. Returns the brightness to set: 0 is normal
// and -16 is black.
int startup_update(Startup *s, int elapsed, bool pressed);

#endif
