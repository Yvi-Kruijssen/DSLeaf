// Turns decoded rows into rows of palette indexes for the screen (docs: decision 075).
// Pure code, tested on a PC.
//
// The picture is shrunk to fit (each screen pixel is the average of the block of picture pixels
// it covers, a picture is never enlarged) and then dithered with a 4x4 ordered pattern to a fixed
// palette of PIC_PAL_COUNT colours: 32 greys (the screen has no more than that) and a 5x5x5 colour
// cube. Colours that are nearly grey use the greys, so black and white pictures keep their tones.
#ifndef DSLEAF_PICSINK_H
#define DSLEAF_PICSINK_H

#include <stdbool.h>
#include <stdint.h>

#include "imagedec.h"

#define PIC_PAL_BASE 80   // first palette entry the picture uses (0 to 79 are the text colours)
#define PIC_GREYS 32
#define PIC_CUBE 125
#define PIC_PAL_COUNT (PIC_GREYS + PIC_CUBE)

// Called for each finished row of the shrunk picture: `idx` has `w` palette entries
// already offset by PIC_PAL_BASE. Return false to stop.
typedef bool (*PicRowFn)(void *user, int y, int w, const uint8_t *idx);
// Called once the shrunk size is known (before the first row).
typedef void (*PicSizeFn)(void *user, int w, int h);

typedef struct {
	int area_w, area_h;       // the largest size the picture may have
	PicRowFn put_row;
	PicSizeFn put_size;       // may be NULL
	void *user;

	int src_w, src_h, dst_w, dst_h;
	uint32_t *acc;            // dst_w * 3 sums of the current output row
	uint16_t *col;            // output column of each picture column
	uint16_t *col_count;      // picture columns in each output column
	uint8_t *line;
	int cur;                  // output row being summed, -1 for none
	int rows_in;              // picture rows summed into it
	int stopped;
} PicSink;

// Sets `out` up as an ImageSink that feeds `ps`. The sink frees its memory in picsink_close.
void picsink_init(PicSink *ps, int area_w, int area_h, PicRowFn put_row, PicSizeFn put_size, void *user,
                  ImageSink *out);
// Delivers the last row. Returns false if the sink was stopped or nothing was decoded.
bool picsink_finish(PicSink *ps);
void picsink_close(PicSink *ps);

// The colours of the palette, 15 bit (RGB15) values for entries PIC_PAL_BASE up.
void picsink_palette(uint16_t out[PIC_PAL_COUNT]);

// The palette index (from 0, without PIC_PAL_BASE) for a pixel, at output position x, y.
int picsink_index(int r, int g, int b, int x, int y);

#endif
