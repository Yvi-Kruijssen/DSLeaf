#include "page_menu.h"

#include "input.h"

static int count_digits(int n) {
	int d = 1;
	while (n >= 10) {
		n /= 10;
		d++;
	}
	return d;
}

static int power10(int n) {
	int p = 1;
	while (n-- > 0) p *= 10;
	return p;
}

void page_menu_open(PageMenu *m, int current, int total) {
	if (total < 1) total = 1;
	if (current < 1) current = 1;
	if (current > total) current = total;
	m->total = total;
	m->value = current;
	m->digits = count_digits(total);
	m->cursor = m->digits - 1;
	m->changed = true;
}

int page_menu_digit(const PageMenu *m, int index) {
	return (m->value / power10(m->digits - 1 - index)) % 10;
}

PageEvent page_menu_update(PageMenu *m, uint32_t actions) {
	if (actions & ACT_BACK) return PAGE_EVENT_CLOSE;
	if (actions & ACT_CONFIRM) return PAGE_EVENT_GO;

	int before = m->value, cursor = m->cursor;
	int step = power10(m->digits - 1 - m->cursor);
	if (actions & ACT_UP) {
		long v = (long)m->value + step;
		m->value = v > m->total ? m->total : (int)v;
	} else if (actions & ACT_DOWN) {
		long v = (long)m->value - step;
		m->value = v < 1 ? 1 : (int)v;
	} else if (actions & ACT_LEFT) {
		if (m->cursor > 0) m->cursor--;
	} else if (actions & ACT_RIGHT) {
		if (m->cursor < m->digits - 1) m->cursor++;
	}
	if (m->value != before || m->cursor != cursor) m->changed = true;
	return PAGE_EVENT_NONE;
}
