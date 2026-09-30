/*
 * shelf.h - the player's disk shelf: which disk goes in the drive (C90).
 *
 * The shelf lists the original .d64 images found in a directory (all
 * mounted write-protected) plus one writable user disk for saving the
 * mode library.  The frontend steps through it to swap disks, as a
 * player would when FS2 asks for a scenery or save disk.
 */
#ifndef SHELF_H
#define SHELF_H

/* Scan dir for *.d64 (sorted by name) and add the user disk at the end.
   Returns the number of disks on the shelf. */
int shelf_init(const char *dir, const char *userdisk_path);

/* Insert the disk whose path equals path (or contains it); returns 0 if
   found and mounted. */
int shelf_insert_path(const char *path);

/* Insert the next (+1) or previous (-1) disk on the shelf. */
void shelf_step(int dir);

/* Short name of the inserted disk, for the window title. */
const char *shelf_label(void);

#endif
