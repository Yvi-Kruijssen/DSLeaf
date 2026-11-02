// Picture decoders (docs: decision 075). Pure code, tested on a PC.
//
// A decoder reads the file through a read function and hands the picture to a sink one
// row at a time, as RGB (3 bytes a pixel). A JPEG may be decoded at a reduced size (1/2,
// 1/4 or 1/8) when the screen could not show the detail anyway, which is much faster. The
// size the sink gets is the size actually decoded.
#ifndef DSLEAF_IMAGEDEC_H
#define DSLEAF_IMAGEDEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "imageinfo.h"

// Supplies the bytes of the file. Returns how many were written (0 at the end).
typedef size_t (*ImageReadFn)(void *user, uint8_t *buf, size_t cap);

typedef struct {
	void *user;
	// Called once, before the first row, with the size of the picture as it will be delivered.
	bool (*begin)(void *user, int width, int height);
	// Row `y` (0 is the top), width * 3 bytes. Return false to stop (the decoder gives DEC_STOPPED).
	bool (*row)(void *user, int y, const uint8_t *rgb);
} ImageSink;

typedef enum {
	DEC_OK,
	DEC_UNSUPPORTED, // a kind of file this decoder does not do (progressive JPEG, 16 bit colour ...)
	DEC_BAD,         // damaged or truncated
	DEC_MEMORY,
	DEC_STOPPED,     // the sink asked to stop
} DecResult;

// `info` comes from image_probe. `want_w` and `want_h` are the size the picture will be shown
// at; a JPEG is decoded no smaller than that.
DecResult png_decode(ImageReadFn read, void *read_user, const ImageInfo *info, const ImageSink *sink);
DecResult jpeg_decode(ImageReadFn read, void *read_user, const ImageInfo *info, int want_w, int want_h,
                      const ImageSink *sink);

#endif
