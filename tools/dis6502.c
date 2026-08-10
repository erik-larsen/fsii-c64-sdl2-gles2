/*
 * dis6502.c - 6502 disassembler for the FS2 porting effort (C90)
 *
 * Two-phase operation:
 *   1. Recursive-descent trace from entry points marks bytes as code,
 *      following branches / JSR / JMP.  Everything else is data.
 *   2. Listing pass emits labeled assembly with C64 hardware register
 *      names, and .byte rows (with ASCII sidebar) for data regions.
 *
 * Usage:
 *   dis6502 image.bin LOADADDR [options]
 *     -e ADDR       add entry point (repeatable, hex)
 *     -a FILE       annotation file: lines "ADDR name [comment...]"
 *     -o FILE       output listing (default stdout)
 *     -m FILE       write code-map (one char/byte: C=code D=data)
 *     -hw           print instructions touching $D000-$DFFF I/O and exit
 *     -jsr          print histogram of JSR targets and exit
 *
 * Addresses on the command line are hex (with or without leading $/0x).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEMSIZE 65536

/* --- opcode table ------------------------------------------------------ */

enum {
    AM_IMP, AM_ACC, AM_IMM, AM_ZP, AM_ZPX, AM_ZPY, AM_ABS, AM_ABX,
    AM_ABY, AM_IND, AM_IZX, AM_IZY, AM_REL, AM_BAD
};

static const int am_len[] = { 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 2, 2, 2, 1 };

struct op { const char *mn; int am; };

static struct op ops[256];

static void set_op(int code, const char *mn, int am)
{
    ops[code].mn = mn;
    ops[code].am = am;
}

static void init_ops(void)
{
    int i;
    for (i = 0; i < 256; i++)
        set_op(i, NULL, AM_BAD);

    set_op(0x00, "BRK", AM_IMP); set_op(0x01, "ORA", AM_IZX);
    set_op(0x05, "ORA", AM_ZP);  set_op(0x06, "ASL", AM_ZP);
    set_op(0x08, "PHP", AM_IMP); set_op(0x09, "ORA", AM_IMM);
    set_op(0x0A, "ASL", AM_ACC); set_op(0x0D, "ORA", AM_ABS);
    set_op(0x0E, "ASL", AM_ABS); set_op(0x10, "BPL", AM_REL);
    set_op(0x11, "ORA", AM_IZY); set_op(0x15, "ORA", AM_ZPX);
    set_op(0x16, "ASL", AM_ZPX); set_op(0x18, "CLC", AM_IMP);
    set_op(0x19, "ORA", AM_ABY); set_op(0x1D, "ORA", AM_ABX);
    set_op(0x1E, "ASL", AM_ABX); set_op(0x20, "JSR", AM_ABS);
    set_op(0x21, "AND", AM_IZX); set_op(0x24, "BIT", AM_ZP);
    set_op(0x25, "AND", AM_ZP);  set_op(0x26, "ROL", AM_ZP);
    set_op(0x28, "PLP", AM_IMP); set_op(0x29, "AND", AM_IMM);
    set_op(0x2A, "ROL", AM_ACC); set_op(0x2C, "BIT", AM_ABS);
    set_op(0x2D, "AND", AM_ABS); set_op(0x2E, "ROL", AM_ABS);
    set_op(0x30, "BMI", AM_REL); set_op(0x31, "AND", AM_IZY);
    set_op(0x35, "AND", AM_ZPX); set_op(0x36, "ROL", AM_ZPX);
    set_op(0x38, "SEC", AM_IMP); set_op(0x39, "AND", AM_ABY);
    set_op(0x3D, "AND", AM_ABX); set_op(0x3E, "ROL", AM_ABX);
    set_op(0x40, "RTI", AM_IMP); set_op(0x41, "EOR", AM_IZX);
    set_op(0x45, "EOR", AM_ZP);  set_op(0x46, "LSR", AM_ZP);
    set_op(0x48, "PHA", AM_IMP); set_op(0x49, "EOR", AM_IMM);
    set_op(0x4A, "LSR", AM_ACC); set_op(0x4C, "JMP", AM_ABS);
    set_op(0x4D, "EOR", AM_ABS); set_op(0x4E, "LSR", AM_ABS);
    set_op(0x50, "BVC", AM_REL); set_op(0x51, "EOR", AM_IZY);
    set_op(0x55, "EOR", AM_ZPX); set_op(0x56, "LSR", AM_ZPX);
    set_op(0x58, "CLI", AM_IMP); set_op(0x59, "EOR", AM_ABY);
    set_op(0x5D, "EOR", AM_ABX); set_op(0x5E, "LSR", AM_ABX);
    set_op(0x60, "RTS", AM_IMP); set_op(0x61, "ADC", AM_IZX);
    set_op(0x65, "ADC", AM_ZP);  set_op(0x66, "ROR", AM_ZP);
    set_op(0x68, "PLA", AM_IMP); set_op(0x69, "ADC", AM_IMM);
    set_op(0x6A, "ROR", AM_ACC); set_op(0x6C, "JMP", AM_IND);
    set_op(0x6D, "ADC", AM_ABS); set_op(0x6E, "ROR", AM_ABS);
    set_op(0x70, "BVS", AM_REL); set_op(0x71, "ADC", AM_IZY);
    set_op(0x75, "ADC", AM_ZPX); set_op(0x76, "ROR", AM_ZPX);
    set_op(0x78, "SEI", AM_IMP); set_op(0x79, "ADC", AM_ABY);
    set_op(0x7D, "ADC", AM_ABX); set_op(0x7E, "ROR", AM_ABX);
    set_op(0x81, "STA", AM_IZX); set_op(0x84, "STY", AM_ZP);
    set_op(0x85, "STA", AM_ZP);  set_op(0x86, "STX", AM_ZP);
    set_op(0x88, "DEY", AM_IMP); set_op(0x8A, "TXA", AM_IMP);
    set_op(0x8C, "STY", AM_ABS); set_op(0x8D, "STA", AM_ABS);
    set_op(0x8E, "STX", AM_ABS); set_op(0x90, "BCC", AM_REL);
    set_op(0x91, "STA", AM_IZY); set_op(0x94, "STY", AM_ZPX);
    set_op(0x95, "STA", AM_ZPX); set_op(0x96, "STX", AM_ZPY);
    set_op(0x98, "TYA", AM_IMP); set_op(0x99, "STA", AM_ABY);
    set_op(0x9A, "TXS", AM_IMP); set_op(0x9D, "STA", AM_ABX);
    set_op(0xA0, "LDY", AM_IMM); set_op(0xA1, "LDA", AM_IZX);
    set_op(0xA2, "LDX", AM_IMM); set_op(0xA4, "LDY", AM_ZP);
    set_op(0xA5, "LDA", AM_ZP);  set_op(0xA6, "LDX", AM_ZP);
    set_op(0xA8, "TAY", AM_IMP); set_op(0xA9, "LDA", AM_IMM);
    set_op(0xAA, "TAX", AM_IMP); set_op(0xAC, "LDY", AM_ABS);
    set_op(0xAD, "LDA", AM_ABS); set_op(0xAE, "LDX", AM_ABS);
    set_op(0xB0, "BCS", AM_REL); set_op(0xB1, "LDA", AM_IZY);
    set_op(0xB4, "LDY", AM_ZPX); set_op(0xB5, "LDA", AM_ZPX);
    set_op(0xB6, "LDX", AM_ZPY); set_op(0xB8, "CLV", AM_IMP);
    set_op(0xB9, "LDA", AM_ABY); set_op(0xBA, "TSX", AM_IMP);
    set_op(0xBC, "LDY", AM_ABX); set_op(0xBD, "LDA", AM_ABX);
    set_op(0xBE, "LDX", AM_ABY); set_op(0xC0, "CPY", AM_IMM);
    set_op(0xC1, "CMP", AM_IZX); set_op(0xC4, "CPY", AM_ZP);
    set_op(0xC5, "CMP", AM_ZP);  set_op(0xC6, "DEC", AM_ZP);
    set_op(0xC8, "INY", AM_IMP); set_op(0xC9, "CMP", AM_IMM);
    set_op(0xCA, "DEX", AM_IMP); set_op(0xCC, "CPY", AM_ABS);
    set_op(0xCD, "CMP", AM_ABS); set_op(0xCE, "DEC", AM_ABS);
    set_op(0xD0, "BNE", AM_REL); set_op(0xD1, "CMP", AM_IZY);
    set_op(0xD5, "CMP", AM_ZPX); set_op(0xD6, "DEC", AM_ZPX);
    set_op(0xD8, "CLD", AM_IMP); set_op(0xD9, "CMP", AM_ABY);
    set_op(0xDD, "CMP", AM_ABX); set_op(0xDE, "DEC", AM_ABX);
    set_op(0xE0, "CPX", AM_IMM); set_op(0xE1, "SBC", AM_IZX);
    set_op(0xE4, "CPX", AM_ZP);  set_op(0xE5, "SBC", AM_ZP);
    set_op(0xE6, "INC", AM_ZP);  set_op(0xE8, "INX", AM_IMP);
    set_op(0xE9, "SBC", AM_IMM); set_op(0xEA, "NOP", AM_IMP);
    set_op(0xEC, "CPX", AM_ABS); set_op(0xED, "SBC", AM_ABS);
    set_op(0xEE, "INC", AM_ABS); set_op(0xF0, "BEQ", AM_REL);
    set_op(0xF1, "SBC", AM_IZY); set_op(0xF5, "SBC", AM_ZPX);
    set_op(0xF6, "SBC", AM_ZPX); set_op(0xF8, "SED", AM_IMP);
    set_op(0xF9, "SBC", AM_ABY); set_op(0xFD, "SBC", AM_ABX);
    set_op(0xFE, "INC", AM_ABX);
}

/* --- C64 hardware names ------------------------------------------------ */

static const char *hw_name(unsigned a)
{
    switch (a) {
    case 0x0314: return "CINV (IRQ vector)";
    case 0x0316: return "CBINV (BRK vector)";
    case 0x0318: return "NMINV (NMI vector)";
    case 0xD000: return "VIC sprite0 X";
    case 0xD001: return "VIC sprite0 Y";
    case 0xD010: return "VIC sprite X MSB";
    case 0xD011: return "VIC control 1 (bitmap/blank/raster8)";
    case 0xD012: return "VIC raster";
    case 0xD015: return "VIC sprite enable";
    case 0xD016: return "VIC control 2 (multicolor/xscroll)";
    case 0xD017: return "VIC sprite Y expand";
    case 0xD018: return "VIC memory pointers";
    case 0xD019: return "VIC IRQ flag";
    case 0xD01A: return "VIC IRQ enable";
    case 0xD01B: return "VIC sprite priority";
    case 0xD01C: return "VIC sprite multicolor";
    case 0xD01D: return "VIC sprite X expand";
    case 0xD020: return "VIC border color";
    case 0xD021: return "VIC background color";
    case 0xD022: return "VIC bg color 1";
    case 0xD023: return "VIC bg color 2";
    case 0xD025: return "VIC sprite mcolor 0";
    case 0xD026: return "VIC sprite mcolor 1";
    case 0xD027: return "VIC sprite0 color";
    case 0xD028: return "VIC sprite1 color";
    case 0xDC00: return "CIA1 port A (kbd col/joy2)";
    case 0xDC01: return "CIA1 port B (kbd row/joy1)";
    case 0xDC02: return "CIA1 ddr A";
    case 0xDC03: return "CIA1 ddr B";
    case 0xDC04: return "CIA1 timer A lo";
    case 0xDC05: return "CIA1 timer A hi";
    case 0xDC0D: return "CIA1 int control";
    case 0xDC0E: return "CIA1 control A";
    case 0xDD00: return "CIA2 port A (VIC bank)";
    case 0xDD0D: return "CIA2 int control";
    case 0xFFFE: return "IRQ hw vector";
    default:
        if (a >= 0xD400 && a <= 0xD41C) return "SID";
        return NULL;
    }
}

/* --- state ------------------------------------------------------------- */

static unsigned char mem[MEMSIZE];
static unsigned char is_code[MEMSIZE];   /* 1 = opcode byte, 2 = operand */
static unsigned char is_target[MEMSIZE]; /* label needed */
static char *names[MEMSIZE];             /* annotation names */
static char *comments[MEMSIZE];          /* annotation comments */
static unsigned load_addr, end_addr;

static unsigned jsr_count[MEMSIZE];

static int in_range(unsigned a) { return a >= load_addr && a < end_addr; }

/* --- tracing ----------------------------------------------------------- */

#define STACK_MAX 65536
static unsigned trace_stack[STACK_MAX];
static int trace_sp;
static unsigned char queued[MEMSIZE];

static void push_entry(unsigned a)
{
    if (in_range(a) && !is_code[a] && !queued[a] && trace_sp < STACK_MAX) {
        queued[a] = 1;
        trace_stack[trace_sp++] = a;
    }
}

static void trace(void)
{
    while (trace_sp > 0) {
        unsigned pc = trace_stack[--trace_sp];
        for (;;) {
            unsigned char opc;
            struct op *o;
            int len, i;
            unsigned tgt;

            if (!in_range(pc) || is_code[pc])
                break;
            opc = mem[pc];
            o = &ops[opc];
            if (o->mn == NULL)   /* undocumented opcode: treat as data end */
                break;
            len = am_len[o->am];
            if (!in_range(pc + len - 1))
                break;
            is_code[pc] = 1;
            for (i = 1; i < len; i++)
                is_code[pc + i] = 2;

            if (o->am == AM_REL) {
                int off = (signed char)mem[pc + 1];
                tgt = (pc + 2 + off) & 0xFFFF;
                is_target[tgt] = 1;
                push_entry(tgt);
            } else if (opc == 0x20) {            /* JSR */
                tgt = mem[pc + 1] | (mem[pc + 2] << 8);
                is_target[tgt] = 1;
                if (in_range(tgt))
                    jsr_count[tgt]++;
                push_entry(tgt);
            } else if (opc == 0x4C) {            /* JMP abs */
                tgt = mem[pc + 1] | (mem[pc + 2] << 8);
                is_target[tgt] = 1;
                push_entry(tgt);
                break;
            } else if (opc == 0x6C || opc == 0x60 || opc == 0x40) {
                break;                            /* JMP (ind), RTS, RTI */
            } else if (opc == 0x00) {
                break;                            /* BRK */
            }
            pc += len;
        }
    }
}

/* --- output ------------------------------------------------------------ */

static void label_for(unsigned a, char *out)
{
    if (names[a])
        sprintf(out, "%s", names[a]);
    else
        sprintf(out, "L%04X", a);
}

static void operand_str(unsigned pc, char *out, char *cmt)
{
    struct op *o = &ops[mem[pc]];
    unsigned v8 = mem[(pc + 1) & 0xFFFF];
    unsigned v16 = v8 | (mem[(pc + 2) & 0xFFFF] << 8);
    const char *hw;
    char lbl[64];

    cmt[0] = 0;
    switch (o->am) {
    case AM_IMP: out[0] = 0; break;
    case AM_ACC: sprintf(out, "A"); break;
    case AM_IMM: sprintf(out, "#$%02X", v8); break;
    case AM_ZP:  sprintf(out, "$%02X", v8); break;
    case AM_ZPX: sprintf(out, "$%02X,X", v8); break;
    case AM_ZPY: sprintf(out, "$%02X,Y", v8); break;
    case AM_ABS:
        if ((in_range(v16) && (is_target[v16] || names[v16])) ) {
            label_for(v16, lbl);
            sprintf(out, "%s", lbl);
            sprintf(cmt, "$%04X", v16);
        } else
            sprintf(out, "$%04X", v16);
        hw = hw_name(v16);
        if (hw) sprintf(cmt + strlen(cmt), "%s%s", cmt[0] ? " " : "", hw);
        break;
    case AM_ABX:
        sprintf(out, "$%04X,X", v16);
        hw = hw_name(v16);
        if (hw) sprintf(cmt, "%s", hw);
        break;
    case AM_ABY:
        sprintf(out, "$%04X,Y", v16);
        hw = hw_name(v16);
        if (hw) sprintf(cmt, "%s", hw);
        break;
    case AM_IND: sprintf(out, "($%04X)", v16); break;
    case AM_IZX: sprintf(out, "($%02X,X)", v8); break;
    case AM_IZY: sprintf(out, "($%02X),Y", v8); break;
    case AM_REL: {
        int off = (signed char)v8;
        unsigned tgt = (pc + 2 + off) & 0xFFFF;
        label_for(tgt, lbl);
        sprintf(out, "%s", lbl);
        sprintf(cmt, "$%04X", tgt);
        break;
    }
    default: out[0] = 0; break;
    }
}

static void listing(FILE *out)
{
    unsigned pc = load_addr;
    while (pc < end_addr) {
        if (is_code[pc] == 1) {
            struct op *o = &ops[mem[pc]];
            int len = am_len[o->am], i;
            char opstr[64], cmt[128], lbl[64];

            if (is_target[pc] || names[pc]) {
                label_for(pc, lbl);
                fprintf(out, "%s:", lbl);
                if (comments[pc])
                    fprintf(out, "%*s; %s", (int)(24 - strlen(lbl) - 1), "",
                            comments[pc]);
                fprintf(out, "\n");
            }
            operand_str(pc, opstr, cmt);
            fprintf(out, "  %04X  ", pc);
            for (i = 0; i < 3; i++) {
                if (i < len) fprintf(out, "%02X ", mem[pc + i]);
                else fprintf(out, "   ");
            }
            fprintf(out, " %s %-14s", o->mn, opstr);
            if (cmt[0])
                fprintf(out, " ; %s", cmt);
            fprintf(out, "\n");
            pc += len;
        } else {
            /* data run */
            unsigned start = pc, i;
            while (pc < end_addr && is_code[pc] != 1)
                pc++;
            for (i = start; i < pc; i += 8) {
                unsigned n = pc - i < 8 ? pc - i : 8, j;
                if ((is_target[i] || names[i]) ) {
                    char lbl[64];
                    label_for(i, lbl);
                    fprintf(out, "%s:", lbl);
                    if (comments[i]) fprintf(out, "  ; %s", comments[i]);
                    fprintf(out, "\n");
                }
                fprintf(out, "  %04X  .byte ", i);
                for (j = 0; j < n; j++)
                    fprintf(out, "$%02X%s", mem[i + j], j + 1 < n ? "," : "");
                fprintf(out, "%*s ; ", (int)((8 - n) * 4), "");
                for (j = 0; j < n; j++) {
                    int c = mem[i + j];
                    fputc(c >= 32 && c < 127 ? c : '.', out);
                }
                fprintf(out, "\n");
            }
        }
    }
}

/* --- misc modes -------------------------------------------------------- */

static void hw_scan(void)
{
    unsigned pc;
    for (pc = load_addr; pc < end_addr; pc++) {
        struct op *o;
        unsigned v16;
        if (is_code[pc] != 1)
            continue;
        o = &ops[mem[pc]];
        if (o->am != AM_ABS && o->am != AM_ABX && o->am != AM_ABY)
            continue;
        v16 = mem[pc + 1] | (mem[pc + 2] << 8);
        if (v16 >= 0xD000 && v16 <= 0xDFFF) {
            const char *hw = hw_name(v16);
            printf("  %04X  %s $%04X%s%s%s\n", pc, o->mn, v16,
                   o->am == AM_ABX ? ",X" : o->am == AM_ABY ? ",Y" : "",
                   hw ? "  ; " : "", hw ? hw : "");
        }
    }
}

static void jsr_histogram(void)
{
    unsigned a;
    for (a = 0; a < MEMSIZE; a++)
        if (jsr_count[a])
            printf("  %04X: %u calls%s%s\n", a, jsr_count[a],
                   names[a] ? "  " : "", names[a] ? names[a] : "");
}

static unsigned parse_hex(const char *s)
{
    if (s[0] == '$') s++;
    else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    return (unsigned)strtoul(s, NULL, 16);
}

static void load_annotations(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[512];
    if (!f) { perror(path); exit(1); }
    while (fgets(line, sizeof line, f)) {
        char *p = line, *nm, *cm;
        unsigned a;
        if (line[0] == '#' || line[0] == ';' || line[0] == '\n')
            continue;
        a = parse_hex(strtok(p, " \t\n"));
        nm = strtok(NULL, " \t\n");
        if (!nm) continue;
        names[a & 0xFFFF] = (char *)malloc(strlen(nm) + 1);
        strcpy(names[a & 0xFFFF], nm);
        cm = strtok(NULL, "\n");
        if (cm) {
            while (*cm == ' ' || *cm == '\t') cm++;
            comments[a & 0xFFFF] = (char *)malloc(strlen(cm) + 1);
            strcpy(comments[a & 0xFFFF], cm);
        }
        is_target[a & 0xFFFF] = 1;
        push_entry(a & 0xFFFF); /* names are presumed code entry points */
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    FILE *f;
    long size;
    int i;
    int do_hw = 0, do_jsr = 0;
    const char *outpath = NULL, *mappath = NULL, *annopath = NULL;
    FILE *out = stdout;

    if (argc < 3) {
        fprintf(stderr, "usage: %s image.bin LOADADDR [-e ADDR]... "
                        "[-a annofile] [-o out] [-m map] [-hw] [-jsr]\n",
                argv[0]);
        return 1;
    }
    init_ops();

    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    load_addr = parse_hex(argv[2]);
    if (load_addr + size > MEMSIZE) {
        fprintf(stderr, "image exceeds 64K\n");
        return 1;
    }
    end_addr = (unsigned)(load_addr + size);
    if ((long)fread(mem + load_addr, 1, (size_t)size, f) != size) {
        fprintf(stderr, "read error\n");
        return 1;
    }
    fclose(f);

    for (i = 3; i < argc; i++) {
        if (strcmp(argv[i], "-e") == 0 && i + 1 < argc)
            push_entry(parse_hex(argv[++i]));
        else if (strcmp(argv[i], "-a") == 0 && i + 1 < argc)
            annopath = argv[++i];
        else if (strcmp(argv[i], "-x") == 0 && i + 1 < argc) {
            /* execution map: 64K bytes, nonzero = opcode fetched there */
            FILE *xf = fopen(argv[++i], "rb");
            unsigned char xm[MEMSIZE];
            unsigned a;
            if (!xf) { perror(argv[i]); return 1; }
            if (fread(xm, 1, MEMSIZE, xf) != MEMSIZE) {
                fprintf(stderr, "bad exec map\n");
                return 1;
            }
            fclose(xf);
            for (a = 0; a < MEMSIZE; a++)
                if (xm[a])
                    push_entry(a);
        }
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            outpath = argv[++i];
        else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc)
            mappath = argv[++i];
        else if (strcmp(argv[i], "-hw") == 0)
            do_hw = 1;
        else if (strcmp(argv[i], "-jsr") == 0)
            do_jsr = 1;
        else {
            fprintf(stderr, "bad arg %s\n", argv[i]);
            return 1;
        }
    }
    if (annopath)
        load_annotations(annopath);

    trace();

    {
        unsigned code = 0, a;
        for (a = load_addr; a < end_addr; a++)
            if (is_code[a]) code++;
        fprintf(stderr, "traced: %u/%lu bytes marked code (%.1f%%)\n",
                code, (unsigned long)(end_addr - load_addr),
                100.0 * code / (double)(end_addr - load_addr));
    }

    if (do_hw) { hw_scan(); return 0; }
    if (do_jsr) { jsr_histogram(); return 0; }

    if (outpath) {
        out = fopen(outpath, "w");
        if (!out) { perror(outpath); return 1; }
    }
    listing(out);
    if (outpath)
        fclose(out);

    if (mappath) {
        FILE *m = fopen(mappath, "w");
        unsigned a;
        if (!m) { perror(mappath); return 1; }
        for (a = load_addr; a < end_addr; a++) {
            fputc(is_code[a] ? 'C' : 'D', m);
            if (((a - load_addr) & 63) == 63)
                fputc('\n', m);
        }
        fclose(m);
    }
    return 0;
}
