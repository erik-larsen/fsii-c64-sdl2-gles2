/*
 * cpu6502.h - MOS 6502 interpreter core (C90)
 *
 * The host provides memory access through cpu6502_bus; the core is
 * otherwise self-contained.  Documented opcodes only (FS2 uses no
 * undocumented ops); hitting an undocumented opcode sets cpu->jam.
 */

#ifndef CPU6502_H
#define CPU6502_H

struct cpu6502_bus {
    unsigned char (*read)(void *ctx, unsigned addr);
    void (*write)(void *ctx, unsigned addr, unsigned char val);
    void *ctx;
};

struct cpu6502 {
    unsigned pc;
    unsigned char a, x, y, sp;
    unsigned char c, z, i, d, b, v, n;  /* flags, 0/1 */
    unsigned long cycles;
    int jam;                            /* set on undocumented opcode */
    struct cpu6502_bus bus;
};

void cpu6502_reset(struct cpu6502 *cpu, unsigned pc);
/* Execute one instruction; returns cycles consumed (approximate). */
int cpu6502_step(struct cpu6502 *cpu);
/* Push return state and vector to IRQ/NMI handler address. */
void cpu6502_irq(struct cpu6502 *cpu);
void cpu6502_nmi(struct cpu6502 *cpu);

unsigned char cpu6502_flags_pack(const struct cpu6502 *cpu);
void cpu6502_flags_unpack(struct cpu6502 *cpu, unsigned char p);

#endif
