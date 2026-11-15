#include "hyphen.h"

#include <string.h>

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

// The children of the root of the trie by letter (index 0 is the dot, 1 to 26 are a to z), 0 for none. Made on first use.
static uint16_t root_child[27];
static bool root_ready;

static void build_root(void) {
	for (int child = hyph_first[0]; child < hyph_first[1]; child++) {
		uint8_t c = hyph_char[child];
		if (c == '.') root_child[0] = (uint16_t)child;
		else if (c >= 'a' && c <= 'z') root_child[c - 'a' + 1] = (uint16_t)child;
	}
	root_ready = true;
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

	// Every pattern that is in the word at `start` is on one path of the trie, so one walk finds them all: this was a
	// binary search of the whole list for every length before, most of the time of laying out a page (decision 114).
	if (!root_ready) build_root();
	for (int start = 0; start < total; start++) {
		int node = 0;
		for (int depth = 1; start + depth <= total; depth++) {
			uint8_t c = (uint8_t)key[start + depth - 1];
			int child;
			if (node == 0) { // the root has a child for nearly every letter: looked up, not searched
				if (c != '.' && (c < 'a' || c > 'z')) break; // not a letter: no pattern has it
				child = root_child[c == '.' ? 0 : c - 'a' + 1];
				if (child == 0) break;
			} else {
				int end = hyph_first[node + 1];
				child = hyph_first[node];
				while (child < end && hyph_char[child] < c) child++;
				if (child == end || hyph_char[child] != c) break;
			}
			node = child;
			if (hyph_value[node]) {
				// The pattern's numbers: digit[j] is the one before its j-th letter (j = 0 to depth).
				const uint8_t *digit = hyph_digits + hyph_value[node];
				for (int j = 0; j <= depth; j++) {
					if (digit[j] > value[start + j]) value[start + j] = digit[j];
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
