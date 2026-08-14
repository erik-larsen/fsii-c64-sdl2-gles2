/*
 * kbd.h - SDL scancode to C64 keyboard matrix mapping (C90).
 */
#ifndef KBD_H
#define KBD_H

/* Returns 1 and fills row/col if the scancode maps to a C64 key. */
int kbd_map(int sdl_scancode, int *row, int *col);

#endif
