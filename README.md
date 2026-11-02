# DSLeaf

*A better way to read on DS.*

DSLeaf is an EPUB e-reader for the Nintendo DS, written in C with devkitARM and libnds. It is designed to work on a normal dual-screen DS or DS Lite and also on a single-screen Game Boy Macro, including builds that have no touchscreen and no X/Y buttons.

> **Status: early development.** Reading works end to end: the menus, the Library, the reader, bookmarks, the table of contents, Settings and the startup screen. It has been tried in the melonDS emulator and on a DS Lite. Images, and testing on a Game Boy Macro and more flashcarts, are still to come.

## Features

- Reads EPUB and plain text (`.txt`) files from a `Books` folder on the SD card.
- **Settings screen** for the screen mode, colour theme, font, line spacing, orientation, paragraph spacing, indent, margins and what the reading footer shows (page count, percentage, both or nothing), with a **sample page preview** and a **Reset all** option. Changes are saved at once.
- **Main menu** with a "Recent Reads" list (3 books), plus Open, Library and Settings buttons.
- **Library** listing all EPUBs, sorted alphabetically.
- **Dual-screen and single-screen modes**, chosen in Settings. Single-screen mode draws everything on the bottom screen.
- **One page per screen**: two pages at once in dual-screen mode, one page in single-screen mode.
- **Rotated reading mode** for holding the device sideways like a book.
- **Bitmap fonts**: the built-in 8x8 font is [font8x8](https://github.com/dhepper/font8x8) (public domain) and a 6x12 font (from Terminus Font, SIL Open Font License) is built in too. Extra fonts up to 8x16 can be put in a `Fonts` folder on the SD card, with a converter script in `tools/`.
- **Table of contents** (EPUB 2 and 3) and **bookmarks** (20 per book), in one menu with tabs to jump around the book, including a jump to a page number once the pages of the book have been counted. Books without a table of contents get one made from their chapters. A book reopens where you left it. Everything is saved as text files on the SD card, and three colour themes (normal, dark and sepia) are remembered.
- **A startup screen** with the DSLeaf logo.
- **Buttons only is always enough.** Touch is optional, and the X and Y buttons are never required.

## Controls

| Where | Input | Action |
|-------|-------|--------|
| Menus | D-pad | Move (Left/Right change a value in Settings) |
| Menus | A | Select |
| Menus | B | Back |
| Menus | Select | Remove the highlighted book from Recent Reads |
| Reading | D-pad Right/Down, R | Next page |
| Reading | D-pad Left/Up, L | Previous page |
| Reading | A | Toggle bookmark |
| Reading | Select | Contents, bookmarks and go-to-page box (L/R switch tabs) |
| Reading | B | Switch colour theme |
| Reading | Start | Back to the main menu |

## Building

You need [devkitPro](https://devkitpro.org/) with devkitARM, libnds and libfat installed, and `DEVKITARM` set in your environment.

```sh
make            # builds DSLeaf.nds
make clean      # removes build output
```

To try it in an emulator, run it with [melonDS](https://melonds.kuribo64.net/):

```sh
melonDS DSLeaf.nds
```

For real hardware, copy `DSLeaf.nds` to the SD card of your flashcart.

## Installing books

Put your `.epub` and `.txt` files in a `Books` folder at the root of the SD card. DSLeaf stores its own files (settings, bookmarks, extra fonts) in a `DSLeaf` folder on the same card. The default reading font is the 6x12 font built into the program (a converted copy of Terminus 6x12, see the licences below); the 8x8 font is built in too. To use another font, put a `.fnt` file in `DSLeaf/Fonts` (8x8, 8x16 or other sizes such as 6x12, made with `tools/bdf2fnt.py`) and pick it in Settings.

## Licence

DSLeaf is released under the [MIT licence](LICENSE). Third-party parts keep their own licences:

- **font8x8** (the built-in 8x8 font) is public domain.
- The built-in **6x12 font** is a converted copy of [Terminus Font](https://terminus-font.sourceforge.net/) (6x12, regular), Copyright (C) 2020 Dimitar Toshkov Zhekov, under the **SIL Open Font License 1.1**. Its data (`source/font_6x12.c`) stays under that licence, not the MIT licence, and it is not called Terminus in the program because of the licence's Reserved Font Name. The licence text and the details are in [`third_party/terminus/`](third_party/terminus/). If you give out a built `DSLeaf.nds`, give out that licence file with it.
