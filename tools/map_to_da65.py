#!/usr/bin/env python3
"""
Fold a dis6502 code/data map into the da65 info file as RANGE directives.

dis6502 -m writes one byte per address: 'C' where it traced an opcode,
'D' otherwise (newlines every 64 bytes for readability).  da65 does a
linear decode and drifts out of phase over data; emitting Code ranges for
the traced-code runs and ByteTable ranges for the rest keeps da65's
instruction boundaries aligned with the real code.

Usage: map_to_da65.py fs2-golden.map fs2.info fs2-full.info
"""

import sys

mp = open(sys.argv[1]).read().replace("\n", "")
base_info = open(sys.argv[2]).read()
out = sys.argv[3]

# Contiguous runs of equal class.
runs = []
i = 0
n = len(mp)
while i < n:
    c = mp[i]
    j = i
    while j < n and mp[j] == c:
        j += 1
    runs.append((c, i, j - 1))
    i = j

# Reserve the top vectors as data so da65 does not try to decode them.
with open(out, "w") as f:
    f.write(base_info.rstrip() + "\n\n")
    f.write("# ranges from dis6502 execution trace\n")
    for c, a, b in runs:
        if a > 0xFFF9:
            c = "D"
        t = "Code" if c == "C" else "ByteTable"
        f.write("RANGE { START $%04X; END $%04X; TYPE %s; };\n" % (a, b, t))

codeb = sum(b - a + 1 for c, a, b in runs if c == "C")
print("wrote %s: %d runs, %d code bytes, %d data bytes"
      % (out, len(runs), codeb, n - codeb))
