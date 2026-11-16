# DSLeaf

*A better way to read on DS.*

DSLeaf is an e-reader for the Nintendo DS that reads EPUB and plain text books from the SD card of a flashcart. It is written in C with devkitARM and libnds. It works on a normal dual-screen DS or DS Lite and on a single-screen Game Boy Macro, including builds that have no touchscreen and no X/Y buttons.

**Version 1.0.0.** Everything below works and has been used on real hardware: a DS Lite and a self-made Game Boy Macro (no touchscreen, no X/Y buttons), both with an Ace3ds X flashcart. Other flashcarts have not been tried yet; it should run on any card that supports DLDI. It also runs in the melonDS emulator.

## Features

- Reads EPUB and plain text (`.txt`) files from a `Books` folder on the SD card.
- **Pictures in EPUBs** (PNG and baseline JPEG): each picture is a page of its own, shrunk to fit and dithered. The next picture is decoded while you read the page before it, so it is there when you turn the page, and the last few are kept so turning back is quick. Small ornaments and thin strips are left out.
- **Page numbers for the whole book** (`10 / 2000`) and a percentage in the footer. The pages are counted in the background while you read (about 10 seconds for a novel on a DS Lite) and remembered for next time.
- **Made for a slow machine**: a book of 25 MB opens at once and reopens where you left it in about a second, and nothing is loaded whole into the DS's 4 MB of memory.
- **Settings screen** for the screen mode, colour theme, font, line spacing, orientation, paragraph spacing, indent, margins (sides, and above and below for a glass screen protector) and what the reading footer shows ("Book progress": pages, percent, both or nothing), with a **sample page preview**, **Reset all** and an **About** screen with the licences. Changes are saved at once, and the screen works with touch as well as buttons.
- **Main menu** with a "Recent Reads" list (3 books), plus Open, Library and Settings buttons.
- **Library** listing the EPUB and text books in your `Books` folder, sorted alphabetically, with subfolders. It remembers where you were, and books you have read to the end get a check mark (Select marks or unmarks one by hand).
- **Dual-screen and single-screen modes**, chosen in Settings. Single-screen mode draws everything on the bottom screen.
- **One page per screen**: two pages at once in dual-screen mode, one page in single-screen mode.
- **Rotated reading mode** for holding the device sideways like a book.
- **English hyphenation** at line ends (a setting), with the TeX patterns, and text files in the old Chinese, Japanese, Korean and Cyrillic encodings (GBK, Big5, Shift-JIS, EUC-KR, Windows-1251).
- **Greek and Cyrillic** in the built-in 6x12 font, and **Chinese, Japanese and Korean** with an optional font file on the SD card (made from a BDF font with `tools/bdf2cjk.py`).
- **Bitmap fonts**: the built-in 8x8 font is [font8x8](https://github.com/dhepper/font8x8) (public domain) and a 6x12 font (from Terminus Font, SIL Open Font License) is built in too. Extra fonts up to 8x16 can be put in a `Fonts` folder on the SD card, with a converter script in `tools/`.
- **Table of contents** (EPUB 2 and 3), **bookmarks** (20 per book) and **go to page**, in one menu with tabs. Books without a table of contents get one made from their chapters, and bookmarks show the page they are on.
- **Three colour themes** (normal, dark and sepia, menus included). Settings, bookmarks and your place in every book are saved as plain text files on the SD card.
- **A startup screen** with the DSLeaf logo.
- **Buttons only is always enough.** Touch is optional, and the X and Y buttons are never required.

## Not supported

Books with DRM, PDF files, progressive JPEG pictures (they show "Picture not supported"), CSS and embedded fonts, links inside a book, and hyphenation for languages other than English.

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

## Installing

1. Copy `DSLeaf.nds` to the SD card of your flashcart (build it as described under "Building").
2. Put your books on the card as described below, and start DSLeaf from the flashcart's menu.

### Books

Put your `.epub` and `.txt` files in a `Books` folder at the root of the SD card (subfolders are fine, up to 6 deep). DSLeaf stores its own files (settings, bookmarks, extra fonts) in a `DSLeaf` folder on the same card. The default reading font is the 6x12 font built into the program (a converted copy of Terminus 6x12, see the licences below); the 8x8 font is built in too. To use another font, put a `.fnt` file in `DSLeaf/Fonts` (8x8, 8x16 or other sizes such as 6x12, made with `tools/bdf2fnt.py`) and pick it in Settings.

### Chinese, Japanese and Korean

The CJK font is not part of the program (it would double its size), so it is a file you add to the card:

1. Make a `.dslc` file with `tools/bdf2cjk.py` from BDF or PCF bitmap fonts that have the characters, at **12 pixels** so that it fits the built-in 6x12 font (for example a 12 pixel WenQuanYi bitmap font for Chinese and Japanese, Galmuri11 for Korean and GNU Unifont to fill the gaps). Examples are at the top of the script. Check the licence of the fonts you use.
2. Copy the file to `DSLeaf/Fonts` on the SD card (the first `.dslc` in name order is used).
3. **Restart DSLeaf.** The file is read when the program starts.
4. Choose the **Built-in 6x12** font in Settings. The 8x8 font and the other fonts cannot draw these characters.

Without the file, Chinese, Japanese and Korean text shows as `?`, and a book in one of these languages says "No CJK font, see README" in the status row when it is opened. With the file but another font it says "CJK needs the 6x12 font". Menus, the Library and the contents always use the 8x8 font and show `?` for these characters.

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

If you keep the tests (they are not part of this repository), `make test` runs them on the PC.

## Licence

DSLeaf is released under the [MIT licence](LICENSE). Third-party parts keep their own licences:

- **font8x8** (the built-in 8x8 font) is public domain.
- The built-in **6x12 font** is a converted copy of [Terminus Font](https://terminus-font.sourceforge.net/) (6x12, regular), Copyright (C) 2020 Dimitar Toshkov Zhekov, under the **SIL Open Font License 1.1**. Its data (`source/font_6x12.c`) stays under that licence, not the MIT licence, and it is not called Terminus in the program because of the licence's Reserved Font Name. The licence text and the details are in [`third_party/terminus/`](third_party/terminus/). If you give out a built `DSLeaf.nds`, give out that licence file with it.
