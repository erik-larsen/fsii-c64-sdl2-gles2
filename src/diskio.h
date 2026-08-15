/*
 * diskio.h - C port of the FS2 loader's drive-channel layer (C90).
 *
 * The original stage-2 loader ($7300-$7AFF) talks to a stock 1541 with
 * standard CBM DOS commands ("U1:2 0 tt ss" block-read, "U2" write,
 * "I" initialize, "N0:..." format) over its own IEC bit-bang routines.
 * We hook the five channel primitives and service them from a .d64
 * image, so all the higher loader logic (block math, sector bitmap,
 * error checking) still runs as original 6502 code:
 *
 *   $7657 send buffer to channel (Y=0)   -> DOS command / ignored
 *   $765B send buffer to channel (Y=1)   -> data into drive buffer
 *   $7618 receive channel into ($C2)     -> drive buffer / error string
 *   $75DD open channel                   -> no-op success
 *   $75FC close channel                  -> no-op success
 */

#ifndef DISKIO_H
#define DISKIO_H

#include "c64.h"

/* Load a .d64 (35/40 track, with or without error info) and install
   the channel hooks.  Returns 0 on success. */
int diskio_init(struct c64 *m, const char *d64_path);

/* Swap in a different disk image (scenery disks) at runtime. */
int diskio_swap(const char *d64_path);

#endif
