# Terminus Font (6x12), built into DSLeaf

DSLeaf includes a converted copy of **Terminus Font** (regular weight, 6x12: `ter-u12n.bdf` of Terminus Font
4.49.1, https://terminus-font.sourceforge.net/) by Dimitar Toshkov Zhekov.

- **Licence:** SIL Open Font License, Version 1.1. The full text is in [`OFL.txt`](OFL.txt), copied unchanged from the
  Terminus Font download. Copyright (C) 2020 Dimitar Toshkov Zhekov, with Reserved Font Name "Terminus Font".
- **What was changed (a Modified Version under the licence):** the BDF bitmap font was converted with
  `tools/bdf2fnt.py` to the DSLeaf `.fnt` format (glyphs U+0000 to U+00FF, 6x12, 3080 bytes) and written as a C array
  with `tools/fnt2c.py` into `source/font_6x12.c`. The shapes of the glyphs were not changed.
- **Name:** because of the Reserved Font Name, the converted font is **not** called Terminus in the program. It is shown
  as "Built-in 6x12" (and is named `builtin-6x12` in `config.txt`).
- **Distribution:** this font's data (`source/font_6x12.c`) stays under the SIL Open Font License, not the MIT licence of
  the rest of DSLeaf. A copy of DSLeaf that includes the program (a built `.nds` file) should be given out together with
  this folder's `OFL.txt` (or the same notice and licence in another form the user can easily read). The font may not be sold by itself.
- **Rebuilding it:** download Terminus Font 4.49.1, then
  `python3 -I tools/bdf2fnt.py ter-u12n.bdf font6x12.fnt` and `python3 -I tools/fnt2c.py font6x12.fnt source/font_6x12.c`.
