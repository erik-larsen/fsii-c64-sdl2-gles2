/*
 * fbtest.c - headless verification of the C64 machine model (C90).
 *
 * Boots the VICE snapshot, runs N frames, and writes the framebuffer to
 * a PPM for comparison with the VICE reference screenshot.  Optionally
 * injects a keypress (row/col) partway through to exercise the path
 * past the display-select prompt.
 *
 * Usage: fbtest snapshot-base frames out.ppm [row col press_at_frame]...
 * (repeat the row/col/frame triplet for multiple keys; each key is held
 * for 5 frames)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/c64.h"
#include "../src/snapshot.h"
#include "../src/diskio.h"

static struct c64 machine;

static int write_ppm(const char *path, const unsigned char *fb)
{
    FILE *f = fopen(path, "wb");
    int i;
    if (!f)
        return -1;
    fprintf(f, "P6\n%d %d\n255\n", C64_DISPLAY_W, C64_DISPLAY_H);
    for (i = 0; i < C64_DISPLAY_W * C64_DISPLAY_H; i++)
        fwrite(fb + i * 4, 1, 3, f);
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    int frames, i, k;
    int nkeys = 0;
    int krow[16], kcol[16], kat[16];
    const char *diskpath = NULL;
    int a = 1;
    struct c64 *m = &machine;

    if (argc > 2 && strcmp(argv[1], "-d") == 0) {
        diskpath = argv[2];
        a = 3;
    }
    if (argc < a + 3) {
        fprintf(stderr,
            "usage: %s [-d disk.d64] snapshot-base frames out.ppm "
            "[row col at]...\n", argv[0]);
        return 1;
    }
    frames = atoi(argv[a + 1]);
    for (k = a + 3; k + 2 < argc && nkeys < 16; k += 3) {
        krow[nkeys] = atoi(argv[k]);
        kcol[nkeys] = atoi(argv[k + 1]);
        kat[nkeys] = atoi(argv[k + 2]);
        nkeys++;
    }

    c64_init(m);
    if (snapshot_boot(m, argv[a]) != 0)
        return 1;
    if (diskpath && diskio_init(m, diskpath) != 0)
        return 1;
    printf("booted: PC=$%04X A=%02X X=%02X Y=%02X SP=%02X $01=%02X\n",
           m->cpu.pc, m->cpu.a, m->cpu.x, m->cpu.y, m->cpu.sp,
           m->ram[1]);

    for (i = 0; i < frames; i++) {
        for (k = 0; k < nkeys; k++) {
            if (i == kat[k])
                c64_key(m, krow[k], kcol[k], 1);
            if (i == kat[k] + 5)
                c64_key(m, krow[k], kcol[k], 0);
        }
        c64_run_frame(m);
        if (m->cpu.jam) {
            fprintf(stderr, "JAM at $%04X (frame %d)\n", m->cpu.pc, i);
            write_ppm(argv[a + 2], m->fb);
            return 1;
        }
    }
    if (getenv("FS2_TRACE_LINES")) {
        m->trace_lines = 1;
        c64_run_frame(m);
    }
    printf("ran %d frames, PC=$%04X cycles=%lu\n", frames, m->cpu.pc,
           m->cpu.cycles);
    if (write_ppm(argv[a + 2], m->fb) != 0) {
        perror(argv[a + 2]);
        return 1;
    }
    printf("wrote %s\n", argv[a + 2]);
    return 0;
}
