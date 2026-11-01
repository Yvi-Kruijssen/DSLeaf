#include "startup.h"

void startup_init(Startup *s) {
	s->phase = STARTUP_SHOW;
	s->frame = 0;
	s->switch_now = false;
}

int startup_update(Startup *s, int elapsed, bool pressed) {
	s->switch_now = false;

	switch (s->phase) {
	case STARTUP_SHOW:
		if (elapsed >= STARTUP_SHOW_FRAMES || (elapsed >= STARTUP_IGNORE_FRAMES && pressed)) {
			s->phase = STARTUP_FADE_OUT;
			s->frame = 0;
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
