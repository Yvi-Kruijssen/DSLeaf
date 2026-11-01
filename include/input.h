// Input layer (docs: decisions 007, 015, 033).
//
// Physical inputs are turned into abstract actions. The rules:
// - Everything works with buttons alone. Touch is optional and only exposed
//   as a tap, nothing may depend on it.
// - X and Y are never used. Traditional Game Boy Macro builds do not have them.
// - Page turns do not care which way the device is held, so rotated reading
//   needs no remapping.
#ifndef DSLEAF_INPUT_H
#define DSLEAF_INPUT_H

#include <stdbool.h>
#include <stdint.h>

// Buttons that may be used for required actions.
typedef enum {
	BTN_A      = 1 << 0,
	BTN_B      = 1 << 1,
	BTN_SELECT = 1 << 2,
	BTN_START  = 1 << 3,
	BTN_RIGHT  = 1 << 4,
	BTN_LEFT   = 1 << 5,
	BTN_UP     = 1 << 6,
	BTN_DOWN   = 1 << 7,
	BTN_R      = 1 << 8,
	BTN_L      = 1 << 9,
} Button;

// What the buttons mean depends on where the user is.
typedef enum {
	INPUT_MENU,    // all menus, including the bookmark menu
	INPUT_READING, // reading a book
} InputContext;

// Bit flags, so several actions can happen in one frame.
typedef enum {
	ACT_NONE          = 0,

	// Menus
	ACT_UP            = 1 << 0,
	ACT_DOWN          = 1 << 1,
	ACT_LEFT          = 1 << 2,
	ACT_RIGHT         = 1 << 3,
	ACT_CONFIRM       = 1 << 4,  // A: select the item
	ACT_BACK          = 1 << 5,  // B
	ACT_REMOVE        = 1 << 6,  // Select: remove the focused entry
	ACT_TAB_PREV      = 1 << 13, // L: the previous tab of a menu (decision 048)
	ACT_TAB_NEXT      = 1 << 14, // R: the next tab

	// Reading
	ACT_NEXT_PAGE     = 1 << 7,  // Right, Down, R
	ACT_PREV_PAGE     = 1 << 8,  // Left, Up, L
	ACT_BOOKMARK      = 1 << 9,  // A
	ACT_BOOKMARK_MENU = 1 << 10, // Select
	ACT_INVERT        = 1 << 11, // B
	ACT_MENU          = 1 << 12, // Start
} InputAction;

// Pure mapping from newly pressed buttons to actions. Opposing page turns in
// the same frame (for example Right and L) cancel out, and several buttons
// that mean the same thing give one action.
uint32_t input_map(InputContext context, uint32_t pressed_buttons);

// Short name such as "NEXT_PAGE" for one action bit, for logs and debugging.
const char *input_action_name(InputAction action);

// Call once at startup.
void input_init(void);

// Which mapping is active. Switching also changes key repeat: menus repeat
// the D-pad while held, reading does not so a held button cannot skip pages.
void input_set_context(InputContext context);

// Call once per frame, right after swiWaitForVBlank().
void input_update(void);

// Actions triggered in the current frame.
uint32_t input_actions(void);

// True in the frame when any button or the touchscreen is pressed, for
// example to skip the startup screen.
bool input_any_pressed(void);

// True in the frame a touch starts, with its position in screen pixels.
// Only useful as an extra way to do something buttons can already do.
bool input_tap(int *x, int *y);

#endif
