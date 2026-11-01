#include "input.h"

#include <nds.h>

#define REPEAT_DELAY 20 // frames before a held D-pad starts repeating
#define REPEAT_RATE  6  // frames between repeats

static InputContext context = INPUT_MENU;
static uint32_t actions;
static bool any_pressed;
static bool tapped;
static int tap_x, tap_y;

// Physical key bit for each Button.
static const struct { u32 key; Button button; } key_map[] = {
	{ KEY_A,      BTN_A },
	{ KEY_B,      BTN_B },
	{ KEY_SELECT, BTN_SELECT },
	{ KEY_START,  BTN_START },
	{ KEY_RIGHT,  BTN_RIGHT },
	{ KEY_LEFT,   BTN_LEFT },
	{ KEY_UP,     BTN_UP },
	{ KEY_DOWN,   BTN_DOWN },
	{ KEY_R,      BTN_R },
	{ KEY_L,      BTN_L },
};

static uint32_t to_buttons(u32 keys) {
	uint32_t buttons = 0;
	for (unsigned i = 0; i < sizeof key_map / sizeof key_map[0]; i++) {
		if (keys & key_map[i].key) buttons |= key_map[i].button;
	}
	return buttons;
}

void input_init(void) {
	keysSetRepeat(REPEAT_DELAY, REPEAT_RATE);
	context = INPUT_MENU;
}

void input_set_context(InputContext new_context) {
	context = new_context;
}

void input_update(void) {
	scanKeys();
	u32 down = keysDown();

	// Only the D-pad repeats, and only in menus.
	if (context == INPUT_MENU) {
		down |= keysDownRepeat() & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT);
	}

	actions = input_map(context, to_buttons(down));

	// Any button, X and Y included, or a touch counts. The lid switch does not.
	// Only used to skip things, never for a required action.
	any_pressed = (keysDown() & (KEY_TOUCH | KEY_X | KEY_Y | 0x03FF)) != 0;

	tapped = (keysDown() & KEY_TOUCH) != 0;
	if (tapped) {
		touchPosition touch;
		touchRead(&touch);
		tap_x = touch.px;
		tap_y = touch.py;
	}
}

uint32_t input_actions(void) { return actions; }

bool input_any_pressed(void) { return any_pressed; }

bool input_tap(int *x, int *y) {
	if (!tapped) return false;
	if (x) *x = tap_x;
	if (y) *y = tap_y;
	return true;
}
