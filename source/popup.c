#include "popup.h"

#include <string.h>

PopupGeometry popup_geometry(bool rotated) {
	PopupGeometry g;
	if (!rotated) {
		g.logical_w = 256;
		g.logical_h = 192;
		g.x = 12;
		g.y = 16;
		g.w = 232;
		g.h = 160;   // y 16 to 175
	} else {
		g.logical_w = 192;
		g.logical_h = 256;
		g.x = 16;    // logical x is the screen's y: 16 to 175
		g.y = 12;
		g.w = 160;
		g.h = 230;   // logical y is the screen's x, reversed: 12 to 241 (the shadow, 3 pixels further, ends at x 10)
	}
	g.list_y = POPUP_HEADER_H + POPUP_TOP_GAP;
	g.footer_h = rotated ? POPUP_FOOTER2_H : POPUP_FOOTER_H;
	g.rows = (g.h - g.list_y - g.footer_h) / POPUP_ROW_H;
	return g;
}

void popup_to_screen(bool rotated, int x, int y, int w, int h, int *sx, int *sy, int *sw, int *sh) {
	if (!rotated) {
		*sx = x;
		*sy = y;
		*sw = w;
		*sh = h;
		return;
	}
	// Logical (x, y) is at screen (255 - y, x): the rectangle's top-right logical corner is its screen top-left.
	*sx = 255 - (y + h - 1);
	*sy = x;
	*sw = h;
	*sh = w;
}

void popup_pixel_to_screen(bool rotated, int x, int y, int *sx, int *sy) {
	if (!rotated) {
		*sx = x;
		*sy = y;
		return;
	}
	*sx = 255 - y;
	*sy = x;
}

void popup_screen_to_pixel(bool rotated, int sx, int sy, int *x, int *y) {
	if (!rotated) {
		*x = sx;
		*y = sy;
		return;
	}
	*x = sy;           // the inverse of (255 - y, x)
	*y = 255 - sx;
}

const char *popup_tab_name(ReadingTab tab, int level) {
	switch (tab) {
	case TAB_CONTENTS: return level == 0 ? "Contents" : "Index";
	case TAB_BOOKMARKS: return "Marks";
	default: return "Page";
	}
}

static int named_width(const char *name) {
	return POPUP_ICON_W + 3 + (int)strlen(name) * 8 + POPUP_TAB_PAD;
}

int popup_tab_layout(const PopupGeometry *g, int tabs, ReadingTab active, PopupTabBox out[POPUP_TABS_MAX], PopupRect *prev,
                     PopupRect *next) {
	if (tabs < 2) tabs = 2;
	if (tabs > POPUP_TABS_MAX) tabs = POPUP_TABS_MAX;
	prev->x = g->x;
	prev->y = g->y;
	prev->w = POPUP_SHOULDER_W;
	prev->h = POPUP_HEADER_H + 1;
	next->x = g->x + g->w - POPUP_SHOULDER_W;
	next->y = prev->y;
	next->w = POPUP_SHOULDER_W;
	next->h = prev->h;

	int area_x = prev->x + prev->w + 2, area_w = next->x - 2 - area_x;
	int gaps = (tabs - 1) * POPUP_TAB_GAP;
	int width[POPUP_TABS_MAX];
	const char *name[POPUP_TABS_MAX];

	// 1. Every tab with its name, the long one if all of them fit. 2. Every tab with the short name.
	// 3. The tab in use with its name and the others with only an icon.
	int level = -1;
	for (int l = 0; l < 2 && level < 0; l++) {
		int total = gaps;
		for (int t = 0; t < tabs; t++) total += named_width(popup_tab_name((ReadingTab)t, l));
		if (total <= area_w) level = l;
	}
	int spare = 0;
	if (level >= 0) {
		int total = gaps;
		for (int t = 0; t < tabs; t++) {
			name[t] = popup_tab_name((ReadingTab)t, level);
			width[t] = named_width(name[t]);
			total += width[t];
		}
		spare = area_w - total;
		for (int t = 0; t < tabs; t++) width[t] += spare / tabs + (t < spare % tabs ? 1 : 0); // the rest shared out
	} else {
		int others = tabs - 1;
		for (int t = 0; t < tabs; t++) {
			bool is_active = (int)t == (int)active;
			name[t] = is_active ? popup_tab_name((ReadingTab)t, 1) : NULL;
			width[t] = is_active ? area_w - gaps - others * POPUP_TAB_ICON_ONLY_W : POPUP_TAB_ICON_ONLY_W;
		}
	}
	int x = area_x;
	for (int t = 0; t < tabs; t++) {
		out[t].box.x = x;
		out[t].box.y = g->y + 1;                  // the tab in use reaches up to the top edge, and down to the line under the strip
		out[t].box.w = width[t];
		out[t].box.h = POPUP_HEADER_H;
		out[t].tab = (ReadingTab)t;
		out[t].name = name[t];
		out[t].active = t == (int)active;
		x += width[t] + POPUP_TAB_GAP;
	}
	return tabs;
}

int popup_hints(ReadingTab tab, PopupHint out[POPUP_HINTS_MAX]) {
	int n = 0;
	out[n++] = (PopupHint){ "A", "Go", POPUP_GO };
	out[n++] = (PopupHint){ "B", "Back", POPUP_BACK };
	if (tab == TAB_BOOKMARKS) out[n++] = (PopupHint){ "Sel", "Del", POPUP_DELETE };
	return n;
}

static int hint_width(const PopupHint *h) {
	return (int)strlen(h->keys) * 8 + 4 + 4 + (int)strlen(h->label) * 8;
}

int popup_hint_layout(const PopupGeometry *g, ReadingTab tab, PopupHintBox out[POPUP_HINTS_MAX]) {
	PopupHint hints[POPUP_HINTS_MAX];
	int n = popup_hints(tab, hints);
	int area_y = g->y + g->h - g->footer_h;
	int lines = g->footer_h >= POPUP_FOOTER2_H ? 2 : 1;
	int limit = g->x + g->w - 8, x = g->x + 8, line = 0, placed = 0;
	for (int i = 0; i < n; i++) {
		int w = hint_width(&hints[i]);
		if (x + w > limit) {
			if (line + 1 >= lines) break;
			line++;
			x = g->x + 8;
		}
		out[placed].box.x = x;
		out[placed].box.y = area_y + 6 + line * 14;
		out[placed].box.w = w;
		out[placed].box.h = 12;
		out[placed].hint = hints[i];
		placed++;
		x += w + 8;
	}
	return placed;
}

bool popup_position_fits(const PopupGeometry *g, ReadingTab tab, int text_width) {
	PopupHintBox boxes[POPUP_HINTS_MAX];
	int n = popup_hint_layout(g, tab, boxes);
	int end = g->x + 8;
	int first_line_y = g->y + g->h - g->footer_h + 6;
	for (int i = 0; i < n; i++) {
		if (boxes[i].box.y == first_line_y && boxes[i].box.x + boxes[i].box.w > end) end = boxes[i].box.x + boxes[i].box.w;
	}
	return end + 8 + text_width <= g->x + g->w - 8;
}

void popup_digit_layout(const PopupGeometry *g, int digits, int *x0, int *y0) {
	int total_w = (2 + digits) * 8;                          // "/ " and the total, which has as many digits
	int block = digits * POPUP_DIGIT_W - 2 + 12 + total_w;
	*x0 = g->x + (g->w - block) / 2;
	*y0 = g->y + g->list_y + 22;
}

static bool inside(const PopupRect *r, int x, int y, int grow) {
	return x >= r->x - grow && x < r->x + r->w + grow && y >= r->y - grow && y < r->y + r->h + grow;
}

PopupHit popup_hit(const PopupGeometry *g, ReadingTab tab, int tabs, int digits, int x, int y) {
	PopupHit none = { POPUP_HIT_NONE, 0 };
	if (x < g->x || x >= g->x + g->w || y < g->y || y >= g->y + g->h) return (PopupHit){ POPUP_HIT_OUTSIDE, 0 };

	// The tab bar.
	if (y < g->y + POPUP_HEADER_H + 1) {
		PopupTabBox boxes[POPUP_TABS_MAX];
		PopupRect prev, next;
		int n = popup_tab_layout(g, tabs, tab, boxes, &prev, &next);
		if (inside(&prev, x, y, 4) && x < boxes[0].box.x) return (PopupHit){ POPUP_HIT_TAB_PREV, 0 };
		if (inside(&next, x, y, 4) && x >= boxes[n - 1].box.x + boxes[n - 1].box.w) return (PopupHit){ POPUP_HIT_TAB_NEXT, 0 };
		for (int t = 0; t < n; t++) {
			// A tap in the gap goes to the nearer tab, so there is no dead strip between them.
			int left = boxes[t].box.x - (t > 0 ? POPUP_TAB_GAP / 2 + 1 : 4), right = boxes[t].box.x + boxes[t].box.w + (t < n - 1 ? POPUP_TAB_GAP / 2 : 4);
			if (x >= left && x < right) return (PopupHit){ POPUP_HIT_TAB, (int)boxes[t].tab };
		}
		return none;
	}

	// The key hints.
	PopupHintBox hints[POPUP_HINTS_MAX];
	int n = popup_hint_layout(g, tab, hints);
	for (int i = 0; i < n; i++) {
		if (inside(&hints[i].box, x, y, 3)) return (PopupHit){ POPUP_HIT_HINT, (int)hints[i].hint.action };
	}

	if (tab == TAB_PAGE) {
		int x0, y0;
		popup_digit_layout(g, digits, &x0, &y0);
		if (x >= x0 - 1 && x < x0 + digits * POPUP_DIGIT_W - 1) {
			int d = (x - (x0 - 1)) / POPUP_DIGIT_W;
			if (y >= y0 - 14 && y < y0) return (PopupHit){ POPUP_HIT_DIGIT_UP, d };
			if (y >= y0 && y < y0 + 18) return (PopupHit){ POPUP_HIT_DIGIT, d };
			if (y >= y0 + 18 && y < y0 + 34) return (PopupHit){ POPUP_HIT_DIGIT_DOWN, d };
		}
		return none;
	}

	// The list.
	int list_top = g->y + g->list_y, list_bottom = list_top + g->rows * POPUP_ROW_H;
	if (y >= list_top && y < list_bottom) {
		if (x >= g->x + g->w - 14) return (PopupHit){ y < (list_top + list_bottom) / 2 ? POPUP_HIT_SCROLL_UP : POPUP_HIT_SCROLL_DOWN, 0 };
		if (x >= g->x + 4) return (PopupHit){ POPUP_HIT_ROW, (y - list_top) / POPUP_ROW_H };
	}
	return none;
}
