#!/usr/bin/env python3
"""
Generate a da65 info file for the fully-loaded FS2 memory image.

The image (build/fs2-vice-mem.bin) is a full 64K CPU snapshot taken from
VICE after the disk finished loading, so file offset == absolute address
and STARTADDR is $0000.

We seed da65 with the interrupt/handler entry points read live from the
snapshot plus the entry points discovered during static RE, and label the
C64 I/O registers.  da65 follows control flow from CODE labels and leaves
the rest as data.
"""

import sys

IMG = sys.argv[1] if len(sys.argv) > 1 else "build/fs2-vice-mem.bin"
OUT = sys.argv[2] if len(sys.argv) > 2 else "build/fs2.info"

d = open(IMG, "rb").read()

def w(a):
    return d[a] | (d[a + 1] << 8)

# entry points: (addr, name)
entries = [
    (w(0xfffe), "irq_hw"),
    (w(0xfffa), "nmi_hw"),
    (w(0x0314), "cinv"),
    (w(0x0318), "nminv"),
    (0xcba6, "snd_irq_setup"),
    (0x25d6, "vic_init_a"),
    (0x789b, "vic_init_b"),
    (0x2285, "irq_main"),
]
# de-dup by address, keep first name
seen = {}
for a, n in entries:
    if a not in seen:
        seen[a] = n

# C64 hardware register labels
hw = {
    0xd000: "VIC_S0X", 0xd001: "VIC_S0Y", 0xd010: "VIC_SXMSB",
    0xd011: "VIC_CR1", 0xd012: "VIC_RASTER", 0xd015: "VIC_SPREN",
    0xd016: "VIC_CR2", 0xd017: "VIC_SPYEXP", 0xd018: "VIC_MEMPTR",
    0xd019: "VIC_IRQ", 0xd01a: "VIC_IRQEN", 0xd01b: "VIC_SPRPRI",
    0xd01c: "VIC_SPRMC", 0xd01d: "VIC_SPXEXP", 0xd020: "VIC_BORDER",
    0xd021: "VIC_BG0", 0xd022: "VIC_BG1", 0xd023: "VIC_BG2",
    0xd025: "VIC_SMC0", 0xd026: "VIC_SMC1", 0xd027: "VIC_S0C",
    0xdc00: "CIA1_PRA", 0xdc01: "CIA1_PRB", 0xdc02: "CIA1_DDRA",
    0xdc03: "CIA1_DDRB", 0xdc04: "CIA1_TALO", 0xdc05: "CIA1_TAHI",
    0xdc0d: "CIA1_ICR", 0xdc0e: "CIA1_CRA",
    0xdd00: "CIA2_PRA", 0xdd0d: "CIA2_ICR",
}
for base, nm in [(0xd400, "SID")]:
    for i in range(0x1d):
        hw[base + i] = "%s_%02X" % (nm, i)

with open(OUT, "w") as f:
    f.write("GLOBAL {\n")
    f.write('    INPUTNAME "%s";\n' % IMG)
    f.write('    OUTPUTNAME "build/fs2-da65-aligned.s";\n')
    f.write("    STARTADDR $0000;\n")
    f.write('    CPU "6502";\n')
    f.write("    COMMENTS 1;\n")
    f.write("    LABELBREAK 1;\n")
    f.write("};\n\n")
    for a, n in sorted(seen.items()):
        f.write("LABEL { NAME \"%s\"; ADDR $%04X; };\n" % (n, a))
    f.write("\n")
    for a in sorted(hw):
        f.write("LABEL { NAME \"%s\"; ADDR $%04X; };\n" % (hw[a], a))

print("wrote %s with %d entry points" % (OUT, len(seen)))
for a, n in sorted(seen.items()):
    print("  $%04X %s" % (a, n))
