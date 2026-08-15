/*
 * diskio.c - C port of the FS2 loader's drive-channel layer (C90).
 *
 * See diskio.h for the hook contract.  Behavior mirrors the original
 * routines' register/memory effects:
 *
 * $7657/$765B (send buffer to channel): A = channel, buffer at
 *   ($A5)/($A6) with length $7324 (0 => 256).  Channel 15: interpret as
 *   a DOS command.  Channel 2: fill the drive-side buffer with the sent
 *   bytes in arrival order (the original starts at Y=1 and wraps, which
 *   only affects the order bytes leave RAM, not the buffer layout).
 *   Effects: $7323 = A, $B8 = 0, carry clear.
 *
 * $7618 (receive channel into memory): A = channel; destination pointer
 *   at $C2/$C3, incremented per byte.  If $7327 != 0, stores above
 *   $BFFF are suppressed (pointer still advances) - the loader's guard
 *   against overwriting the loader itself.  Channel 2: 256 bytes of the
 *   current sector buffer.  Channel 15: the DOS error string.
 *   Effects: $B8 gets EOI (bit 6), carry clear.
 *
 * $75DD (open) / $75FC (close): $7323 = A, carry clear.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "diskio.h"

#define MAX_TRACKS 40

static const int sectors_per_track[MAX_TRACKS + 1] = {
    0,
    21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
    19, 19, 19, 19, 19, 19, 19,
    18, 18, 18, 18, 18, 18,
    17, 17, 17, 17, 17,
    17, 17, 17, 17, 17
};

static unsigned char disk[MAX_TRACKS * 21 * 256];
static int disk_tracks;
static char disk_name[512];

static unsigned char drive_buf[256]; /* channel 2 buffer ("#") */
static unsigned buf_fill;            /* bytes received into it */
static char errstr[64] = "00, OK,00,00\015";
static int last_track, last_sector;

static long ts_offset(int track, int sector)
{
    long off = 0;
    int t;
    for (t = 1; t < track; t++)
        off += 256L * sectors_per_track[t];
    return off + 256L * sector;
}

static void set_error(int code, const char *msg, int t, int s)
{
    sprintf(errstr, "%02d,%s,%02d,%02d\015", code, msg, t, s);
}

int diskio_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    long size;
    if (!f) {
        fprintf(stderr, "diskio: cannot open %s\n", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    disk_tracks = (size >= 196608L) ? 40 : 35;
    if (fread(disk, 1, (size_t)ts_offset(disk_tracks + 1, 0), f) == 0) {
        fclose(f);
        fprintf(stderr, "diskio: read error on %s\n", path);
        return -1;
    }
    fclose(f);
    strncpy(disk_name, path, sizeof disk_name - 1);
    fprintf(stderr, "diskio: %s (%d tracks)\n", path, disk_tracks);
    return 0;
}

int diskio_swap(const char *d64_path)
{
    return diskio_load(d64_path);
}

/* ---- DOS command interpreter (channel 15) ---- */

static void dos_command(const unsigned char *cmd, unsigned len)
{
    char c[64];
    unsigned n = len < sizeof c - 1 ? len : sizeof c - 1;
    memcpy(c, cmd, n);
    c[n] = 0;

    if (c[0] == 'U' && (c[1] == '1' || c[1] == '2')) {
        /* "U1:ch dr tt ss" - ASCII decimal fields */
        int ch = 0, dr = 0, t = 0, s = 0;
        if (sscanf(c + 2, ":%d %d %d %d", &ch, &dr, &t, &s) != 4 &&
            sscanf(c + 2, ": %d %d %d %d", &ch, &dr, &t, &s) != 4) {
            set_error(30, "SYNTAX ERROR", 0, 0);
            return;
        }
        if (t < 1 || t > disk_tracks || s < 0 ||
            s >= sectors_per_track[t]) {
            set_error(66, "ILLEGAL TRACK AND SECTOR", t, s);
            return;
        }
        last_track = t;
        last_sector = s;
        if (c[1] == '1') {
            memcpy(drive_buf, disk + ts_offset(t, s), 256);
            buf_fill = 0;
        } else {
            /* U2: write the received buffer to the in-memory image
               only; the original media is never modified. */
            memcpy(disk + ts_offset(t, s), drive_buf, 256);
            fprintf(stderr, "diskio: U2 write t%d s%d (in-memory)\n",
                    t, s);
        }
        set_error(0, " OK", 0, 0);
    } else if (c[0] == 'I') {
        buf_fill = 0;
        set_error(0, " OK", 0, 0);
    } else if (c[0] == 'N') {
        fprintf(stderr, "diskio: ignoring format command \"%s\"\n", c);
        set_error(0, " OK", 0, 0);
    } else {
        fprintf(stderr, "diskio: unknown DOS command \"%s\"\n", c);
        set_error(31, "SYNTAX ERROR", 0, 0);
    }
}

/* ---- hook implementations ---- */

static unsigned rd16zp(struct c64 *m, unsigned zp)
{
    return m->ram[zp] | ((unsigned)m->ram[zp + 1] << 8);
}

/* $7657 (Y=0) / $765B (Y=1): send buffer to channel */
static void hk_send(struct c64 *m)
{
    unsigned ch = m->cpu.a & 0x0F;
    unsigned len = m->ram[0x7324] ? m->ram[0x7324] : 256;
    unsigned ptr = rd16zp(m, 0xA5);
    unsigned i;

    m->ram[0x7323] = m->cpu.a;
    m->ram[0xB8] = 0;

    if (ch == 15) {
        dos_command(m->ram + ptr, len);
    } else {
        /* data to the buffer channel, in the original's send order */
        unsigned start = (m->cpu.pc == 0x765B) ? 1 : 0;
        buf_fill = 0;
        for (i = 0; i < len && i < 256; i++)
            drive_buf[buf_fill++] =
                m->ram[(ptr + ((start + i) & 0xFF)) & 0xFFFF];
    }
    m->cpu.c = 0;
    m->cpu.z = 0;
}

/* $7618: receive channel into ($C2), advancing the pointer */
static void hk_recv(struct c64 *m)
{
    unsigned ch = m->cpu.a & 0x0F;
    unsigned dst = rd16zp(m, 0xC2);
    unsigned guard = m->ram[0x7327];
    unsigned i, n;
    const unsigned char *src;

    m->ram[0x7323] = m->cpu.a;

    if (ch == 15) {
        src = (const unsigned char *)errstr;
        n = (unsigned)strlen(errstr);
    } else {
        src = drive_buf;
        n = 256;
    }
    for (i = 0; i < n; i++) {
        if (!(guard && dst >= 0xC000))
            m->ram[dst & 0xFFFF] = src[i];
        dst = (dst + 1) & 0xFFFF;
    }
    m->ram[0xC2] = (unsigned char)(dst & 0xFF);
    m->ram[0xC3] = (unsigned char)(dst >> 8);
    m->ram[0xB8] = 0x40; /* EOI */
    m->cpu.a = n ? src[n - 1] : 0;
    m->cpu.c = 0;
}

/* $75DD open / $75FC close: success no-ops */
static void hk_open_close(struct c64 *m)
{
    m->ram[0x7323] = m->cpu.a;
    if (m->cpu.pc == 0x75DD)
        m->ram[0xB8] = 0;
    m->cpu.c = 0;
}

int diskio_init(struct c64 *m, const char *d64_path)
{
    if (diskio_load(d64_path) != 0)
        return -1;
    c64_add_hook(m, 0x7657, hk_send, "iec_send_buf");
    c64_add_hook(m, 0x765B, hk_send, "iec_send_buf_y1");
    c64_add_hook(m, 0x7618, hk_recv, "iec_recv_to_mem");
    c64_add_hook(m, 0x75DD, hk_open_close, "iec_open_ch");
    c64_add_hook(m, 0x75FC, hk_open_close, "iec_close_ch");
    return 0;
}
