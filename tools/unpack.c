/*
 * unpack.c - run the FS2 tape crack in the 6502 core until the game
 * proper starts, then snapshot memory (C90).
 *
 * The cracked tape image is double-packed (stage 1 uses escape byte $D3,
 * stage 2 uses $E3), so instead of pattern-matching depackers we simply
 * execute everything and stop at the first store to VIC/SID/CIA I/O
 * space made while I/O is banked in ($01) - i.e. the first thing only
 * real game init code would do.
 *
 * Outputs:
 *   snapshot.bin  - 64K RAM image at the stop point
 *   exec.map      - 64K bytes, 1 where an opcode was fetched (code map)
 *
 * Usage: unpack game.prg snapshot.bin exec.map [maxsteps]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/cpu6502.h"

static unsigned char ram[65536];
static unsigned char execmap[65536];
static unsigned io_write_addr = 0;
static int io_hit = 0;
static unsigned long bank_writes = 0;

static int io_mapped(void)
{
    unsigned char p = ram[1] & 7;
    return p >= 5; /* CHAREN=1 and not all-RAM: %101, %110, %111 */
}

static unsigned char bus_read(void *ctx, unsigned addr)
{
    (void)ctx;
    return ram[addr & 0xFFFF];
}

static void bus_write(void *ctx, unsigned addr, unsigned char val)
{
    (void)ctx;
    addr &= 0xFFFF;
    if (addr == 1 && bank_writes < 32) {
        printf("  [$01] <- $%02X\n", val);
        bank_writes++;
    }
    if (addr >= 0xD000 && addr <= 0xDFFF && io_mapped() && !io_hit) {
        io_hit = 1;
        io_write_addr = addr;
    }
    if (addr == 0x0314 || addr == 0x0315 || addr == 0xFFFE ||
        addr == 0xFFFF)
        printf("  [vec $%04X] <- $%02X\n", addr, val);
    ram[addr] = val;
}

int main(int argc, char **argv)
{
    FILE *f;
    struct cpu6502 cpu;
    unsigned load;
    long n;
    unsigned long steps = 0, maxsteps = 100000000UL;

    if (argc < 4) {
        fprintf(stderr, "usage: %s game.prg snapshot.bin exec.map "
                        "[maxsteps]\n", argv[0]);
        return 1;
    }
    if (argc > 4)
        maxsteps = strtoul(argv[4], NULL, 10);

    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    load = fgetc(f);
    load |= fgetc(f) << 8;
    n = (long)fread(ram + load, 1, sizeof(ram) - load, f);
    fclose(f);
    printf("loaded $%04X-$%04lX (%ld bytes)\n", load, load + n - 1, n);
    ram[1] = 0x37;

    memset(&cpu, 0, sizeof cpu);
    cpu.bus.read = bus_read;
    cpu.bus.write = bus_write;
    cpu6502_reset(&cpu, 0x0811);

    {
        unsigned trace_pc[64];
        unsigned char trace_op[64];
        int tr = 0;
        int redirected = 0;

        while (steps < maxsteps && !io_hit) {
            execmap[cpu.pc] = 1;
            trace_pc[tr & 63] = cpu.pc;
            trace_op[tr & 63] = ram[cpu.pc];
            tr++;
            cpu6502_step(&cpu);
            /*
             * The crack's stage-1 tail ends with JMP $A8BC, which in this
             * dump holds data (the stage-2 restore template).  The
             * *original* boot path restored by stage 1 at $0801 is
             * "NOP NOP LDA #$36 STA $01 JSR $A960 JMP $A88E"; resume
             * there instead the first time we jam at $A8BC.
             */
            if (cpu.jam && cpu.pc == 0xA8BC && !redirected) {
                printf("stage 1 done; redirecting from crack JMP $A8BC "
                       "to original boot $0811\n");
                cpu.jam = 0;
                cpu.pc = 0x0811;
                redirected = 1;
                continue;
            }
            if (cpu.jam) {
                int k;
                fprintf(stderr, "JAM at $%04X (opcode $%02X) after %lu "
                        "steps\n", cpu.pc, ram[cpu.pc], steps);
                fprintf(stderr, "last instructions:\n");
                for (k = tr > 64 ? tr - 64 : 0; k < tr; k++)
                    fprintf(stderr, "  $%04X: $%02X\n",
                            trace_pc[k & 63], trace_op[k & 63]);
                break;
            }
            steps++;
        }
    }

    if (io_hit)
        printf("first I/O write to $%04X at PC=$%04X after %lu "
               "instructions\n", io_write_addr, cpu.pc, steps);
    else
        printf("stopped after %lu instructions at PC=$%04X\n", steps,
               cpu.pc);

    f = fopen(argv[2], "wb");
    if (!f) { perror(argv[2]); return 1; }
    fwrite(ram, 1, sizeof ram, f);
    fclose(f);
    f = fopen(argv[3], "wb");
    if (!f) { perror(argv[3]); return 1; }
    fwrite(execmap, 1, sizeof execmap, f);
    fclose(f);
    printf("wrote %s and %s\n", argv[2], argv[3]);
    return cpu.jam ? 1 : 0;
}
