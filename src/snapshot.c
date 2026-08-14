/*
 * snapshot.c - load the VICE-dumped machine state (C90).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snapshot.h"

static unsigned char *read_file(const char *path, long want)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long n;
    if (!f)
        return NULL;
    buf = (unsigned char *)malloc((size_t)want);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    n = (long)fread(buf, 1, (size_t)want, f);
    fclose(f);
    if (n != want) {
        free(buf);
        return NULL;
    }
    return buf;
}

int snapshot_boot(struct c64 *m, const char *base)
{
    char path[1024];
    unsigned char *cpuview, *rawram;
    FILE *f;

    cpuview = read_file(base, 65536);
    if (!cpuview) {
        fprintf(stderr, "snapshot: cannot read %s\n", base);
        return -1;
    }
    sprintf(path, "%s.ram", base);
    rawram = read_file(path, 65536);

    c64_load_snapshot(m, cpuview, rawram);
    free(cpuview);
    if (rawram)
        free(rawram);

    /* registers */
    sprintf(path, "%s.regs", base);
    f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "snapshot: cannot read %s\n", path);
        return -1;
    }
    {
        char name[32];
        unsigned val;
        cpu6502_reset(&m->cpu, 0);
        while (fscanf(f, "%31s %x", name, &val) == 2) {
            if (strcmp(name, "PC") == 0) m->cpu.pc = val & 0xFFFF;
            else if (strcmp(name, "A") == 0) m->cpu.a = (unsigned char)val;
            else if (strcmp(name, "X") == 0) m->cpu.x = (unsigned char)val;
            else if (strcmp(name, "Y") == 0) m->cpu.y = (unsigned char)val;
            else if (strcmp(name, "SP") == 0)
                m->cpu.sp = (unsigned char)val;
            else if (strcmp(name, "FL") == 0)
                cpu6502_flags_unpack(&m->cpu, (unsigned char)val);
            else if (strcmp(name, "00") == 0)
                m->ram[0] = (unsigned char)val;
            else if (strcmp(name, "01") == 0)
                m->ram[1] = (unsigned char)val;
        }
    }
    fclose(f);
    return 0;
}
