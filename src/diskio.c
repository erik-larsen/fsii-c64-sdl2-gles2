/*
 * diskio.c - C port of the FS2 loader's drive-channel layer (C90).
 *
 * See diskio.h for the hook contract and drive model.  The hooks mirror
 * the original routines' register/memory effects:
 *
 * $7657/$765B (send buffer to channel): A = secondary address (low
 *   nibble = channel), buffer at ($A5) with length $7324 (0 => 256).
 *   A high nibble $F: OPEN with the buffer as filename.  Otherwise: data
 *   to the channel; channel 15 data is a DOS command.  $765B starts at
 *   buffer offset 1 and wraps, i.e. the game expects the drive's buffer
 *   pointer to be at 1 after OPEN "#" so bytes land at their own offsets.
 *   Effects: $7323 = A, $B8 = 0, carry clear.
 *
 * $7618 (receive channel into memory): A = channel; destination pointer
 *   at $C2/$C3, incremented per byte.  If $7327 != 0, stores above $BFFF
 *   are suppressed (pointer still advances) - the loader's guard against
 *   overwriting itself.  Channel 2: 256 bytes from the drive buffer at
 *   its pointer.  Channel 15: the DOS error string.
 *   Effects: $B8 gets EOI (bit 6), carry clear.
 *
 * $75DD (open) / $75FC (close): $7323 = A, carry clear.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "diskio.h"

#define MAX_TRACKS 40
#define D64_SECTORS_35 683
#define D64_SECTORS_40 768

static const int sectors_per_track[MAX_TRACKS + 1] = {
    0,
    21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
    19, 19, 19, 19, 19, 19, 19,
    18, 18, 18, 18, 18, 18,
    17, 17, 17, 17, 17,
    17, 17, 17, 17, 17
};

/* ---- drive state ---- */

static int inserted;                 /* 0 = drive empty */
static char disk_path[1024];
static int writable;                 /* write-protect tab removed */
static int tracks;                   /* 35 or 40 */
static int has_errinfo;              /* image carried error bytes */
static unsigned char image[D64_SECTORS_40 * 256];
static unsigned char errinfo[D64_SECTORS_40]; /* .d64 codes, 1 = OK */

static unsigned char drive_buf[256]; /* the "#" direct-access buffer */
static unsigned buf_ptr;             /* drive-side buffer pointer */
static char errstr[64] = "73,CBM DOS V2.6 1541,00,00\015";
static int last_track, last_sector;

static void (*write_cb)(const char *path);
static int trace_dos = -1;

static int total_sectors(void)
{
    return tracks == 40 ? D64_SECTORS_40 : D64_SECTORS_35;
}

static int sector_index(int t, int s)
{
    int i = 0, k;
    for (k = 1; k < t; k++)
        i += sectors_per_track[k];
    return i + s;
}

static void set_error(int code, const char *msg, int t, int s)
{
    sprintf(errstr, "%02d,%s,%02d,%02d\015", code, msg, t, s);
}

static void set_ok(void)
{
    set_error(0, " OK", 0, 0);
}

/* .d64 error-info byte -> 1541 DOS error code (0 = no error) */
static int errinfo_to_dos(unsigned char e)
{
    switch (e) {
    case 0x00: case 0x01: return 0;
    case 0x02: return 20;
    case 0x03: return 21;
    case 0x04: return 22;
    case 0x05: return 23;
    case 0x06: return 24;
    case 0x07: return 25;
    case 0x08: return 26;
    case 0x09: return 27;
    case 0x0A: return 28;
    case 0x0B: return 29;
    case 0x0F: return 74;
    default:   return 0;
    }
}

static const char *dos_error_text(int code)
{
    switch (code) {
    case 25: case 28: return "WRITE ERROR";
    case 26: return "WRITE PROTECT ON";
    case 29: return "DISK ID MISMATCH";
    case 74: return "DRIVE NOT READY";
    default: return "READ ERROR";
    }
}

/* A disk is formatted if its BAM at 18/0 carries the 1541 format mark. */
static int formatted(void)
{
    const unsigned char *bam = image + 256L * sector_index(18, 0);
    return bam[0] == 18 && bam[2] == 0x41;
}

/* ---- image I/O ---- */

static int save_image(void)
{
    FILE *f = fopen(disk_path, "wb");
    if (!f) {
        fprintf(stderr, "diskio: cannot write %s\n", disk_path);
        return -1;
    }
    fwrite(image, 1, (size_t)total_sectors() * 256, f);
    if (has_errinfo)
        fwrite(errinfo, 1, (size_t)total_sectors(), f);
    fclose(f);
    if (write_cb)
        write_cb(disk_path);
    return 0;
}

int diskio_mount(const char *path, int wr)
{
    FILE *f = fopen(path, "rb");
    long size;
    int n;

    buf_ptr = 0;
    memset(errinfo, 1, sizeof errinfo);
    if (!f) {
        if (!wr) {
            fprintf(stderr, "diskio: cannot open %s\n", path);
            return -1;
        }
        /* a fresh, unformatted blank disk; created on first write */
        memset(image, 0, sizeof image);
        tracks = 35;
        has_errinfo = 0;
    } else {
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (size == 174848L || size == 175531L)
            tracks = 35;
        else if (size == 196608L || size == 197376L)
            tracks = 40;
        else {
            fclose(f);
            fprintf(stderr, "diskio: %s: not a .d64 (%ld bytes)\n",
                    path, size);
            return -1;
        }
        n = total_sectors();
        if (fread(image, 256, (size_t)n, f) != (size_t)n) {
            fclose(f);
            fprintf(stderr, "diskio: read error on %s\n", path);
            return -1;
        }
        has_errinfo = size == 175531L || size == 197376L;
        if (has_errinfo && fread(errinfo, 1, (size_t)n, f) != (size_t)n)
            has_errinfo = 0;
        fclose(f);
    }
    strncpy(disk_path, path, sizeof disk_path - 1);
    disk_path[sizeof disk_path - 1] = 0;
    writable = wr;
    inserted = 1;
    fprintf(stderr, "diskio: inserted %s (%d tracks%s%s%s)\n", path,
            tracks, has_errinfo ? ", error info" : "",
            writable ? ", writable" : ", write-protected",
            formatted() ? "" : ", unformatted");
    return 0;
}

void diskio_eject(void)
{
    inserted = 0;
    disk_path[0] = 0;
}

const char *diskio_disk_path(void)
{
    return inserted ? disk_path : NULL;
}

void diskio_set_write_callback(void (*fn)(const char *path))
{
    write_cb = fn;
}

/* ---- DOS commands ---- */

/* 1541 "N" (new): BAM + empty directory on track 18, all blocks free
   except 18/0 and 18/1; data blocks get the drive's fill pattern. */
static void format_disk(const char *name, const char *id)
{
    unsigned char *bam, *dir;
    int t, s, k;

    for (k = 0; k < total_sectors(); k++) {
        memset(image + 256L * k, 0x01, 256);
        image[256L * k] = 0x4B;
        errinfo[k] = 1;
    }
    bam = image + 256L * sector_index(18, 0);
    dir = image + 256L * sector_index(18, 1);
    memset(bam, 0, 256);
    memset(dir, 0, 256);
    bam[0] = 18;
    bam[1] = 1;
    bam[2] = 0x41;
    for (t = 1; t <= 35; t++) {
        unsigned char *e = bam + 4 * t;
        int n = sectors_per_track[t];
        unsigned long bits = (1UL << n) - 1;
        if (t == 18) {
            bits &= ~3UL;
            n -= 2;
        }
        e[0] = (unsigned char)n;
        e[1] = (unsigned char)(bits & 0xFF);
        e[2] = (unsigned char)((bits >> 8) & 0xFF);
        e[3] = (unsigned char)((bits >> 16) & 0xFF);
    }
    memset(bam + 0x90, 0xA0, 0x1B);
    for (s = 0; s < 16 && name[s]; s++)
        bam[0x90 + s] = (unsigned char)name[s];
    bam[0xA2] = (unsigned char)(id[0] ? id[0] : 0xA0);
    bam[0xA3] = (unsigned char)(id[0] && id[1] ? id[1] : 0xA0);
    bam[0xA5] = '2';
    bam[0xA6] = 'A';
    dir[1] = 0xFF; /* tracks 36-40 of a 40-track image stay outside
                      the standard BAM, as on a real 1541 */
}

static void block_command(int write, int t, int s)
{
    int idx, err;

    if (t < 1 || t > tracks || s < 0 || s >= sectors_per_track[t]) {
        set_error(66, "ILLEGAL TRACK AND SECTOR", t, s);
        return;
    }
    last_track = t;
    last_sector = s;
    idx = sector_index(t, s);

    if (!write) {
        err = formatted() ? errinfo_to_dos(errinfo[idx]) : 21;
        if (err) {
            set_error(err, dos_error_text(err), t, s);
            return;
        }
        memcpy(drive_buf, image + 256L * idx, 256);
        buf_ptr = 0;
        set_ok();
        return;
    }

    if (!writable) {
        set_error(26, "WRITE PROTECT ON", t, s);
        return;
    }
    if (!formatted()) {
        set_error(20, "READ ERROR", t, s);
        return;
    }
    memcpy(image + 256L * idx, drive_buf, 256);
    errinfo[idx] = 1;
    set_ok();
    save_image();
}

static void dos_command(const unsigned char *cmd, unsigned len)
{
    char c[64];
    unsigned n = len < sizeof c - 1 ? len : sizeof c - 1;
    memcpy(c, cmd, n);
    c[n] = 0;
    while (n > 0 && (c[n - 1] == '\r' || c[n - 1] == ' '))
        c[--n] = 0;

    if (trace_dos < 0)
        trace_dos = getenv("FS2_TRACE_DOS") != NULL;
    if (trace_dos)
        fprintf(stderr, "dos: \"%s\"\n", c);

    if (!inserted) {
        set_error(74, "DRIVE NOT READY", 0, 0);
        return;
    }

    if (c[0] == 'U' && (c[1] == '1' || c[1] == '2')) {
        /* "U1:ch dr tt ss" - ASCII decimal fields */
        int ch = 0, dr = 0, t = 0, s = 0;
        if (sscanf(c + 2, ":%d %d %d %d", &ch, &dr, &t, &s) != 4 &&
            sscanf(c + 2, ": %d %d %d %d", &ch, &dr, &t, &s) != 4) {
            set_error(30, "SYNTAX ERROR", 0, 0);
            return;
        }
        block_command(c[1] == '2', t, s);
    } else if (c[0] == 'I') {
        set_ok();
    } else if (c[0] == 'N') {
        /* "N0:NAME,ID" */
        char name[17] = "", id[3] = "";
        const char *p = strchr(c, ':');
        const char *comma;
        if (!writable) {
            set_error(26, "WRITE PROTECT ON", 0, 0);
            return;
        }
        p = p ? p + 1 : c + 1;
        comma = strchr(p, ',');
        if (comma) {
            size_t nl = (size_t)(comma - p);
            if (nl > 16) nl = 16;
            memcpy(name, p, nl);
            name[nl] = 0;
            strncpy(id, comma + 1, 2);
            id[2] = 0;
        } else {
            strncpy(name, p, 16);
            name[16] = 0;
        }
        format_disk(name, id);
        fprintf(stderr, "diskio: formatted %s as \"%s\",%s\n", disk_path,
                name, id);
        set_ok();
        save_image();
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
    unsigned sa = m->cpu.a;
    unsigned ch = sa & 0x0F;
    unsigned len = m->ram[0x7324] ? m->ram[0x7324] : 256;
    unsigned ptr = rd16zp(m, 0xA5);
    unsigned start = (m->cpu.pc == 0x765B) ? 1 : 0;
    unsigned char bytes[256];
    unsigned i, n = 0;

    /* gather in the original's send order: Y = start .. wraps .. len */
    i = start;
    do {
        bytes[n++] = m->ram[(ptr + i) & 0xFFFF];
        i = (i + 1) & 0xFF;
    } while (i != (len & 0xFF) && n < 256);

    m->ram[0x7323] = m->cpu.a;
    m->ram[0xB8] = 0;

    if ((sa & 0xF0) == 0xF0) {
        /* OPEN with filename */
        if (ch == 15) {
            dos_command(bytes, n);
        } else if (n > 0 && bytes[0] == '#') {
            buf_ptr = 1; /* 1541: direct-access buffer pointer after open */
            if (inserted)
                set_ok();
            else
                set_error(74, "DRIVE NOT READY", 0, 0);
        }
    } else if (ch == 15) {
        dos_command(bytes, n);
    } else {
        for (i = 0; i < n; i++) {
            drive_buf[buf_ptr] = bytes[i];
            buf_ptr = (buf_ptr + 1) & 0xFF;
        }
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
    unsigned char last = 0;

    m->ram[0x7323] = m->cpu.a;

    if (ch == 15) {
        n = (unsigned)strlen(errstr);
        for (i = 0; i < n; i++) {
            last = (unsigned char)errstr[i];
            if (!(guard && dst >= 0xC000))
                m->ram[dst & 0xFFFF] = last;
            dst = (dst + 1) & 0xFFFF;
        }
        set_ok(); /* reading the error channel clears it */
    } else {
        if (trace_dos > 0)
            fprintf(stderr, "dos:   t%d s%d -> $%04X%s\n", last_track,
                    last_sector, dst, guard ? " (guarded)" : "");
        for (i = 0; i < 256; i++) {
            last = drive_buf[buf_ptr];
            buf_ptr = (buf_ptr + 1) & 0xFF;
            if (!(guard && dst >= 0xC000))
                m->ram[dst & 0xFFFF] = last;
            dst = (dst + 1) & 0xFFFF;
        }
    }
    m->ram[0xC2] = (unsigned char)(dst & 0xFF);
    m->ram[0xC3] = (unsigned char)(dst >> 8);
    m->ram[0xB8] = 0x40; /* EOI */
    m->cpu.a = last;
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

void diskio_init(struct c64 *m)
{
    c64_add_hook(m, 0x7657, hk_send, "iec_send_buf");
    c64_add_hook(m, 0x765B, hk_send, "iec_send_buf_y1");
    c64_add_hook(m, 0x7618, hk_recv, "iec_recv_to_mem");
    c64_add_hook(m, 0x75DD, hk_open_close, "iec_open_ch");
    c64_add_hook(m, 0x75FC, hk_open_close, "iec_close_ch");
}
