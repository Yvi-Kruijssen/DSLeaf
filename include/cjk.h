// The optional CJK font: Chinese, Japanese and Korean characters from a file on the SD card (docs: decision 092).
//
// A file ending in .dslc in /DSLeaf/Fonts (made with tools/bdf2cjk.py) holds the glyphs of the wide characters: each is
// drawn two cells wide. The glyphs stay on the card and are read one at a time, with a cache of the latest, so the file
// can be large (about 1 MB for the common characters at 12 pixels). Nothing is built in. No DS code, so it is tested on a PC.
#ifndef DSLEAF_CJK_H
#define DSLEAF_CJK_H

#include <stdbool.h>
#include <stdint.h>

#define CJK_SIZE_MAX 16

// Opens the first .dslc file of /DSLeaf/Fonts (sorted by name). Returns false if there is none or it is not a CJK font; the
// font is then unloaded. Calling it again reads the folder again.
bool cjk_load(void);
void cjk_unload(void);
bool cjk_loaded(void);

// The size of the glyphs in pixels (0 when no font is loaded).
int cjk_width(void);
int cjk_height(void);

// True if the font has a range that holds the code point.
bool cjk_has(uint32_t cp);

// The rows of a glyph, from the top, bit 0 the leftmost pixel, `cjk_height()` of them. False if it is not available.
bool cjk_rows(uint32_t cp, uint16_t rows[CJK_SIZE_MAX]);

// Whether a character is a wide one, drawn two cells wide when the font has it (text.c uses this).
bool cjk_is_wide(uint32_t cp);

#endif
