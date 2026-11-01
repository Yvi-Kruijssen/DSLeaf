# DSLeaf

*A better way to read on DS.*

DSLeaf is an EPUB e-reader for the Nintendo DS, written in C with devkitARM and libnds. It is designed to work on a normal dual-screen DS or DS Lite and also on a single-screen Game Boy Macro, including builds that have no touchscreen and no X/Y buttons.

> **Status: early development.** Only a placeholder that prints the app name exists so far. Nothing below is implemented yet.

## Planned features

- Reads EPUB files from a `Books` folder on the SD card.
- **Main menu** with a "Recent Reads" list (3 books), plus Open, Library and Settings buttons.
- **Library** listing all EPUBs, sorted alphabetically.
- **Dual-screen and single-screen modes**, chosen in Settings. Single-screen mode draws everything on the bottom screen.
- **One page per screen**: two pages at once in dual-screen mode, one page in single-screen mode.
- **Rotated reading mode** for holding the device sideways like a book.
- **8x8 bitmap fonts**, switchable from a `Fonts` folder. The built-in font is [font8x8](https://github.com/dhepper/font8x8) (public domain).
- **Bookmarks** and reading progress saved as text files on the SD card, and an invert-colours mode that is remembered.
- **Buttons only is always enough.** Touch is optional, and the X and Y buttons are never required.

## Controls (planned)

| Where | Input | Action |
|-------|-------|--------|
| Menus | D-pad | Move |
| Menus | A | Select |
| Menus | B | Back |
| Menus | Select | Remove the highlighted book from Recent Reads |
| Reading | D-pad Right/Down, R | Next page |
| Reading | D-pad Left/Up, L | Previous page |
| Reading | A | Toggle bookmark |
| Reading | Select | Bookmark menu |
| Reading | B | Invert colours |
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

Put your `.epub` files in a `Books` folder at the root of the SD card. DSLeaf stores its own files (settings, bookmarks, extra fonts) in a `DSLeaf` folder on the same card.

## Licence

DSLeaf is released under the [MIT licence](LICENSE). Third-party parts keep their own licences, for example font8x8 is public domain.
