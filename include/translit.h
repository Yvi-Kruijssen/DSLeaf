// Greek and Cyrillic letters in Latin letters (docs: decision 091).
#ifndef DSLEAF_TRANSLIT_H
#define DSLEAF_TRANSLIT_H

#include <stdint.h>

// Writes the Latin spelling of a Greek or Cyrillic letter into `out` and returns how many characters (1 to 3), or 0 if the
// code point is not a letter this knows.
int translit_letter(uint32_t cp, uint8_t out[3]);

#endif
