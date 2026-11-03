// The old double-byte encodings of Chinese, Japanese and Korean text files (docs: decision 094): GBK (GB2312), Big5,
// Shift-JIS and EUC-KR, read with tables made by tools/make_mbcs.py. No DS code, so it is tested on a PC.
#ifndef DSLEAF_MBCS_H
#define DSLEAF_MBCS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { MB_GBK, MB_BIG5, MB_SJIS, MB_EUCKR, MB_COUNT } MbKind;

// The tables (mbcs_tables.c).
extern const uint16_t mb_gbk[], mb_big5[], mb_sjis[], mb_euckr[];
extern const uint16_t cp1251_high[128]; // Windows-1251 (Cyrillic): the characters of the bytes 0x80 to 0xFF

// Whether a byte can start a two byte character in the encoding.
bool mb_is_lead(MbKind kind, int byte);

// The character of a lead and a trail byte as a Unicode code point, or 0 if the pair is not a character of the encoding.
uint16_t mb_decode(MbKind kind, int lead, int trail);

#endif
