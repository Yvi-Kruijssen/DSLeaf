// The startup screen sequence (docs: ui.md, decisions 018, 021, 047).
//
// The logo shows for 3 seconds, then fades out. Any button or a touch skips the
// wait. A button that is still held from starting the program is ignored for the
// first few frames. When the fade out ends the screen is switched to the main
// menu, and the screen fades back in. This file has no DS code and is tested on
// a PC. The caller counts frames (the DS shows about 60 a second).
#ifndef DSLEAF_STARTUP_H
#define DSLEAF_STARTUP_H

#include <stdbool.h>

#define STARTUP_SHOW_FRAMES 180 // 3 seconds
#define STARTUP_IGNORE_FRAMES 12 // input is ignored this long after starting
#define STARTUP_FADE_FRAMES 16   // each fade: from full brightness to black, or back

typedef enum {
	STARTUP_SHOW,
	STARTUP_FADE_OUT,
	STARTUP_FADE_IN,
	STARTUP_DONE,
} StartupPhase;

typedef struct {
	StartupPhase phase;
	int frame;         // frames spent in a fade
	bool switch_now;   // true for one call: the screen is black, show the next screen now
} Startup;

void startup_init(Startup *s);

// Call once per frame. `elapsed` is the number of frames since the logo first
// showed (it includes time spent loading). `pressed` is true if a button or the
// touchscreen was pressed this frame. Returns the brightness to set: 0 is normal
// and -16 is black.
int startup_update(Startup *s, int elapsed, bool pressed);

#endif
