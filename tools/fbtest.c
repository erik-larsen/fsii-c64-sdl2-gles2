/*
 * fbtest.c - headless verification of the C64 machine model (C90).
 *
 * Boots the VICE snapshot, runs N frames, and writes the framebuffer to
 * a PPM for comparison with the VICE reference screenshot.  Optionally
 * injects a keypress (row/col) partway through to exercise the path
 * past the display-select prompt.
 *
 * Usage: fbtest [-d game.d64] [-u userdisk.d64] snapshot-base frames
 *               out.ppm [row col press_at_frame]...
 * FS2_SWAP="frame:target,..." changes disks during the run; target is g
 * (the -d disk), u (the -u disk), e (eject) or a .d64 path (mounted
 * write-protected).
 * (repeat the row/col/frame triplet for multiple keys; each key is held
 * for FS2_KEY_HOLD frames, default 5, or per key as frame:hold.  Keep
 * command keys short - FS2 auto-repeats held keys - but hold CTRL
 * across the chord, since it must latch a scan pass before the key)
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

/* apply any FS2_SWAP entries scheduled for this frame */
static void do_swaps(const char *spec, int frame, const char *game,
                     const char *user)
{
    const char *p = spec;
    while (*p) {
        char target[1024];
        int f = 0, n = 0;
        if (sscanf(p, "%d:%1023[^,]%n", &f, target, &n) != 2)
            return;
        if (f == frame) {
            printf("frame %d: swap -> %s\n", frame, target);
            if (strcmp(target, "e") == 0)
                diskio_eject();
            else if (strcmp(target, "g") == 0 && game)
                diskio_mount(game, 0);
            else if (strcmp(target, "u") == 0 && user)
                diskio_mount(user, 1);
            else
                diskio_mount(target, 0);
        }
        p += n;
        if (*p == ',')
            p++;
    }
}

int main(int argc, char **argv)
{
    int frames, i, k;
    int nkeys = 0;
    int krow[16], kcol[16], kat[16], khold[16];
    const char *diskpath = NULL, *userpath = NULL;
    const char *swaps = getenv("FS2_SWAP");
    int a = 1;
    int hold = getenv("FS2_KEY_HOLD") ? atoi(getenv("FS2_KEY_HOLD")) : 5;
    struct c64 *m = &machine;

    while (a + 1 < argc && argv[a][0] == '-') {
        if (strcmp(argv[a], "-d") == 0)
            diskpath = argv[a + 1];
        else if (strcmp(argv[a], "-u") == 0)
            userpath = argv[a + 1];
        else
            break;
        a += 2;
    }
    if (argc < a + 3) {
        fprintf(stderr,
            "usage: %s [-d game.d64] [-u user.d64] snapshot-base frames "
            "out.ppm [row col at]...\n", argv[0]);
        return 1;
    }
    frames = atoi(argv[a + 1]);
    for (k = a + 3; k + 2 < argc && nkeys < 16; k += 3) {
        krow[nkeys] = atoi(argv[k]);
        kcol[nkeys] = atoi(argv[k + 1]);
        khold[nkeys] = hold;
        if (sscanf(argv[k + 2], "%d:%d", &kat[nkeys], &khold[nkeys]) < 1)
            kat[nkeys] = atoi(argv[k + 2]);
        nkeys++;
    }

    c64_init(m);
    if (snapshot_boot(m, argv[a]) != 0)
        return 1;
    diskio_init(m);
    if (diskpath && diskio_mount(diskpath, 0) != 0)
        return 1;
    printf("booted: PC=$%04X A=%02X X=%02X Y=%02X SP=%02X $01=%02X\n",
           m->cpu.pc, m->cpu.a, m->cpu.x, m->cpu.y, m->cpu.sp,
           m->ram[1]);

    for (i = 0; i < frames; i++) {
        for (k = 0; k < nkeys; k++) {
            if (i == kat[k])
                c64_key(m, krow[k], kcol[k], 1);
            if (i == kat[k] + khold[k])
                c64_key(m, krow[k], kcol[k], 0);
        }
        if (swaps)
            do_swaps(swaps, i, diskpath, userpath);
        c64_run_frame(m);
        if (m->cpu.jam) {
            fprintf(stderr, "JAM at $%04X (frame %d)\n", m->cpu.pc, i);
            write_ppm(argv[a + 2], m->fb);
            return 1;
        }
    }
    if (getenv("FS2_DUMP_RAM")) {
        FILE *rf = fopen(getenv("FS2_DUMP_RAM"), "wb");
        if (rf) {
            fwrite(m->ram, 1, sizeof m->ram, rf);
            fclose(rf);
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
