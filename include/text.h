// UTF-8 to glyph mapping for the Latin-1 8x8 font (docs: fonts.md).
#ifndef DSLEAF_TEXT_H
#define DSLEAF_TEXT_H

#include <stdint.h>

// Decodes one UTF-8 character at *s and advances *s past it. Writes the glyph
// indexes to draw into `out` and returns how many (1 to 3). Never returns 0,
// and always advances by at least one byte unless *s is the terminating NUL.
//
// Latin-1 characters map to themselves. Common typographic punctuation is
// replaced (curly quotes, dashes, ellipsis, no-break space). Control
// characters become a space. Anything else becomes '?'.
int text_utf8_next(const char **s, uint8_t out[3]);

#endif
