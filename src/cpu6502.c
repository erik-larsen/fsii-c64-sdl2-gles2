/*
 * cpu6502.c - MOS 6502 interpreter core (C90)
 */

#include "cpu6502.h"

#define RD(a) (cpu->bus.read(cpu->bus.ctx, (a) & 0xFFFF))
#define WR(a, v) (cpu->bus.write(cpu->bus.ctx, (a) & 0xFFFF, \
                                 (unsigned char)(v)))

static unsigned rd16(struct cpu6502 *cpu, unsigned a)
{
    return RD(a) | (RD(a + 1) << 8);
}

/* 6502 bug: indirect JMP wraps within the page. */
static unsigned rd16_pagewrap(struct cpu6502 *cpu, unsigned a)
{
    unsigned lo = RD(a);
    unsigned hi = RD((a & 0xFF00) | ((a + 1) & 0xFF));
    return lo | (hi << 8);
}

static void push(struct cpu6502 *cpu, unsigned char v)
{
    WR(0x0100 | cpu->sp, v);
    cpu->sp = (cpu->sp - 1) & 0xFF;
}

static unsigned char pull(struct cpu6502 *cpu)
{
    cpu->sp = (cpu->sp + 1) & 0xFF;
    return RD(0x0100 | cpu->sp);
}

unsigned char cpu6502_flags_pack(const struct cpu6502 *cpu)
{
    return (unsigned char)((cpu->n << 7) | (cpu->v << 6) | 0x20 |
                           (cpu->b << 4) | (cpu->d << 3) | (cpu->i << 2) |
                           (cpu->z << 1) | cpu->c);
}

void cpu6502_flags_unpack(struct cpu6502 *cpu, unsigned char p)
{
    cpu->n = (p >> 7) & 1;
    cpu->v = (p >> 6) & 1;
    cpu->b = (p >> 4) & 1;
    cpu->d = (p >> 3) & 1;
    cpu->i = (p >> 2) & 1;
    cpu->z = (p >> 1) & 1;
    cpu->c = p & 1;
}

void cpu6502_reset(struct cpu6502 *cpu, unsigned pc)
{
    cpu->pc = pc & 0xFFFF;
    cpu->a = cpu->x = cpu->y = 0;
    cpu->sp = 0xFD;
    cpu->c = cpu->z = cpu->d = cpu->b = cpu->v = cpu->n = 0;
    cpu->i = 1;
    cpu->cycles = 0;
    cpu->jam = 0;
}

void cpu6502_irq(struct cpu6502 *cpu)
{
    if (cpu->i)
        return;
    push(cpu, (unsigned char)(cpu->pc >> 8));
    push(cpu, (unsigned char)(cpu->pc & 0xFF));
    cpu->b = 0;
    push(cpu, cpu6502_flags_pack(cpu));
    cpu->i = 1;
    cpu->pc = rd16(cpu, 0xFFFE);
    cpu->cycles += 7;
}

void cpu6502_nmi(struct cpu6502 *cpu)
{
    push(cpu, (unsigned char)(cpu->pc >> 8));
    push(cpu, (unsigned char)(cpu->pc & 0xFF));
    cpu->b = 0;
    push(cpu, cpu6502_flags_pack(cpu));
    cpu->i = 1;
    cpu->pc = rd16(cpu, 0xFFFA);
    cpu->cycles += 7;
}

static void set_nz(struct cpu6502 *cpu, unsigned char v)
{
    cpu->n = (v >> 7) & 1;
    cpu->z = v == 0;
}

static void adc(struct cpu6502 *cpu, unsigned char m)
{
    if (cpu->d) {
        /* decimal mode (FS2 barely uses it, but be correct) */
        unsigned lo = (cpu->a & 0x0F) + (m & 0x0F) + cpu->c;
        unsigned hi = (cpu->a >> 4) + (m >> 4);
        unsigned bin = cpu->a + m + cpu->c;
        if (lo > 9) { lo += 6; hi++; }
        cpu->v = (~(cpu->a ^ m) & (cpu->a ^ (unsigned char)bin) & 0x80) != 0;
        if (hi > 9) hi += 6;
        cpu->c = hi > 15;
        cpu->a = (unsigned char)(((hi & 0x0F) << 4) | (lo & 0x0F));
        cpu->z = ((unsigned char)bin) == 0;
        cpu->n = (cpu->a & 0x80) != 0;
    } else {
        unsigned r = cpu->a + m + cpu->c;
        cpu->v = (~(cpu->a ^ m) & (cpu->a ^ (unsigned char)r) & 0x80) != 0;
        cpu->c = r > 0xFF;
        cpu->a = (unsigned char)r;
        set_nz(cpu, cpu->a);
    }
}

static void sbc(struct cpu6502 *cpu, unsigned char m)
{
    if (cpu->d) {
        unsigned bin = cpu->a - m - (1 - cpu->c);
        int lo = (cpu->a & 0x0F) - (m & 0x0F) - (1 - cpu->c);
        int hi = (cpu->a >> 4) - (m >> 4);
        cpu->v = ((cpu->a ^ m) & (cpu->a ^ (unsigned char)bin) & 0x80) != 0;
        if (lo < 0) { lo -= 6; hi--; }
        if (hi < 0) hi -= 6;
        cpu->c = (bin & 0x100) == 0;
        cpu->z = ((unsigned char)bin) == 0;
        cpu->n = ((unsigned char)bin & 0x80) != 0;
        cpu->a = (unsigned char)(((hi & 0x0F) << 4) | (lo & 0x0F));
    } else {
        adc(cpu, (unsigned char)(m ^ 0xFF));
    }
}

static void cmp_op(struct cpu6502 *cpu, unsigned char r, unsigned char m)
{
    unsigned d = r - m;
    cpu->c = r >= m;
    set_nz(cpu, (unsigned char)d);
}

static unsigned char asl(struct cpu6502 *cpu, unsigned char v)
{
    cpu->c = (v >> 7) & 1;
    v = (unsigned char)(v << 1);
    set_nz(cpu, v);
    return v;
}

static unsigned char lsr(struct cpu6502 *cpu, unsigned char v)
{
    cpu->c = v & 1;
    v >>= 1;
    set_nz(cpu, v);
    return v;
}

static unsigned char rol(struct cpu6502 *cpu, unsigned char v)
{
    unsigned char c0 = cpu->c;
    cpu->c = (v >> 7) & 1;
    v = (unsigned char)((v << 1) | c0);
    set_nz(cpu, v);
    return v;
}

static unsigned char ror(struct cpu6502 *cpu, unsigned char v)
{
    unsigned char c0 = cpu->c;
    cpu->c = v & 1;
    v = (unsigned char)((v >> 1) | (c0 << 7));
    set_nz(cpu, v);
    return v;
}

static void branch(struct cpu6502 *cpu, int cond)
{
    signed char off = (signed char)RD(cpu->pc);
    cpu->pc = (cpu->pc + 1) & 0xFFFF;
    if (cond) {
        cpu->pc = (cpu->pc + off) & 0xFFFF;
        cpu->cycles += 1;
    }
}

/* addressing helpers: return effective address, advancing pc */
static unsigned ea_zp(struct cpu6502 *cpu)
{
    unsigned a = RD(cpu->pc);
    cpu->pc = (cpu->pc + 1) & 0xFFFF;
    return a;
}
static unsigned ea_zpx(struct cpu6502 *cpu)
{
    return (ea_zp(cpu) + cpu->x) & 0xFF;
}
static unsigned ea_zpy(struct cpu6502 *cpu)
{
    return (ea_zp(cpu) + cpu->y) & 0xFF;
}
static unsigned ea_abs(struct cpu6502 *cpu)
{
    unsigned a = rd16(cpu, cpu->pc);
    cpu->pc = (cpu->pc + 2) & 0xFFFF;
    return a;
}
static unsigned ea_abx(struct cpu6502 *cpu)
{
    return (ea_abs(cpu) + cpu->x) & 0xFFFF;
}
static unsigned ea_aby(struct cpu6502 *cpu)
{
    return (ea_abs(cpu) + cpu->y) & 0xFFFF;
}
static unsigned ea_izx(struct cpu6502 *cpu)
{
    unsigned zp = (ea_zp(cpu) + cpu->x) & 0xFF;
    return rd16_pagewrap(cpu, zp);
}
static unsigned ea_izy(struct cpu6502 *cpu)
{
    unsigned zp = ea_zp(cpu);
    return (rd16_pagewrap(cpu, zp) + cpu->y) & 0xFFFF;
}
static unsigned char imm(struct cpu6502 *cpu)
{
    unsigned char v = RD(cpu->pc);
    cpu->pc = (cpu->pc + 1) & 0xFFFF;
    return v;
}

int cpu6502_step(struct cpu6502 *cpu)
{
    unsigned char op;
    unsigned long c0 = cpu->cycles;
    unsigned a;

    op = RD(cpu->pc);
    cpu->pc = (cpu->pc + 1) & 0xFFFF;
    cpu->cycles += 2; /* base; not cycle-exact, sufficient for FS2 pacing */

    switch (op) {
    /* --- loads/stores --- */
    case 0xA9: cpu->a = imm(cpu); set_nz(cpu, cpu->a); break;
    case 0xA5: cpu->a = RD(ea_zp(cpu)); set_nz(cpu, cpu->a); cpu->cycles++; break;
    case 0xB5: cpu->a = RD(ea_zpx(cpu)); set_nz(cpu, cpu->a); cpu->cycles += 2; break;
    case 0xAD: cpu->a = RD(ea_abs(cpu)); set_nz(cpu, cpu->a); cpu->cycles += 2; break;
    case 0xBD: cpu->a = RD(ea_abx(cpu)); set_nz(cpu, cpu->a); cpu->cycles += 2; break;
    case 0xB9: cpu->a = RD(ea_aby(cpu)); set_nz(cpu, cpu->a); cpu->cycles += 2; break;
    case 0xA1: cpu->a = RD(ea_izx(cpu)); set_nz(cpu, cpu->a); cpu->cycles += 4; break;
    case 0xB1: cpu->a = RD(ea_izy(cpu)); set_nz(cpu, cpu->a); cpu->cycles += 3; break;

    case 0xA2: cpu->x = imm(cpu); set_nz(cpu, cpu->x); break;
    case 0xA6: cpu->x = RD(ea_zp(cpu)); set_nz(cpu, cpu->x); cpu->cycles++; break;
    case 0xB6: cpu->x = RD(ea_zpy(cpu)); set_nz(cpu, cpu->x); cpu->cycles += 2; break;
    case 0xAE: cpu->x = RD(ea_abs(cpu)); set_nz(cpu, cpu->x); cpu->cycles += 2; break;
    case 0xBE: cpu->x = RD(ea_aby(cpu)); set_nz(cpu, cpu->x); cpu->cycles += 2; break;

    case 0xA0: cpu->y = imm(cpu); set_nz(cpu, cpu->y); break;
    case 0xA4: cpu->y = RD(ea_zp(cpu)); set_nz(cpu, cpu->y); cpu->cycles++; break;
    case 0xB4: cpu->y = RD(ea_zpx(cpu)); set_nz(cpu, cpu->y); cpu->cycles += 2; break;
    case 0xAC: cpu->y = RD(ea_abs(cpu)); set_nz(cpu, cpu->y); cpu->cycles += 2; break;
    case 0xBC: cpu->y = RD(ea_abx(cpu)); set_nz(cpu, cpu->y); cpu->cycles += 2; break;

    case 0x85: WR(ea_zp(cpu), cpu->a); cpu->cycles++; break;
    case 0x95: WR(ea_zpx(cpu), cpu->a); cpu->cycles += 2; break;
    case 0x8D: WR(ea_abs(cpu), cpu->a); cpu->cycles += 2; break;
    case 0x9D: WR(ea_abx(cpu), cpu->a); cpu->cycles += 3; break;
    case 0x99: WR(ea_aby(cpu), cpu->a); cpu->cycles += 3; break;
    case 0x81: WR(ea_izx(cpu), cpu->a); cpu->cycles += 4; break;
    case 0x91: WR(ea_izy(cpu), cpu->a); cpu->cycles += 4; break;

    case 0x86: WR(ea_zp(cpu), cpu->x); cpu->cycles++; break;
    case 0x96: WR(ea_zpy(cpu), cpu->x); cpu->cycles += 2; break;
    case 0x8E: WR(ea_abs(cpu), cpu->x); cpu->cycles += 2; break;

    case 0x84: WR(ea_zp(cpu), cpu->y); cpu->cycles++; break;
    case 0x94: WR(ea_zpx(cpu), cpu->y); cpu->cycles += 2; break;
    case 0x8C: WR(ea_abs(cpu), cpu->y); cpu->cycles += 2; break;

    /* --- transfers --- */
    case 0xAA: cpu->x = cpu->a; set_nz(cpu, cpu->x); break;
    case 0x8A: cpu->a = cpu->x; set_nz(cpu, cpu->a); break;
    case 0xA8: cpu->y = cpu->a; set_nz(cpu, cpu->y); break;
    case 0x98: cpu->a = cpu->y; set_nz(cpu, cpu->a); break;
    case 0xBA: cpu->x = cpu->sp; set_nz(cpu, cpu->x); break;
    case 0x9A: cpu->sp = cpu->x; break;

    /* --- stack --- */
    case 0x48: push(cpu, cpu->a); cpu->cycles++; break;
    case 0x68: cpu->a = pull(cpu); set_nz(cpu, cpu->a); cpu->cycles += 2; break;
    case 0x08: cpu->b = 1; push(cpu, cpu6502_flags_pack(cpu)); cpu->cycles++; break;
    case 0x28: cpu6502_flags_unpack(cpu, pull(cpu)); cpu->cycles += 2; break;

    /* --- logic/arith --- */
    case 0x29: cpu->a &= imm(cpu); set_nz(cpu, cpu->a); break;
    case 0x25: cpu->a &= RD(ea_zp(cpu)); set_nz(cpu, cpu->a); cpu->cycles++; break;
    case 0x35: cpu->a &= RD(ea_zpx(cpu)); set_nz(cpu, cpu->a); cpu->cycles += 2; break;
    case 0x2D: cpu->a &= RD(ea_abs(cpu)); set_nz(cpu, cpu->a); cpu->cycles += 2; break;
    case 0x3D: cpu->a &= RD(ea_abx(cpu)); set_nz(cpu, cpu->a); cpu->cycles += 2; break;
    case 0x39: cpu->a &= RD(ea_aby(cpu)); set_nz(cpu, cpu->a); cpu->cycles += 2; break;
    case 0x21: cpu->a &= RD(ea_izx(cpu)); set_nz(cpu, cpu->a); cpu->cycles += 4; break;
    case 0x31: cpu->a &= RD(ea_izy(cpu)); set_nz(cpu, cpu->a); cpu->cycles += 3; break;

    case 0x09: cpu->a |= imm(cpu); set_nz(cpu, cpu->a); break;
    case 0x05: cpu->a |= RD(ea_zp(cpu)); set_nz(cpu, cpu->a); cpu->cycles++; break;
    case 0x15: cpu->a |= RD(ea_zpx(cpu)); set_nz(cpu, cpu->a); cpu->cycles += 2; break;
    case 0x0D: cpu->a |= RD(ea_abs(cpu)); set_nz(cpu, cpu->a); cpu->cycles += 2; break;
    case 0x1D: cpu->a |= RD(ea_abx(cpu)); set_nz(cpu, cpu->a); cpu->cycles += 2; break;
    case 0x19: cpu->a |= RD(ea_aby(cpu)); set_nz(cpu, cpu->a); cpu->cycles += 2; break;
    case 0x01: cpu->a |= RD(ea_izx(cpu)); set_nz(cpu, cpu->a); cpu->cycles += 4; break;
    case 0x11: cpu->a |= RD(ea_izy(cpu)); set_nz(cpu, cpu->a); cpu->cycles += 3; break;

    case 0x49: cpu->a ^= imm(cpu); set_nz(cpu, cpu->a); break;
    case 0x45: cpu->a ^= RD(ea_zp(cpu)); set_nz(cpu, cpu->a); cpu->cycles++; break;
    case 0x55: cpu->a ^= RD(ea_zpx(cpu)); set_nz(cpu, cpu->a); cpu->cycles += 2; break;
    case 0x4D: cpu->a ^= RD(ea_abs(cpu)); set_nz(cpu, cpu->a); cpu->cycles += 2; break;
    case 0x5D: cpu->a ^= RD(ea_abx(cpu)); set_nz(cpu, cpu->a); cpu->cycles += 2; break;
    case 0x59: cpu->a ^= RD(ea_aby(cpu)); set_nz(cpu, cpu->a); cpu->cycles += 2; break;
    case 0x41: cpu->a ^= RD(ea_izx(cpu)); set_nz(cpu, cpu->a); cpu->cycles += 4; break;
    case 0x51: cpu->a ^= RD(ea_izy(cpu)); set_nz(cpu, cpu->a); cpu->cycles += 3; break;

    case 0x69: adc(cpu, imm(cpu)); break;
    case 0x65: adc(cpu, RD(ea_zp(cpu))); cpu->cycles++; break;
    case 0x75: adc(cpu, RD(ea_zpx(cpu))); cpu->cycles += 2; break;
    case 0x6D: adc(cpu, RD(ea_abs(cpu))); cpu->cycles += 2; break;
    case 0x7D: adc(cpu, RD(ea_abx(cpu))); cpu->cycles += 2; break;
    case 0x79: adc(cpu, RD(ea_aby(cpu))); cpu->cycles += 2; break;
    case 0x61: adc(cpu, RD(ea_izx(cpu))); cpu->cycles += 4; break;
    case 0x71: adc(cpu, RD(ea_izy(cpu))); cpu->cycles += 3; break;

    case 0xE9: sbc(cpu, imm(cpu)); break;
    case 0xE5: sbc(cpu, RD(ea_zp(cpu))); cpu->cycles++; break;
    case 0xF5: sbc(cpu, RD(ea_zpx(cpu))); cpu->cycles += 2; break;
    case 0xED: sbc(cpu, RD(ea_abs(cpu))); cpu->cycles += 2; break;
    case 0xFD: sbc(cpu, RD(ea_abx(cpu))); cpu->cycles += 2; break;
    case 0xF9: sbc(cpu, RD(ea_aby(cpu))); cpu->cycles += 2; break;
    case 0xE1: sbc(cpu, RD(ea_izx(cpu))); cpu->cycles += 4; break;
    case 0xF1: sbc(cpu, RD(ea_izy(cpu))); cpu->cycles += 3; break;

    case 0xC9: cmp_op(cpu, cpu->a, imm(cpu)); break;
    case 0xC5: cmp_op(cpu, cpu->a, RD(ea_zp(cpu))); cpu->cycles++; break;
    case 0xD5: cmp_op(cpu, cpu->a, RD(ea_zpx(cpu))); cpu->cycles += 2; break;
    case 0xCD: cmp_op(cpu, cpu->a, RD(ea_abs(cpu))); cpu->cycles += 2; break;
    case 0xDD: cmp_op(cpu, cpu->a, RD(ea_abx(cpu))); cpu->cycles += 2; break;
    case 0xD9: cmp_op(cpu, cpu->a, RD(ea_aby(cpu))); cpu->cycles += 2; break;
    case 0xC1: cmp_op(cpu, cpu->a, RD(ea_izx(cpu))); cpu->cycles += 4; break;
    case 0xD1: cmp_op(cpu, cpu->a, RD(ea_izy(cpu))); cpu->cycles += 3; break;

    case 0xE0: cmp_op(cpu, cpu->x, imm(cpu)); break;
    case 0xE4: cmp_op(cpu, cpu->x, RD(ea_zp(cpu))); cpu->cycles++; break;
    case 0xEC: cmp_op(cpu, cpu->x, RD(ea_abs(cpu))); cpu->cycles += 2; break;

    case 0xC0: cmp_op(cpu, cpu->y, imm(cpu)); break;
    case 0xC4: cmp_op(cpu, cpu->y, RD(ea_zp(cpu))); cpu->cycles++; break;
    case 0xCC: cmp_op(cpu, cpu->y, RD(ea_abs(cpu))); cpu->cycles += 2; break;

    case 0x24: { unsigned char m = RD(ea_zp(cpu));
        cpu->z = (cpu->a & m) == 0; cpu->n = (m >> 7) & 1;
        cpu->v = (m >> 6) & 1; cpu->cycles++; } break;
    case 0x2C: { unsigned char m = RD(ea_abs(cpu));
        cpu->z = (cpu->a & m) == 0; cpu->n = (m >> 7) & 1;
        cpu->v = (m >> 6) & 1; cpu->cycles += 2; } break;

    /* --- shifts (memory) --- */
    case 0x0A: cpu->a = asl(cpu, cpu->a); break;
    case 0x06: a = ea_zp(cpu); WR(a, asl(cpu, RD(a))); cpu->cycles += 3; break;
    case 0x16: a = ea_zpx(cpu); WR(a, asl(cpu, RD(a))); cpu->cycles += 4; break;
    case 0x0E: a = ea_abs(cpu); WR(a, asl(cpu, RD(a))); cpu->cycles += 4; break;
    case 0x1E: a = ea_abx(cpu); WR(a, asl(cpu, RD(a))); cpu->cycles += 5; break;

    case 0x4A: cpu->a = lsr(cpu, cpu->a); break;
    case 0x46: a = ea_zp(cpu); WR(a, lsr(cpu, RD(a))); cpu->cycles += 3; break;
    case 0x56: a = ea_zpx(cpu); WR(a, lsr(cpu, RD(a))); cpu->cycles += 4; break;
    case 0x4E: a = ea_abs(cpu); WR(a, lsr(cpu, RD(a))); cpu->cycles += 4; break;
    case 0x5E: a = ea_abx(cpu); WR(a, lsr(cpu, RD(a))); cpu->cycles += 5; break;

    case 0x2A: cpu->a = rol(cpu, cpu->a); break;
    case 0x26: a = ea_zp(cpu); WR(a, rol(cpu, RD(a))); cpu->cycles += 3; break;
    case 0x36: a = ea_zpx(cpu); WR(a, rol(cpu, RD(a))); cpu->cycles += 4; break;
    case 0x2E: a = ea_abs(cpu); WR(a, rol(cpu, RD(a))); cpu->cycles += 4; break;
    case 0x3E: a = ea_abx(cpu); WR(a, rol(cpu, RD(a))); cpu->cycles += 5; break;

    case 0x6A: cpu->a = ror(cpu, cpu->a); break;
    case 0x66: a = ea_zp(cpu); WR(a, ror(cpu, RD(a))); cpu->cycles += 3; break;
    case 0x76: a = ea_zpx(cpu); WR(a, ror(cpu, RD(a))); cpu->cycles += 4; break;
    case 0x6E: a = ea_abs(cpu); WR(a, ror(cpu, RD(a))); cpu->cycles += 4; break;
    case 0x7E: a = ea_abx(cpu); WR(a, ror(cpu, RD(a))); cpu->cycles += 5; break;

    /* --- inc/dec --- */
    case 0xE6: a = ea_zp(cpu); { unsigned char v = RD(a) + 1; WR(a, v);
        set_nz(cpu, v); } cpu->cycles += 3; break;
    case 0xF6: a = ea_zpx(cpu); { unsigned char v = RD(a) + 1; WR(a, v);
        set_nz(cpu, v); } cpu->cycles += 4; break;
    case 0xEE: a = ea_abs(cpu); { unsigned char v = RD(a) + 1; WR(a, v);
        set_nz(cpu, v); } cpu->cycles += 4; break;
    case 0xFE: a = ea_abx(cpu); { unsigned char v = RD(a) + 1; WR(a, v);
        set_nz(cpu, v); } cpu->cycles += 5; break;

    case 0xC6: a = ea_zp(cpu); { unsigned char v = RD(a) - 1; WR(a, v);
        set_nz(cpu, v); } cpu->cycles += 3; break;
    case 0xD6: a = ea_zpx(cpu); { unsigned char v = RD(a) - 1; WR(a, v);
        set_nz(cpu, v); } cpu->cycles += 4; break;
    case 0xCE: a = ea_abs(cpu); { unsigned char v = RD(a) - 1; WR(a, v);
        set_nz(cpu, v); } cpu->cycles += 4; break;
    case 0xDE: a = ea_abx(cpu); { unsigned char v = RD(a) - 1; WR(a, v);
        set_nz(cpu, v); } cpu->cycles += 5; break;

    case 0xE8: cpu->x++; set_nz(cpu, cpu->x); break;
    case 0xCA: cpu->x--; set_nz(cpu, cpu->x); break;
    case 0xC8: cpu->y++; set_nz(cpu, cpu->y); break;
    case 0x88: cpu->y--; set_nz(cpu, cpu->y); break;

    /* --- branches --- */
    case 0x10: branch(cpu, !cpu->n); break;
    case 0x30: branch(cpu, cpu->n); break;
    case 0x50: branch(cpu, !cpu->v); break;
    case 0x70: branch(cpu, cpu->v); break;
    case 0x90: branch(cpu, !cpu->c); break;
    case 0xB0: branch(cpu, cpu->c); break;
    case 0xD0: branch(cpu, !cpu->z); break;
    case 0xF0: branch(cpu, cpu->z); break;

    /* --- jumps --- */
    case 0x4C: cpu->pc = ea_abs(cpu); cpu->cycles++; break;
    case 0x6C: cpu->pc = rd16_pagewrap(cpu, ea_abs(cpu)); cpu->cycles += 3; break;
    case 0x20: {
        unsigned tgt = ea_abs(cpu);
        unsigned ret = (cpu->pc - 1) & 0xFFFF;
        push(cpu, (unsigned char)(ret >> 8));
        push(cpu, (unsigned char)(ret & 0xFF));
        cpu->pc = tgt;
        cpu->cycles += 4;
    } break;
    case 0x60: {
        unsigned lo = pull(cpu), hi = pull(cpu);
        cpu->pc = ((hi << 8) | lo) + 1;
        cpu->pc &= 0xFFFF;
        cpu->cycles += 4;
    } break;
    case 0x40: {
        cpu6502_flags_unpack(cpu, pull(cpu));
        {
            unsigned lo = pull(cpu), hi = pull(cpu);
            cpu->pc = (hi << 8) | lo;
        }
        cpu->cycles += 4;
    } break;
    case 0x00: { /* BRK */
        unsigned ret = (cpu->pc + 1) & 0xFFFF;
        push(cpu, (unsigned char)(ret >> 8));
        push(cpu, (unsigned char)(ret & 0xFF));
        cpu->b = 1;
        push(cpu, cpu6502_flags_pack(cpu));
        cpu->i = 1;
        cpu->pc = rd16(cpu, 0xFFFE);
        cpu->cycles += 5;
    } break;

    /* --- flags --- */
    case 0x18: cpu->c = 0; break;
    case 0x38: cpu->c = 1; break;
    case 0x58: cpu->i = 0; break;
    case 0x78: cpu->i = 1; break;
    case 0xB8: cpu->v = 0; break;
    case 0xD8: cpu->d = 0; break;
    case 0xF8: cpu->d = 1; break;

    case 0xEA: break; /* NOP */

    default:
        cpu->jam = 1;
        cpu->pc = (cpu->pc - 1) & 0xFFFF;
        break;
    }

    return (int)(cpu->cycles - c0);
}
