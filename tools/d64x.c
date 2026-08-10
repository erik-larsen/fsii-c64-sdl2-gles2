/*
 * d64x.c - D64 disk image inspector / raw extractor (C90)
 *
 * FS2's disks use a custom sector-level loader (the CBM directory holds a
 * single dummy entry), so this tool works at the track/sector level.
 *
 * Usage:
 *   d64x image.d64 dir                  list BAM name + directory entries
 *   d64x image.d64 map                  per-sector classification (zero/data)
 *   d64x image.d64 sec T S [count]      hexdump sectors starting at T/S
 *   d64x image.d64 dump out.bin         concatenate all 683 sectors
 *   d64x image.d64 chain T S out.bin    follow a t/s link chain, save payload
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define D64_35_SIZE 174848L
#define D64_35E_SIZE 175531L /* 35 tracks + error info bytes */
#define D64_40_SIZE 196608L
#define D64_40E_SIZE 197376L

static const int sectors_per_track[41] = {
    0,
    21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
    19, 19, 19, 19, 19, 19, 19,
    18, 18, 18, 18, 18, 18,
    17, 17, 17, 17, 17,
    17, 17, 17, 17, 17 /* tracks 36-40 on extended images */
};

static long track_offset(int track)
{
    long off = 0;
    int t;
    for (t = 1; t < track; t++)
        off += 256L * sectors_per_track[t];
    return off;
}

static long ts_offset(int track, int sector)
{
    return track_offset(track) + 256L * sector;
}

static int num_tracks = 35;

static void hexdump(const unsigned char *p, long base, int len)
{
    int i, j;
    for (i = 0; i < len; i += 16) {
        printf("%06lX: ", base + i);
        for (j = 0; j < 16; j++)
            printf("%02X%s", p[i + j], (j & 3) == 3 ? " " : "");
        printf(" |");
        for (j = 0; j < 16; j++) {
            int c = p[i + j];
            putchar(c >= 32 && c < 127 ? c : '.');
        }
        printf("|\n");
    }
}

int main(int argc, char **argv)
{
    FILE *f;
    unsigned char *img;
    long size;
    const char *cmd;

    if (argc < 3) {
        fprintf(stderr,
            "usage: %s image.d64 dir|map|sec|dump|chain [args]\n", argv[0]);
        return 1;
    }
    cmd = argv[2];

    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    img = (unsigned char *)malloc((size_t)size);
    if (!img || (long)fread(img, 1, (size_t)size, f) != size) {
        fprintf(stderr, "read error\n");
        return 1;
    }
    fclose(f);

    if (size == D64_40_SIZE || size == D64_40E_SIZE)
        num_tracks = 40;
    printf("image: %ld bytes, %d tracks%s\n", size, num_tracks,
           (size == D64_35E_SIZE || size == D64_40E_SIZE)
               ? " (with error bytes)" : "");

    if (strcmp(cmd, "dir") == 0) {
        const unsigned char *bam = img + ts_offset(18, 0);
        int t, s;
        char name[17];
        int i;
        memcpy(name, bam + 0x90, 16);
        name[16] = 0;
        for (i = 0; i < 16; i++)
            if ((unsigned char)name[i] == 0xA0) name[i] = ' ';
        printf("disk name: \"%s\" id: %c%c\n", name, bam[0xA2], bam[0xA3]);
        t = 18; s = 1;
        while (t) {
            const unsigned char *sec = img + ts_offset(t, s);
            for (i = 0; i < 8; i++) {
                const unsigned char *e = sec + 32 * i;
                if (e[2] == 0)
                    continue;
                memcpy(name, e + 5, 16);
                name[16] = 0;
                {
                    int k;
                    for (k = 0; k < 16; k++)
                        if ((unsigned char)name[k] == 0xA0) name[k] = ' ';
                }
                printf("  type $%02X t/s %2u/%-2u \"%s\" blocks %u\n",
                       e[2], e[3], e[4], name, e[30] | (e[31] << 8));
            }
            t = sec[0]; s = sec[1];
        }
    } else if (strcmp(cmd, "map") == 0) {
        int t, s;
        for (t = 1; t <= num_tracks; t++) {
            printf("T%02d: ", t);
            for (s = 0; s < sectors_per_track[t]; s++) {
                const unsigned char *p = img + ts_offset(t, s);
                int i, nz = 0;
                for (i = 0; i < 256; i++)
                    if (p[i]) nz++;
                putchar(nz == 0 ? '.' : (nz < 32 ? '-' : '#'));
            }
            putchar('\n');
        }
    } else if (strcmp(cmd, "sec") == 0) {
        int t, s, count = 1, i;
        if (argc < 5) { fprintf(stderr, "sec needs T S\n"); return 1; }
        t = atoi(argv[3]); s = atoi(argv[4]);
        if (argc > 5) count = atoi(argv[5]);
        for (i = 0; i < count; i++) {
            printf("--- track %d sector %d ---\n", t, s);
            hexdump(img + ts_offset(t, s), ts_offset(t, s), 256);
            s++;
            if (s >= sectors_per_track[t]) { s = 0; t++; }
        }
    } else if (strcmp(cmd, "dump") == 0) {
        FILE *o;
        long total = track_offset(num_tracks + 1);
        if (argc < 4) { fprintf(stderr, "dump needs outfile\n"); return 1; }
        o = fopen(argv[3], "wb");
        if (!o) { perror(argv[3]); return 1; }
        fwrite(img, 1, (size_t)total, o);
        fclose(o);
        printf("wrote %ld bytes to %s\n", total, argv[3]);
    } else if (strcmp(cmd, "chain") == 0) {
        FILE *o;
        int t, s, n = 0;
        if (argc < 6) { fprintf(stderr, "chain needs T S outfile\n"); return 1; }
        t = atoi(argv[3]); s = atoi(argv[4]);
        o = fopen(argv[5], "wb");
        if (!o) { perror(argv[5]); return 1; }
        while (t && n < 800) {
            const unsigned char *p = img + ts_offset(t, s);
            int payload = p[0] ? 254 : (p[1] ? p[1] - 1 : 254);
            printf("  %2d/%-2d -> %2d/%-2d\n", t, s, p[0], p[1]);
            fwrite(p + 2, 1, (size_t)payload, o);
            t = p[0]; s = p[1];
            n++;
        }
        fclose(o);
        printf("%d sectors followed\n", n);
    } else {
        fprintf(stderr, "unknown command %s\n", cmd);
        return 1;
    }

    free(img);
    return 0;
}
