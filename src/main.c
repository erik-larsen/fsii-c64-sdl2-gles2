/*
 * main.c - FS2 hybrid runtime entry point (C90).
 *
 * Boots the VICE-dumped machine snapshot and runs the original 6502
 * code inside the C64 shim, presenting via SDL2 + OpenGLES2.  Runs on
 * macOS and, via Emscripten, on the web.
 *
 * Usage: fs2 [--selftest] [--disk name] [--disks dir] [snapshot-base]
 *   default snapshot-base: build/fs2-vice-mem.bin
 *   default disk dir: original-disks (the game disk is inserted first)
 *
 * Keys: physical C64 layout mapping (see kbd.c); Esc = RUN/STOP.
 * Host keys: F9/F10 = insert next/previous disk from the shelf (scenery
 * disks, and a writable user disk for the mode library), F12 = dump
 * framebuffer to fs2-shot.ppm.
 *
 * The character ROM (needed for the editor's text screens) is read from
 * $FS2_CHARGEN or a local VICE install - never distributed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "c64.h"
#include "snapshot.h"
#include "diskio.h"
#include "shelf.h"
#include "gfx.h"
#include "kbd.h"

static struct c64 machine;
static int running = 1;

static const char *const chargen_paths[] = {
    "roms/chargen",
    "/usr/local/share/vice/C64/chargen-901225-01.bin",
    "/opt/homebrew/share/vice/C64/chargen-901225-01.bin",
    "/usr/share/vice/C64/chargen-901225-01.bin",
    NULL
};

static void load_chargen(void)
{
    const char *env = getenv("FS2_CHARGEN");
    int i;
    if (env && c64_load_chargen(&machine, env) == 0)
        return;
    for (i = 0; chargen_paths[i]; i++)
        if (c64_load_chargen(&machine, chargen_paths[i]) == 0)
            return;
    fprintf(stderr, "no C64 character ROM found (set FS2_CHARGEN); the "
                    "editor's text screens will not render\n");
}

static void update_title(void)
{
    char title[128];
    sprintf(title, "Flight Simulator II  -  drive 8: %.60s",
            shelf_label());
    gfx_set_title(title);
    fprintf(stderr, "drive 8: %s\n", shelf_label());
}

#ifdef __EMSCRIPTEN__
/* the user disk lives in IndexedDB so saves survive page reloads */
static void persist_saves(const char *path)
{
    (void)path;
    EM_ASM(FS.syncfs(false, function(err) {
        if (err) console.log('saving user disk failed: ' + err);
    }););
}
#endif

static const char *userdisk_path(void)
{
    static char path[1024];
#ifdef __EMSCRIPTEN__
    EM_ASM(
        FS.mkdir('/saves');
        FS.mount(IDBFS, {}, '/saves');
        FS.syncfs(true, function(err) {});
    );
    diskio_set_write_callback(persist_saves);
    strcpy(path, "/saves/userdisk.d64");
#else
    char *pref = SDL_GetPrefPath("fsii-c64-sdl2-gles2", "fs2");
    sprintf(path, "%.1000suserdisk.d64", pref ? pref : "./");
    SDL_free(pref);
#endif
    return path;
}

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
            if (e.key.keysym.scancode == SDL_SCANCODE_F9 ||
                e.key.keysym.scancode == SDL_SCANCODE_F10) {
                if (e.type == SDL_KEYDOWN) {
                    shelf_step(e.key.keysym.scancode ==
                               SDL_SCANCODE_F9 ? 1 : -1);
                    update_title();
                }
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
    const char *disk = "A_-_Game.d64";
    const char *diskdir = "original-disks";
    int selftest = 0;
    int argi = 1;
#ifdef __EMSCRIPTEN__
    base = "fs2-snapshot.bin"; /* preloaded into the virtual FS */
    diskdir = "/disks";
#endif
    while (argi < argc) {
        if (SDL_strcmp(argv[argi], "--selftest") == 0) {
            selftest = 1;
            argi++;
        } else if (SDL_strcmp(argv[argi], "--disk") == 0 &&
                   argi + 1 < argc) {
            disk = argv[argi + 1];
            argi += 2;
        } else if (SDL_strcmp(argv[argi], "--disks") == 0 &&
                   argi + 1 < argc) {
            diskdir = argv[argi + 1];
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
    load_chargen();
    diskio_init(&machine);
    if (gfx_init("Flight Simulator II", 960, 720) != 0)
        return 1;
    shelf_init(diskdir, userdisk_path());
    if (shelf_insert_path(disk) != 0)
        fprintf(stderr, "game disk %s not found; drive is empty\n", disk);
    update_title();

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
