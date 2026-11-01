// Button to action mapping. No DS dependencies, so it can be tested on a PC.
#include "input.h"

#define DPAD (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT)

uint32_t input_map(InputContext context, uint32_t b) {
	uint32_t a = ACT_NONE;

	if (context == INPUT_MENU) {
		if (b & BTN_UP)     a |= ACT_UP;
		if (b & BTN_DOWN)   a |= ACT_DOWN;
		if (b & BTN_LEFT)   a |= ACT_LEFT;
		if (b & BTN_RIGHT)  a |= ACT_RIGHT;
		if (b & BTN_A)      a |= ACT_CONFIRM;
		if (b & BTN_B)      a |= ACT_BACK;
		if (b & BTN_SELECT) a |= ACT_REMOVE;
		return a;
	}

	if (b & (BTN_RIGHT | BTN_DOWN | BTN_R)) a |= ACT_NEXT_PAGE;
	if (b & (BTN_LEFT | BTN_UP | BTN_L))    a |= ACT_PREV_PAGE;
	if ((a & ACT_NEXT_PAGE) && (a & ACT_PREV_PAGE)) {
		a &= ~(uint32_t)(ACT_NEXT_PAGE | ACT_PREV_PAGE);
	}
	if (b & BTN_A)      a |= ACT_BOOKMARK;
	if (b & BTN_SELECT) a |= ACT_BOOKMARK_MENU;
	if (b & BTN_B)      a |= ACT_INVERT;
	if (b & BTN_START)  a |= ACT_MENU;
	return a;
}

const char *input_action_name(InputAction action) {
	switch (action) {
	case ACT_UP:            return "UP";
	case ACT_DOWN:          return "DOWN";
	case ACT_LEFT:          return "LEFT";
	case ACT_RIGHT:         return "RIGHT";
	case ACT_CONFIRM:       return "CONFIRM";
	case ACT_BACK:          return "BACK";
	case ACT_REMOVE:        return "REMOVE";
	case ACT_NEXT_PAGE:     return "NEXT_PAGE";
	case ACT_PREV_PAGE:     return "PREV_PAGE";
	case ACT_BOOKMARK:      return "BOOKMARK";
	case ACT_BOOKMARK_MENU: return "BOOKMARK_MENU";
	case ACT_INVERT:        return "INVERT";
	case ACT_MENU:          return "MENU";
	default:                return "NONE";
	}
}
