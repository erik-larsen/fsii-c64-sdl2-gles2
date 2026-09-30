/*
 * main.c - FS2 hybrid runtime entry point (C90).
 *
 * Boots the VICE-dumped machine snapshot and runs the original 6502
 * code inside the C64 shim, presenting via SDL2 + OpenGLES2.  Runs on
 * macOS and, via Emscripten, on the web.
 *
 * Usage: fs2 [snapshot-base]
 *   default snapshot-base: build/fs2-vice-mem.bin
 *
 * Keys: physical C64 layout mapping (see kbd.c); Esc = RUN/STOP,
 * F12 = dump framebuffer to fs2-shot.ppm.
 */

#include <stdio.h>
#include <SDL.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "c64.h"
#include "snapshot.h"
#include "diskio.h"
#include "gfx.h"
#include "kbd.h"

static struct c64 machine;
static int running = 1;

static void dump_shot(const struct c64 *m)
{
    FILE *f = fopen("fs2-shot.ppm", "wb");
    int i;
    if (!f)
        return;
    fprintf(f, "P6\n%d %d\n255\n", C64_DISPLAY_W, C64_DISPLAY_H);
    for (i = 0; i < C64_DISPLAY_W * C64_DISPLAY_H; i++)
        fwrite(m->fb + i * 4, 1, 3, f);
    fclose(f);
    fprintf(stderr, "wrote fs2-shot.ppm\n");
}

static void handle_events(void)
{
    SDL_Event e;
    int row, col;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            running = 0;
            break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                gfx_resize(e.window.data1, e.window.data2);
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            if (e.key.repeat)
                break;
#ifdef FS2_KEY_DEBUG
            fprintf(stderr, "key %s scancode=%d\n",
                    e.type == SDL_KEYDOWN ? "down" : "up",
                    (int)e.key.keysym.scancode);
#endif
            if (e.type == SDL_KEYDOWN &&
                e.key.keysym.scancode == SDL_SCANCODE_F12) {
                dump_shot(&machine);
                break;
            }
            if (kbd_map(e.key.keysym.scancode, &row, &col))
                c64_key(&machine, row, col, e.type == SDL_KEYDOWN);
            break;
        default:
            break;
        }
    }
}

static void main_iter(void)
{
    handle_events();
    c64_run_frame(&machine);
    if (machine.cpu.jam) {
        fprintf(stderr, "CPU JAM at $%04X\n", machine.cpu.pc);
        running = 0;
#ifdef __EMSCRIPTEN__
        emscripten_cancel_main_loop();
#endif
        return;
    }
    gfx_present(machine.fb);
}

int main(int argc, char **argv)
{
    const char *base = "build/fs2-vice-mem.bin";
    const char *disk =
        "original-disks/Flight_Simulator_II_(Disk)_A_-_Game.d64";
    int selftest = 0;
    int argi = 1;
#ifdef __EMSCRIPTEN__
    base = "fs2-snapshot.bin"; /* preloaded into the virtual FS */
    disk = "fs2-disk.d64";
#endif
    while (argi < argc) {
        if (SDL_strcmp(argv[argi], "--selftest") == 0) {
            selftest = 1;
            argi++;
        } else if (SDL_strcmp(argv[argi], "--disk") == 0 &&
                   argi + 1 < argc) {
            disk = argv[argi + 1];
            argi += 2;
        } else {
            base = argv[argi];
            argi++;
        }
    }

    c64_init(&machine);
    if (snapshot_boot(&machine, base) != 0) {
        fprintf(stderr,
            "Missing snapshot. Generate it locally with: make vice-dump\n"
            "(The game image is extracted from your own FS2 disks and is "
            "never distributed with this repository.)\n");
        return 1;
    }
    diskio_init(&machine);
    if (diskio_mount(disk, 0) != 0)
        fprintf(stderr, "running with an empty drive\n");
    if (gfx_init("Flight Simulator II", 960, 720) != 0)
        return 1;

    fprintf(stderr, "booted at PC=$%04X\n", machine.cpu.pc);

    if (selftest) {
        int i;
        for (i = 0; i < 180 && running; i++)
            main_iter();
        dump_shot(&machine);
        gfx_shutdown();
        return machine.cpu.jam ? 1 : 0;
    }

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(main_iter, 0, 1);
#else
    {
        /* NTSC: 263 lines * 65 cycles at 1.0227 MHz -> ~59.83 Hz */
        Uint32 frame_ms_num = 16713; /* microseconds per frame ~ */
        Uint32 acc = 0, last = SDL_GetTicks();
        while (running) {
            Uint32 now = SDL_GetTicks();
            acc += (now - last) * 1000;
            last = now;
            if (acc < frame_ms_num) {
                SDL_Delay(1);
                continue;
            }
            /* run at most 3 frames of catch-up to avoid spiral */
            if (acc > frame_ms_num * 3)
                acc = frame_ms_num;
            while (acc >= frame_ms_num && running) {
                acc -= frame_ms_num;
                main_iter();
            }
        }
    }
#endif

    gfx_shutdown();
    return 0;
}
