/*
 * diskio.h - C port of the FS2 loader's drive-channel layer (C90).
 *
 * The original stage-2 loader ($7300-$7AFF) talks to a stock 1541 with
 * standard CBM DOS commands ("U1:2 0 tt ss" block-read, "U2" write,
 * "I" initialize, "N0:..." format) over its own IEC bit-bang routines.
 * We hook the five channel primitives and service them from a .d64
 * image, so all the higher loader logic (block math, sector bitmap,
 * error checking, copy protection) still runs as original 6502 code:
 *
 *   $7657 send buffer to channel (Y=0)   -> DOS command / OPEN / data
 *   $765B send buffer to channel (Y=1)   -> data into drive buffer
 *   $7618 receive channel into ($C2)     -> drive buffer / error string
 *   $75DD open channel                   -> success
 *   $75FC close channel                  -> success
 *
 * Drive model: one drive (device 8) holding at most one disk.  Original
 * images are mounted write-protected, as the commercial disks were, so a
 * format or block-write answers 26,WRITE PROTECT ON exactly like real
 * hardware.  A writable "user disk" is where the mode library is saved
 * (the manual says to supply your own disk); its writes are saved to its
 * file immediately.  Per-sector error bytes in .d64 images are honored -
 * the scenery disks' copy protection depends on them.
 */

#ifndef DISKIO_H
#define DISKIO_H

#include "c64.h"

/* Install the channel hooks.  The drive starts empty. */
void diskio_init(struct c64 *m);

/*
 * Insert a disk.  writable=0: original media, must exist.  writable=1:
 * a user disk; if the file does not exist yet, an unformatted blank disk
 * is inserted and the file is created on its first write.
 * Returns 0 on success.
 */
int diskio_mount(const char *path, int writable);

/* Remove the disk (drive empty -> 74,DRIVE NOT READY). */
void diskio_eject(void);

/* Path of the inserted disk, or NULL if the drive is empty. */
const char *diskio_disk_path(void);

/* Called after each write to a user disk file (web: sync IDBFS). */
void diskio_set_write_callback(void (*fn)(const char *path));

#endif
