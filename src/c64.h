/*
 * c64.h - Minimal Commodore 64 machine model for the FS2 port (C90).
 *
 * Enough of a C64 to run the fully-loaded FS2 image faithfully:
 *   - 64K RAM with $01 banking (ROMs banked out; I/O window at $D000)
 *   - VIC-II: raster counter, raster IRQ, bitmap (hires+multicolor) and
 *     text fetch, 8 sprites, per-scanline register sampling
 *   - CIA1: keyboard matrix, timer A IRQ
 *   - CIA2: VIC bank select
 *   - SID: writes captured (audio TBD)
 *
 * The emulation is driven one scanline at a time so that mid-frame writes
 * to $D018/$D016/$DD00 (FS2's 3D-window / instrument-panel raster split)
 * take effect exactly where the original hardware would show them.
 */

#ifndef C64_H
#define C64_H

#include "cpu6502.h"

/* NTSC 6567R8 timing (SubLogic US release). */
#define C64_CYCLES_PER_LINE 65
#define C64_LINES_PER_FRAME 263
#define C64_FIRST_VISIBLE_LINE 51   /* first bitmap raster line (DISPLAY) */
#define C64_DISPLAY_W 320
#define C64_DISPLAY_H 200

struct c64 {
    struct cpu6502 cpu;
    unsigned char ram[65536];
    unsigned char color_ram[1024]; /* $D800-$DBFF, low nibble used */

    /* VIC-II registers ($D000-$D02E), stored raw. */
    unsigned char vic[0x40];
    int raster;                    /* current scanline 0..262 */
    int irq_line;                  /* asserted IRQ (level) from VIC/CIA */

    /* CIA1 */
    unsigned char cia1_pra, cia1_prb, cia1_ddra, cia1_ddrb;
    unsigned cia1_ta, cia1_ta_latch;
    unsigned char cia1_cra, cia1_icr_mask, cia1_icr_data;

    /* CIA2 */
    unsigned char cia2_pra; /* VIC bank in low 2 bits (inverted) */

    /* keyboard matrix: 8 rows x 8 cols, 1 bit per key, 1 = pressed */
    unsigned char keymatrix[8];

    /* framebuffer: DISPLAY_W x DISPLAY_H RGBA */
    unsigned char fb[C64_DISPLAY_W * C64_DISPLAY_H * 4];

    /*
     * Routine-replacement hooks: when the PC reaches a hooked address,
     * the C function runs instead of the 6502 routine and an RTS is
     * emulated.  This is how ported routines take over from original
     * code, one at a time.
     */
    struct c64_hook {
        unsigned addr;
        void (*fn)(struct c64 *m);
        const char *name;
    } hooks[32];
    int nhooks;
    unsigned char hook_at[65536]; /* 0 = none, else hook index + 1 */

    /* SID write log hook could go here later */
    unsigned long frame;

    /* debug: when set, render_line prints VIC state changes per line */
    int trace_lines;
};

void c64_init(struct c64 *m);
/* cpuview: 64K CPU-view dump (I/O regs at $DXXX); rawram: 64K raw RAM
   bank or NULL. */
void c64_load_snapshot(struct c64 *m, const unsigned char *cpuview,
                       const unsigned char *rawram);
/* Run exactly one video frame (263 scanlines), rendering into m->fb. */
void c64_run_frame(struct c64 *m);

/* keyboard: set/clear a matrix bit (row 0..7, col 0..7) */
void c64_key(struct c64 *m, int row, int col, int down);

/* Install a ported-routine hook at a 6502 address (see hooks above). */
void c64_add_hook(struct c64 *m, unsigned addr,
                  void (*fn)(struct c64 *m), const char *name);

/* bus callbacks (exposed for the hook layer / tests) */
unsigned char c64_read(void *ctx, unsigned addr);
void c64_write(void *ctx, unsigned addr, unsigned char val);

#endif
