/*
 * t64x.c - T64 tape image extractor (C90)
 *
 * Extracts file entries from a .t64 image to raw .prg-style binaries.
 * Usage: t64x image.t64 [outdir]
 *
 * Output files are named entryNN_LLLL-EEEE.bin where LLLL/EEEE are the
 * load/end addresses in hex.  A .prg (with 2-byte load address header)
 * is written alongside.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned rd16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static unsigned long rd32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

int main(int argc, char **argv)
{
    FILE *f;
    unsigned char *buf;
    long size;
    unsigned entries, used, i;
    const char *outdir = ".";

    if (argc < 2) {
        fprintf(stderr, "usage: %s image.t64 [outdir]\n", argv[0]);
        return 1;
    }
    if (argc > 2)
        outdir = argv[2];

    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (unsigned char *)malloc((size_t)size);
    if (!buf || (long)fread(buf, 1, (size_t)size, f) != size) {
        fprintf(stderr, "read error\n");
        return 1;
    }
    fclose(f);

    if (size < 64 || memcmp(buf, "C64", 3) != 0) {
        fprintf(stderr, "not a T64 image\n");
        return 1;
    }

    entries = rd16(buf + 0x22);
    used = rd16(buf + 0x24);
    printf("T64: %u entries (%u used)\n", entries, used);

    for (i = 0; i < entries; i++) {
        const unsigned char *e = buf + 0x40 + 32 * i;
        unsigned type = e[0];
        unsigned load = rd16(e + 2);
        unsigned end = rd16(e + 4);
        unsigned long off = rd32(e + 8);
        unsigned long len;
        char name[17];
        char path[1024];
        FILE *o;
        int j;

        if (type == 0)
            continue;
        memcpy(name, e + 16, 16);
        name[16] = 0;
        for (j = 15; j >= 0 && (name[j] == ' ' || name[j] == 0); j--)
            name[j] = 0;

        len = (unsigned long)end - load;
        /* Some T64s lie about the end address; clamp to file size. */
        if (off + len > (unsigned long)size) {
            fprintf(stderr, "entry %u: clamping length %lu -> %lu\n",
                    i, len, (unsigned long)size - off);
            len = (unsigned long)size - off;
            end = (unsigned)(load + len);
        }

        printf("entry %u: \"%s\" type $%02X load $%04X end $%04X offset %lu len %lu\n",
               i, name, type, load, end, off, len);

        sprintf(path, "%s/entry%02u_%04X-%04X.bin", outdir, i, load, end);
        o = fopen(path, "wb");
        if (!o) { perror(path); return 1; }
        fwrite(buf + off, 1, (size_t)len, o);
        fclose(o);
        printf("  wrote %s\n", path);

        sprintf(path, "%s/entry%02u_%04X-%04X.prg", outdir, i, load, end);
        o = fopen(path, "wb");
        if (!o) { perror(path); return 1; }
        fputc(load & 0xFF, o);
        fputc((load >> 8) & 0xFF, o);
        fwrite(buf + off, 1, (size_t)len, o);
        fclose(o);
        printf("  wrote %s\n", path);
    }

    free(buf);
    return 0;
}
