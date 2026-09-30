/*
 * c64.c - Minimal C64 machine model for the FS2 port (C90).
 *
 * See c64.h for scope.  Emulation runs one scanline at a time; the VIC
 * raster IRQ is evaluated at the start of each line and rendering uses
 * the register state left by the CPU at the end of the line, which is
 * accurate enough for FS2's border-line raster splits.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "c64.h"
#include "palette.h"

/* ---------------------------------------------------------------- bus */

static int io_visible(const struct c64 *m)
{
    unsigned char p = m->ram[1] & 7;
    return p >= 5; /* CHAREN=1 and not all-RAM */
}

static void cia1_set_icr(struct c64 *m, unsigned char bits)
{
    m->cia1_icr_data |= bits;
    if (m->cia1_icr_data & m->cia1_icr_mask)
        m->cia1_icr_data |= 0x80;
}

/* recompute the VIC IRQ output level from flags & enables */
static void vic_update_irq(struct c64 *m)
{
    if (m->vic[0x1A] & m->vic[0x19] & 0x0F) {
        m->vic[0x19] |= 0x80;
        m->irq_line |= 1;
    } else {
        m->vic[0x19] &= 0x7F;
        m->irq_line &= ~1;
    }
}

static unsigned char vic_read(struct c64 *m, unsigned reg)
{
    switch (reg) {
    case 0x11:
        return (unsigned char)((m->vic[0x11] & 0x7F) |
                               ((m->raster & 0x100) >> 1));
    case 0x12:
        return (unsigned char)(m->raster & 0xFF);
    case 0x19:
        return m->vic[0x19];
    case 0x1E: /* sprite-sprite collision: unused by FS2 panel sprites */
    case 0x1F: /* sprite-data collision */
        return 0;
    default:
        return m->vic[reg];
    }
}

static void vic_write(struct c64 *m, unsigned reg, unsigned char val)
{
    if (m->trace_lines &&
        (reg == 0x11 || reg == 0x12 || reg == 0x16 || reg == 0x18 ||
         reg == 0x19 || reg == 0x1A))
        fprintf(stderr, "  [line %3d] D0%02X <- %02X\n",
                m->raster, reg, val);
    if (reg == 0x19) {
        /* acknowledge: writing 1s clears flags; the IRQ line is level-
           sensitive and must drop the moment the flag clears */
        m->vic[0x19] &= (unsigned char)~(val & 0x0F);
        vic_update_irq(m);
        return;
    }
    m->vic[reg] = val;
    if (reg == 0x1A)
        vic_update_irq(m);
}

static unsigned char cia1_read(struct c64 *m, unsigned reg)
{
    switch (reg) {
    case 0x00: /* PRA: keyboard columns / joystick 2 (active low) */
        return (unsigned char)(m->cia1_pra | (unsigned char)~m->cia1_ddra);
    case 0x01: { /* PRB: keyboard rows / joystick 1 */
        unsigned char out = (unsigned char)(m->cia1_pra |
                                            (unsigned char)~m->cia1_ddra);
        unsigned char res = 0xFF;
        int c;
        for (c = 0; c < 8; c++)
            if (!(out & (1 << c)))
                res &= (unsigned char)~m->keymatrix[c];
        return res;
    }
    case 0x04: return (unsigned char)(m->cia1_ta & 0xFF);
    case 0x05: return (unsigned char)(m->cia1_ta >> 8);
    case 0x0D: {
        unsigned char v = m->cia1_icr_data;
        m->cia1_icr_data = 0;
        m->irq_line &= ~2;
        return v;
    }
    case 0x0E: return m->cia1_cra;
    default:   return 0;
    }
}

static void cia1_write(struct c64 *m, unsigned reg, unsigned char val)
{
    switch (reg) {
    case 0x00: m->cia1_pra = val; break;
    case 0x01: m->cia1_prb = val; break;
    case 0x02: m->cia1_ddra = val; break;
    case 0x03: m->cia1_ddrb = val; break;
    case 0x04:
        m->cia1_ta_latch = (m->cia1_ta_latch & 0xFF00) | val;
        break;
    case 0x05:
        m->cia1_ta_latch = (m->cia1_ta_latch & 0x00FF) |
                           ((unsigned)val << 8);
        break;
    case 0x0D:
        if (val & 0x80)
            m->cia1_icr_mask |= (val & 0x1F);
        else
            m->cia1_icr_mask &= (unsigned char)~(val & 0x1F);
        break;
    case 0x0E:
        m->cia1_cra = val;
        if (val & 0x10) /* force load */
            m->cia1_ta = m->cia1_ta_latch;
        break;
    default: break;
    }
}

unsigned char c64_read(void *ctx, unsigned addr)
{
    struct c64 *m = (struct c64 *)ctx;
    addr &= 0xFFFF;

    if (addr >= 0xD000 && addr <= 0xDFFF && io_visible(m)) {
        if (addr < 0xD400)
            return vic_read(m, addr & 0x3F);
        if (addr < 0xD800) {
            unsigned r = addr & 0x1F;
            if (r == 0x1B || r == 0x1C) /* SID osc3/env3: pseudo-random */
                return (unsigned char)(rand() & 0xFF);
            return 0;
        }
        if (addr < 0xDC00)
            return (unsigned char)(m->color_ram[addr - 0xD800] | 0xF0);
        if (addr < 0xDD00)
            return cia1_read(m, addr & 0x0F);
        if (addr < 0xDE00) {
            if ((addr & 0x0F) == 0x00)
                return m->cia2_pra;
            if ((addr & 0x0F) == 0x0D)
                return 0;
            return 0;
        }
        return 0;
    }
    return m->ram[addr];
}

void c64_write(void *ctx, unsigned addr, unsigned char val)
{
    struct c64 *m = (struct c64 *)ctx;
    addr &= 0xFFFF;

    if (addr >= 0xD000 && addr <= 0xDFFF && io_visible(m)) {
        if (addr < 0xD400) {
            vic_write(m, addr & 0x3F, val);
            return;
        }
        if (addr < 0xD800)
            return; /* SID: audio TBD */
        if (addr < 0xDC00) {
            m->color_ram[addr - 0xD800] = (unsigned char)(val & 0x0F);
            return;
        }
        if (addr < 0xDD00) {
            cia1_write(m, addr & 0x0F, val);
            return;
        }
        if (addr < 0xDE00) {
            if ((addr & 0x0F) == 0x00)
                m->cia2_pra = val;
            return;
        }
        return;
    }
    m->ram[addr] = val;
}

/* ------------------------------------------------------------- video */

static unsigned vic_base(const struct c64 *m)
{
    return ((unsigned)(~m->cia2_pra & 3)) << 14;
}

/* fetch a byte as the VIC sees it: RAM, except the character ROM
   shadow at $1000-$1FFF in banks 0 and 2 */
static unsigned char vfetch(const struct c64 *m, unsigned addr14)
{
    unsigned base = vic_base(m);
    addr14 &= 0x3FFF;
    if (m->have_chargen && (base == 0x0000 || base == 0x8000) &&
        addr14 >= 0x1000 && addr14 < 0x2000)
        return m->chargen[addr14 - 0x1000];
    return m->ram[(base + addr14) & 0xFFFF];
}

static void render_line(struct c64 *m, int line)
{
    static unsigned char p11, p16, p18, pdd;
    unsigned char d011 = m->vic[0x11];
    unsigned char d016 = m->vic[0x16];
    unsigned char d018 = m->vic[0x18];
    int yscroll = d011 & 7;
    int bmm = (d011 >> 5) & 1;
    int den = (d011 >> 4) & 1;
    int mcm = (d016 >> 4) & 1;
    int y0 = C64_FIRST_VISIBLE_LINE + (yscroll - 3);
    int y = line - y0;
    unsigned screen = ((unsigned)(d018 >> 4)) << 10;   /* 14-bit offsets */
    unsigned bitmap = ((unsigned)(d018 >> 3) & 1) << 13;
    unsigned charset = ((unsigned)(d018 >> 1) & 7) << 11;
    unsigned char *row = m->fb + (unsigned)y * C64_DISPLAY_W * 4;
    unsigned char bg = (unsigned char)(m->vic[0x21] & 0x0F);
    int cx, px;

    if (m->trace_lines &&
        (d011 != p11 || d016 != p16 || d018 != p18 ||
         m->cia2_pra != pdd)) {
        fprintf(stderr,
            "line %3d: D011=%02X D016=%02X D018=%02X DD00=%02X "
            "(irqline=%02X%s)\n",
            line, d011, d016, d018, m->cia2_pra, m->vic[0x12],
            (m->vic[0x11] & 0x80) ? "+256" : "");
        p11 = d011; p16 = d016; p18 = d018; pdd = m->cia2_pra;
    }

    if (y < 0 || y >= C64_DISPLAY_H)
        return;

    if (!den) {
        /* display off: fill with border color */
        unsigned char bc = (unsigned char)(m->vic[0x20] & 0x0F);
        for (px = 0; px < C64_DISPLAY_W; px++)
            memcpy(row + px * 4, c64_palette[bc], 4);
        return;
    }

    for (cx = 0; cx < 40; cx++) {
        int crow = y >> 3, cline = y & 7;
        unsigned cell = (unsigned)(crow * 40 + cx);
        unsigned char sc = vfetch(m, screen + cell);
        unsigned char cr = (unsigned char)(m->color_ram[cell] & 0x0F);
        unsigned char gfx;
        unsigned char pix[8];
        int b;

        if (bmm)
            gfx = vfetch(m, bitmap + (unsigned)crow * 320 +
                            (unsigned)cx * 8 + (unsigned)cline);
        else
            gfx = vfetch(m, charset + (unsigned)sc * 8 + (unsigned)cline);

        if (!mcm) {
            /* hires: bitmap fg/bg from screen byte; text fg from color */
            unsigned char fg, b0;
            if (bmm) {
                fg = (unsigned char)(sc >> 4);
                b0 = (unsigned char)(sc & 0x0F);
            } else {
                fg = cr;
                b0 = bg;
            }
            for (b = 0; b < 8; b++)
                pix[b] = (gfx & (0x80 >> b)) ? fg : b0;
        } else if (bmm) {
            /* multicolor bitmap: 00=bg 01=sc>>4 10=sc&f 11=color ram */
            unsigned char c01 = (unsigned char)(sc >> 4);
            unsigned char c10 = (unsigned char)(sc & 0x0F);
            for (b = 0; b < 4; b++) {
                unsigned char pp = (unsigned char)((gfx >> (6 - b * 2)) & 3);
                unsigned char col = pp == 0 ? bg : pp == 1 ? c01 :
                                    pp == 2 ? c10 : cr;
                pix[b * 2] = col;
                pix[b * 2 + 1] = col;
            }
        } else {
            /* multicolor text */
            if (cr & 8) {
                unsigned char c01 = (unsigned char)(m->vic[0x22] & 0x0F);
                unsigned char c10 = (unsigned char)(m->vic[0x23] & 0x0F);
                for (b = 0; b < 4; b++) {
                    unsigned char pp =
                        (unsigned char)((gfx >> (6 - b * 2)) & 3);
                    unsigned char col = pp == 0 ? bg : pp == 1 ? c01 :
                                        pp == 2 ? c10 :
                                        (unsigned char)(cr & 7);
                    pix[b * 2] = col;
                    pix[b * 2 + 1] = col;
                }
            } else {
                for (b = 0; b < 8; b++)
                    pix[b] = (gfx & (0x80 >> b)) ? cr : bg;
            }
        }
        for (b = 0; b < 8; b++)
            memcpy(row + (cx * 8 + b) * 4, c64_palette[pix[b] & 0x0F], 4);
    }

    /* sprites (drawn over graphics; FS2 panel needles are in front) */
    {
        unsigned char en = m->vic[0x15];
        int s;
        for (s = 7; s >= 0; s--) {
            unsigned sy, sx;
            int xexp, yexp, mc, srow, i;
            unsigned char ptr;
            unsigned addr;
            if (!(en & (1 << s)))
                continue;
            sy = m->vic[0x01 + s * 2];
            /* sprite Y is raster-based: visible line = sy .. sy+20 */
            yexp = (m->vic[0x17] >> s) & 1;
            srow = line - (int)sy;
            if (yexp)
                srow /= 2;
            if (srow < 0 || srow > 20)
                continue;
            sx = m->vic[0x00 + s * 2] |
                 (((unsigned)m->vic[0x10] >> s & 1) << 8);
            xexp = (m->vic[0x1D] >> s) & 1;
            mc = (m->vic[0x1C] >> s) & 1;
            ptr = vfetch(m, (((unsigned)(m->vic[0x18] >> 4)) << 10) +
                            0x3F8 + (unsigned)s);
            addr = ((unsigned)ptr) * 64 + (unsigned)srow * 3;
            for (i = 0; i < 24; i++) {
                int on, sxpix, k, wide;
                unsigned char col = (unsigned char)(m->vic[0x27 + s] & 15);
                unsigned char byte = vfetch(m, addr + (unsigned)(i >> 3));
                if (mc) {
                    unsigned char pp = (unsigned char)
                        ((byte >> (6 - ((i & 6)))) & 3);
                    if (pp == 0)
                        continue;
                    if (pp == 1)
                        col = (unsigned char)(m->vic[0x25] & 15);
                    else if (pp == 3)
                        col = (unsigned char)(m->vic[0x26] & 15);
                    on = 1;
                } else {
                    on = (byte >> (7 - (i & 7))) & 1;
                }
                if (!on)
                    continue;
                wide = xexp ? 2 : 1;
                for (k = 0; k < wide; k++) {
                    /* sprite X: screen x = sx - 24 in our 320px space */
                    sxpix = (int)sx - 24 + i * wide + k;
                    if (sxpix >= 0 && sxpix < C64_DISPLAY_W)
                        memcpy(row + sxpix * 4, c64_palette[col], 4);
                }
            }
        }
    }
}

/* ------------------------------------------------------------ machine */

void c64_init(struct c64 *m)
{
    memset(m, 0, sizeof *m);
    m->cpu.bus.read = c64_read;
    m->cpu.bus.write = c64_write;
    m->cpu.bus.ctx = m;
    m->cia1_ta_latch = 0x4025;
    m->cia1_ta = 0x4025;
    m->cia2_pra = 0x97;
    srand(0x4653); /* deterministic runs; "FS" */
}

void c64_load_snapshot(struct c64 *m, const unsigned char *cpuview,
                       const unsigned char *rawram)
{
    /* cpu view: install I/O register state from the $DXXX window */
    unsigned i;
    for (i = 0; i < 0x2F; i++)
        m->vic[i] = cpuview[0xD000 + i];
    m->vic[0x19] = 0;
    for (i = 0; i < 1024; i++)
        m->color_ram[i] = (unsigned char)(cpuview[0xD800 + i] & 0x0F);
    m->cia1_pra = cpuview[0xDC00];
    m->cia1_ddra = cpuview[0xDC02];
    m->cia1_ddrb = cpuview[0xDC03];
    m->cia2_pra = cpuview[0xDD00];
    /* raster compare: not readable from a dump; a live value would be
       the current line.  Use the RE-known split line; the IRQ handler
       reprograms it every interrupt anyway. */
    m->vic[0x12] = 0x97;
    m->vic[0x11] &= 0x7F;
    /* RAM: raw bank if available (correct bytes under the I/O window),
       else the cpu view as an approximation */
    memcpy(m->ram, rawram ? rawram : cpuview, 65536);
}

int c64_load_chargen(struct c64 *m, const char *path)
{
    FILE *f = fopen(path, "rb");
    size_t n;
    if (!f)
        return -1;
    n = fread(m->chargen, 1, sizeof m->chargen, f);
    fclose(f);
    if (n != sizeof m->chargen)
        return -1;
    m->have_chargen = 1;
    return 0;
}

void c64_key(struct c64 *m, int row, int col, int down)
{
    if (down)
        m->keymatrix[col] |= (unsigned char)(1 << row);
    else
        m->keymatrix[col] &= (unsigned char)~(1 << row);
}

void c64_add_hook(struct c64 *m, unsigned addr,
                  void (*fn)(struct c64 *m), const char *name)
{
    if (m->nhooks >= (int)(sizeof m->hooks / sizeof m->hooks[0])) {
        fprintf(stderr, "hook table full (%s)\n", name);
        return;
    }
    m->hooks[m->nhooks].addr = addr & 0xFFFF;
    m->hooks[m->nhooks].fn = fn;
    m->hooks[m->nhooks].name = name;
    m->hook_at[addr & 0xFFFF] = (unsigned char)(m->nhooks + 1);
    m->nhooks++;
}

/* Run the hook, then emulate the RTS the original routine would do. */
static void run_hook(struct c64 *m, int idx)
{
    unsigned lo, hi;
    m->hooks[idx].fn(m);
    m->cpu.sp = (m->cpu.sp + 1) & 0xFF;
    lo = m->ram[0x100 | m->cpu.sp];
    m->cpu.sp = (m->cpu.sp + 1) & 0xFF;
    hi = m->ram[0x100 | m->cpu.sp];
    m->cpu.pc = (((hi << 8) | lo) + 1) & 0xFFFF;
    m->cpu.cycles += 20; /* nominal cost of the replaced routine */
}

static void step_line_cpu(struct c64 *m)
{
    unsigned long target = m->cpu.cycles + C64_CYCLES_PER_LINE;
    while (m->cpu.cycles < target && !m->cpu.jam) {
        if (m->irq_line && !m->cpu.i)
            cpu6502_irq(&m->cpu);
        if (m->hook_at[m->cpu.pc]) {
            run_hook(m, m->hook_at[m->cpu.pc] - 1);
            continue;
        }
        cpu6502_step(&m->cpu);
    }
    /* CIA1 timer A */
    if (m->cia1_cra & 1) {
        unsigned dec = C64_CYCLES_PER_LINE;
        if (m->cia1_ta <= dec) {
            m->cia1_ta = m->cia1_ta_latch;
            if (m->cia1_cra & 8) /* one-shot */
                m->cia1_cra &= (unsigned char)~1;
            cia1_set_icr(m, 1);
            if (m->cia1_icr_data & 0x80)
                m->irq_line |= 2;
        } else {
            m->cia1_ta -= dec;
        }
    }
}

void c64_run_frame(struct c64 *m)
{
    int line;
    for (line = 0; line < C64_LINES_PER_FRAME; line++) {
        m->raster = line;
        /* raster IRQ */
        {
            int cmp = m->vic[0x12] | ((m->vic[0x11] & 0x80) << 1);
            if (line == cmp)
                m->vic[0x19] |= 1;
        }
        vic_update_irq(m);

        step_line_cpu(m);
        render_line(m, line);
    }
    m->frame++;
}
