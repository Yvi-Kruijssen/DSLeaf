// Reading the size and kind of a picture from its first bytes (docs: decision 075).
// Pure code, tested on a PC. Used to decide which pictures of a book get a page.
#ifndef DSLEAF_IMAGEINFO_H
#define DSLEAF_IMAGEINFO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { IMAGE_NONE, IMAGE_PNG, IMAGE_JPEG } ImageKind;

typedef struct {
	ImageKind kind;
	uint32_t width, height;
	bool progressive;   // JPEG: progressive (not supported), PNG: interlaced (supported)
	uint8_t components; // JPEG: 1 grey, 3 colour, 4 not supported
	uint8_t bit_depth;  // PNG
	uint8_t color_type; // PNG
} ImageInfo;

typedef enum { PROBE_OK, PROBE_NEED_MORE, PROBE_BAD } ProbeResult;

// Reads the header found in the first `len` bytes of a file. PROBE_NEED_MORE means the
// header may follow later (a long JPEG preamble), PROBE_BAD an unknown or damaged file.
ProbeResult image_probe(const uint8_t *data, size_t len, ImageInfo *info);

// Whether a picture of that size gets a page of its own. Small pictures (ornaments, icons)
// and thin strips (headings drawn as pictures) are left out.
bool image_wanted(uint32_t width, uint32_t height);

// The size at which a picture is shown on a screen of `sw` x `sh` pixels: scaled down to
// fit, and never enlarged.
void image_fit(uint32_t width, uint32_t height, int sw, int sh, int *out_w, int *out_h);

#endif
