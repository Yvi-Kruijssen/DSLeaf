// The fonts built into the program (docs: fonts.md, decision 073).
//
// "default" is the 8x8 font8x8 (public domain, font_default.c). "builtin-6x12" is a 6x12 font in the DSLeaf
// .fnt format (font_6x12.c), a converted copy of Terminus 6x12 under the SIL Open Font License, which is shown
// to the user as "Built-in 6x12" because the licence does not allow a converted copy to use the name Terminus.
#ifndef DSLEAF_FONT_BUILTIN_H
#define DSLEAF_FONT_BUILTIN_H

#define FONT_NAME_8X8 "default"
#define FONT_NAME_6X12 "builtin-6x12"

extern const unsigned char font_6x12_data[];
extern const unsigned long font_6x12_size;

#endif
