// Colour themes (docs: decision 042).
#ifndef DSLEAF_THEME_H
#define DSLEAF_THEME_H

typedef enum {
	THEME_NORMAL, // dark text on a light, slightly warm page
	THEME_DARK,   // light grey text on a black page (decision 064)
	THEME_SEPIA,  // dark brown text on a tan page
	THEME_COUNT,
} Theme;

#endif
