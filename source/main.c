// Input layer demo, replaced by the real app as it is built.
// Menu context:    B switches to the reading context, Select exits.
// Reading context: Start switches back to the menu, B inverts the colours.
// The log shows each action as it happens.
#include <nds.h>
#include <stdio.h>
#include <string.h>

#include "input.h"
#include "render.h"

#define LOG_LINES 14
#define LOG_WIDTH 30

static Font font;
static char log_lines[LOG_LINES][LOG_WIDTH + 1];
static int frame;

static const InputAction all_actions[] = {
	ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT, ACT_CONFIRM, ACT_BACK, ACT_REMOVE,
	ACT_NEXT_PAGE, ACT_PREV_PAGE, ACT_BOOKMARK, ACT_BOOKMARK_MENU, ACT_INVERT, ACT_MENU,
};

static void log_line(const char *text) {
	memmove(log_lines[0], log_lines[1], sizeof(log_lines) - sizeof(log_lines[0]));
	snprintf(log_lines[LOG_LINES - 1], sizeof(log_lines[0]), "%s", text);
}

static void draw(InputContext context, int taps) {
	render_clear(SCREEN_TOP);
	render_puts(SCREEN_TOP, 1, 0, "Input demo", STYLE_HIGHLIGHT);
	render_puts(SCREEN_TOP, 1, 2, context == INPUT_MENU ? "Context: MENU" : "Context: READING",
	            STYLE_NORMAL);
	render_puts(SCREEN_TOP, 1, 3, context == INPUT_MENU
	                                  ? "B reading  Select exit"
	                                  : "Start menu  B invert",
	            STYLE_DIM);
	for (int i = 0; i < LOG_LINES; i++) {
		render_puts(SCREEN_TOP, 1, 5 + i, log_lines[i], i == LOG_LINES - 1 ? STYLE_HIGHLIGHT : STYLE_NORMAL);
	}

	char buf[32];
	render_clear(SCREEN_BOTTOM);
	render_puts(SCREEN_BOTTOM, 1, 0, "Touch is optional", STYLE_HIGHLIGHT);
	snprintf(buf, sizeof buf, "Taps: %d", taps);
	render_puts(SCREEN_BOTTOM, 1, 2, buf, STYLE_NORMAL);
}

int main(void) {
	render_init();
	font_load_default(&font);
	render_set_font(&font);
	input_init();

	InputContext context = INPUT_MENU;
	input_set_context(context);
	int taps = 0;
	log_line("Press buttons...");
	draw(context, taps);

	while (pmMainLoop()) {
		swiWaitForVBlank();
		frame++;
		input_update();

		uint32_t actions = input_actions();
		bool changed = false;

		for (unsigned i = 0; i < sizeof all_actions / sizeof all_actions[0]; i++) {
			if (!(actions & all_actions[i])) continue;
			char buf[LOG_WIDTH + 1];
			snprintf(buf, sizeof buf, "%5d %s", frame, input_action_name(all_actions[i]));
			log_line(buf);
			changed = true;
		}

		int x, y;
		if (input_tap(&x, &y)) {
			char buf[LOG_WIDTH + 1];
			snprintf(buf, sizeof buf, "%5d TAP %d,%d", frame, x, y);
			log_line(buf);
			taps++;
			changed = true;
		}

		if (context == INPUT_MENU && (actions & ACT_REMOVE)) break;
		if (context == INPUT_MENU && (actions & ACT_BACK)) {
			context = INPUT_READING;
			input_set_context(context);
		} else if (context == INPUT_READING && (actions & ACT_MENU)) {
			context = INPUT_MENU;
			input_set_context(context);
		} else if (context == INPUT_READING && (actions & ACT_INVERT)) {
			render_set_inverted(!render_is_inverted());
		}

		if (changed) draw(context, taps);
	}

	return 0;
}
