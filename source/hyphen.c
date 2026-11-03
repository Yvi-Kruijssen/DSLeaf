#include "hyphen.h"

#include <string.h>

// Compares a key (letters and dots) with a pattern written with digits between its letters: the digits are skipped.
// Returns <0, 0 or >0 like strcmp, treating the end of the key as the end of the pattern's letters.
static int compare(const char *key, int len, const char *pat) {
	int i = 0;
	for (; *pat; pat++) {
		if (*pat >= '0' && *pat <= '9') continue;
		if (i >= len) return -1;       // the key ended first: it is smaller
		if (key[i] != *pat) return (unsigned char)key[i] < (unsigned char)*pat ? -1 : 1;
		i++;
	}
	return i == len ? 0 : 1;           // the pattern ended first: the key is larger
}

static const char *find_pattern(const char *key, int len) {
	int lo = 0, hi = hyph_pattern_count - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		int c = compare(key, len, hyph_patterns[mid]);
		if (c == 0) return hyph_patterns[mid];
		if (c < 0) hi = mid - 1;
		else lo = mid + 1;
	}
	return NULL;
}

// An exception: the word with its hyphens marked, such as "as-so-ciate". Binary search by the letters.
static const char *find_exception(const char *word, int n) {
	int lo = 0, hi = hyph_exception_count - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		const char *e = hyph_exceptions[mid];
		int i = 0, c = 0;
		for (; *e; e++) {
			if (*e == '-') continue;
			if (i >= n) { c = -1; break; }          // the word ended first: it is smaller
			if (word[i] != *e) { c = (unsigned char)word[i] < (unsigned char)*e ? -1 : 1; break; }
			i++;
		}
		if (c == 0 && i < n) c = 1;                // the exception ended first: the word is larger
		if (c == 0) return hyph_exceptions[mid];
		if (c < 0) hi = mid - 1;
		else lo = mid + 1;
	}
	return NULL;
}

int hyphen_points(const char *word, int n, bool points[HYPHEN_WORD_MAX + 1]) {
	for (int i = 0; i <= HYPHEN_WORD_MAX; i++) points[i] = false;
	if (n < HYPHEN_LEFT_MIN + HYPHEN_RIGHT_MIN || n > HYPHEN_WORD_MAX) return 0;

	int count = 0;
	const char *exc = find_exception(word, n);
	if (exc) {
		int letters = 0;
		for (const char *e = exc; *e; e++) {
			if (*e == '-') {
				if (letters >= 1 && letters < n) {
					points[letters] = true;
					count++;
				}
			} else {
				letters++;
			}
		}
		return count;
	}

	// The word between dots, and for every place between two of those characters the highest number any pattern gives.
	char key[HYPHEN_WORD_MAX + 3];
	key[0] = '.';
	memcpy(key + 1, word, (size_t)n);
	key[n + 1] = '.';
	int total = n + 2;
	uint8_t value[HYPHEN_WORD_MAX + 4];
	memset(value, 0, sizeof value);

	for (int start = 0; start < total; start++) {
		for (int len = 1; len <= hyph_max_pattern && start + len <= total; len++) {
			const char *pat = find_pattern(key + start, len);
			if (!pat) continue;
			// value[start + j] is the number before the j-th letter of the pattern (j = 0 to len).
			int j = 0;
			for (const char *p = pat; *p; p++) {
				if (*p >= '0' && *p <= '9') {
					if ((uint8_t)(*p - '0') > value[start + j]) value[start + j] = (uint8_t)(*p - '0');
				} else {
					j++;
				}
			}
		}
	}
	// value[k + 1] is the place between letter k and letter k + 1 of the word (the dot is character 0).
	for (int k = HYPHEN_LEFT_MIN; k <= n - HYPHEN_RIGHT_MIN; k++) {
		if (value[k + 1] & 1) {
			points[k] = true;
			count++;
		}
	}
	return count;
}
