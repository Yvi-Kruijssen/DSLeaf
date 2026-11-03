// Finding where an English word may be hyphenated, with Liang's algorithm and the American English patterns of TeX
// (docs: decision 093). No DS code, so it is tested on a PC.
#ifndef DSLEAF_HYPHEN_H
#define DSLEAF_HYPHEN_H

#include <stdbool.h>
#include <stdint.h>

#define HYPHEN_WORD_MAX 40   // longer words are not hyphenated
#define HYPHEN_LEFT_MIN 2    // letters before the first hyphen (TeX's \lefthyphenmin for American English)
#define HYPHEN_RIGHT_MIN 3   // letters after the last one

// The data (hyph_en.c, made by tools/make_hyphen.py).
extern const int hyph_max_pattern;
extern const char *const hyph_patterns[];
extern const int hyph_pattern_count;
extern const char *const hyph_exceptions[];
extern const int hyph_exception_count;

// `word` is `n` letters, a to z in lower case (n from HYPHEN_LEFT_MIN + HYPHEN_RIGHT_MIN to HYPHEN_WORD_MAX). Sets
// points[k] for k = 1 to n - 1 to true if the word may be broken after its k-th letter. Returns how many points are set.
int hyphen_points(const char *word, int n, bool points[HYPHEN_WORD_MAX + 1]);

#endif
