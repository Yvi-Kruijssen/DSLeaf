#include "text.h"

static int one(uint8_t out[3], uint8_t a) {
	out[0] = a;
	return 1;
}

static int map_codepoint(uint32_t cp, uint8_t out[3]) {
	switch (cp) {
	case 0x00A0: return one(out, ' ');  // no-break space
	case 0x2018:
	case 0x2019: return one(out, '\''); // curly single quotes
	case 0x201C:
	case 0x201D: return one(out, '"');  // curly double quotes
	case 0x2013: return one(out, '-');  // en dash
	case 0x2014:                        // em dash
		out[0] = out[1] = '-';
		return 2;
	case 0x2026:                        // ellipsis
		out[0] = out[1] = out[2] = '.';
		return 3;
	}

	if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) return one(out, ' ');
	if (cp <= 0xFF) return one(out, (uint8_t)cp);
	return one(out, '?');
}

int text_utf8_next(const char **s, uint8_t out[3]) {
	const uint8_t *p = (const uint8_t *)*s;
	uint32_t cp;
	int extra;

	if (p[0] == 0) return one(out, ' ');

	if (p[0] < 0x80) {
		cp = p[0];
		extra = 0;
	} else if ((p[0] & 0xE0) == 0xC0) {
		cp = p[0] & 0x1F;
		extra = 1;
	} else if ((p[0] & 0xF0) == 0xE0) {
		cp = p[0] & 0x0F;
		extra = 2;
	} else if ((p[0] & 0xF8) == 0xF0) {
		cp = p[0] & 0x07;
		extra = 3;
	} else {
		*s += 1; // stray continuation byte or invalid lead byte
		return one(out, '?');
	}

	int used = 1;
	for (int i = 1; i <= extra; i++) {
		if ((p[i] & 0xC0) != 0x80) {
			// Truncated sequence: report one '?' and resume at the bad byte.
			*s += used;
			return one(out, '?');
		}
		cp = (cp << 6) | (p[i] & 0x3F);
		used++;
	}

	*s += used;
	return map_codepoint(cp, out);
}
