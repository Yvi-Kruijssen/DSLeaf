// UTF-8 to glyph mapping for the Latin-1 8x8 font (docs: fonts.md).
#ifndef DSLEAF_TEXT_H
#define DSLEAF_TEXT_H

#include <stdint.h>

// A glyph code (decision 091): 0 to 255 is a Latin-1 glyph of the font, 0x100 and more is the Unicode code point of a letter
// the font has (Greek, Cyrillic), and GLYPH_FILL is the second cell of a glyph that is two cells wide.
typedef uint16_t Glyph;
#define GLYPH_FILL 0xFFFF

// Which scripts the font in use can draw itself (bits).
#define SCRIPT_ALPHABETS 1 // Greek and Cyrillic letters (font_ext.h)
#define SCRIPT_HYPHEN 4    // not a script: the layout breaks English words at hyphenation points (hyphen.h, decision 093). Part of the same set of switches so that it is in the page count key
#define SCRIPT_CJK 2       // Chinese, Japanese and Korean characters from the CJK font (cjk.h): two cells wide, GLYPH_FILL after the code

// Decodes one UTF-8 character at *s and advances *s past it. Writes the glyph
// indexes to draw into `out` and returns how many (1 to 3). Never returns 0,
// and always advances by at least one byte unless *s is the terminating NUL.
//
// Latin-1 characters map to themselves. Common typographic punctuation is
// replaced (curly quotes, dashes, ellipsis, no-break space). Control
// characters become a space. Anything else becomes '?'.
int text_utf8_next(const char **s, uint8_t out[3]);

// The same with the glyph codes of a font that can draw more than Latin-1: with SCRIPT_ALPHABETS in `scripts`, Greek and
// Cyrillic letters the font has give their own code, and the others are written in Latin letters (translit.h), as
// text_utf8_next always does (the menus use that one).
int text_utf8_next_ext(const char **s, Glyph out[3], int scripts);

#endif
