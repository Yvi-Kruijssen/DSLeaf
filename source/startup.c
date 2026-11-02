#include "startup.h"

#include <stdint.h>

void startup_init(Startup *s) {
	s->phase = STARTUP_SHOW;
	s->frame = 0;
	s->slide = 0;
	s->slide_done = false;
	s->switch_now = false;
}

int startup_update(Startup *s, int elapsed, bool pressed) {
	s->switch_now = false;
	s->slide_done = false;

	switch (s->phase) {
	case STARTUP_SHOW:
		if (elapsed >= STARTUP_SHOW_FRAMES || (elapsed >= STARTUP_IGNORE_FRAMES && pressed)) {
			s->phase = STARTUP_SLIDE;
			s->frame = 0;
		}
		return 0;

	case STARTUP_SLIDE:
		if (pressed) s->frame = STARTUP_SLIDE_FRAMES; // a press finishes the slide
		else s->frame++;
		if (s->frame >= STARTUP_SLIDE_FRAMES) {
			s->slide = STARTUP_SLIDE_PIXELS;
			s->slide_done = true;
			s->phase = STARTUP_FADE_OUT;
			s->frame = 0;
		} else {
			// A slow start and a slow stop: 35t^4 - 84t^5 + 70t^6 - 20t^7 (the smoother step of order 7), in millionths.
			int64_t t = (int64_t)s->frame * 1000000 / STARTUP_SLIDE_FRAMES;
			int64_t t2 = t * t / 1000000, t3 = t2 * t / 1000000, t4 = t2 * t2 / 1000000;
			int64_t poly = 35000000 - 84 * t + 70 * t2 - 20 * t3;
			int64_t eased = t4 * poly / 1000000;
			s->slide = (int)(STARTUP_SLIDE_PIXELS * eased / 1000000);
		}
		return 0;

	case STARTUP_FADE_OUT:
		s->frame++;
		if (s->frame >= STARTUP_FADE_FRAMES) {
			s->phase = STARTUP_FADE_IN;
			s->frame = 0;
			s->switch_now = true; // it is black now
			return -16;
		}
		return -(s->frame * 16 / STARTUP_FADE_FRAMES);

	case STARTUP_FADE_IN:
		s->frame++;
		if (s->frame >= STARTUP_FADE_FRAMES) {
			s->phase = STARTUP_DONE;
			return 0;
		}
		return -16 + s->frame * 16 / STARTUP_FADE_FRAMES;

	case STARTUP_DONE:
		break;
	}
	return 0;
}
