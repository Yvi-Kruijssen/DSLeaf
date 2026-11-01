// Reading zip archives, which is what an EPUB is (docs: formats.md, decision 037).
//
// Supports stored and deflated entries, with the archive read straight from
// the SD card. Not supported (and refused cleanly): zip64, encryption, split
// archives and other compression methods.
#ifndef DSLEAF_ZIP_H
#define DSLEAF_ZIP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "inflate.h"

#define ZIP_NAME_MAX 256

typedef struct {
	char name[ZIP_NAME_MAX];
	uint32_t compressed_size;
	uint32_t size;          // uncompressed
	uint32_t crc32;
	uint32_t header_offset; // of the local header, inside the archive
	uint16_t method;        // 0 stored, 8 deflate
	uint16_t flags;
} ZipEntry;

typedef struct {
	FILE *file;
	uint32_t dir_offset; // start of the central directory
	uint32_t dir_size;
	uint16_t entry_count;
} Zip;

// Walks the entries one by one.
typedef struct {
	uint32_t pos;
	uint16_t index;
} ZipIter;

bool zip_open(Zip *zip, const char *path);
void zip_close(Zip *zip);

void zip_iter_start(const Zip *zip, ZipIter *iter);
// Next entry, or false at the end (or if the directory is damaged). Entries
// whose names are too long for ZIP_NAME_MAX are skipped.
bool zip_iter_next(Zip *zip, ZipIter *iter, ZipEntry *entry);

// Finds an entry by its exact, case sensitive name, such as "META-INF/container.xml".
bool zip_find(Zip *zip, const char *name, ZipEntry *entry);

// Reads the contents of one entry in pieces. Many streams can be open at once.
typedef struct {
	FILE *file;
	uint32_t pos;        // next byte to read from the archive
	uint32_t in_left;    // compressed bytes not read yet
	uint32_t out_left;   // uncompressed bytes not delivered yet
	bool deflated;
	Inflate *inflate;    // only for deflated entries (about 34 KB)
} ZipStream;

// Returns false if the entry cannot be read (encrypted, unknown method, bad header).
bool zip_stream_open(Zip *zip, const ZipEntry *entry, ZipStream *stream);

// Returns the number of bytes read, 0 at the end, or -1 on damaged data.
int zip_stream_read(ZipStream *stream, uint8_t *buf, size_t cap);
void zip_stream_close(ZipStream *stream);

// CRC-32 as used by zip. For checking, not needed while reading.
uint32_t zip_crc32(uint32_t crc, const uint8_t *data, size_t len);

#endif
