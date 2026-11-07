# Artwick 1984 — Technique Checklist

Source: Bruce A. Artwick, *Applied Concepts in Microcomputer Graphics*, Prentice-Hall, 1984 (ISBN 0-13-039322-3). Artwick was president of Sublogic when he wrote it.

The book never names *Flight Simulator II*. It describes the techniques generically and names Sublogic's own A2-3D animation package (Apple II, 1979). Treat each item below as a hypothesis to confirm or reject against the disassembly. Page numbers are the book's printed pages.

Status key: `?` unchecked · `✓` confirmed in FS II · `✗` not used · `~` variant

## Direct Sublogic references

| Page | What | Status |
|---|---|---|
| 8 (Fig. 1-9) | Photo of a Sublogic flight-sim display on a personal computer | — |
| 293 (Table 8-1) | Profile of Sublogic **A2-3D2** on a 1 MHz Apple II: 16-bit database projected to 280×192. Time split is ~60% transforms, ~40% drawing and erase. Matrix generation is cheaper at zero attitude because the multiply fast-paths 0 and 1. | — |
| 355–356 | Sublogic A2-3D1 (1979) hi-res erase: 32 unrolled `STA page,X` stores, one per 256-byte page. X steps by 3 (`INX` ×3) so the screen clears in a checkerboard rather than bands; an odd step still covers all 256 offsets. | ? |
| 357 | Apple II page flipping ("ping-ponging") between hi-res pages 1 and 2 | ? |

## Main loop and timing

| Page | Technique | What to look for | Status |
|---|---|---|---|
| 189–191 | **Time interleaving**: each loop samples controls, steps the sim, updates the display | Top-level loop structure | ? |
| 190–191 | **Time slicing**: slow-changing items updated on alternate frames by a frame counter | Counter that dispatches to per-frame subsets (instruments? radios?) | ? |
| 191 | Sim step tied to loop period; run physics several times per display frame if it overshoots | Integration step vs. frame rate | ? |
| 192 | Profiling with a hardware real-time clock | Method for our own frame-budget figure | — |
| 322 | Sync to vertical blank; pick a frame rate that divides evenly into the refresh rate | Raster/VBL waits | ? |

## Number representation and arithmetic

| Page | Technique | What to look for | Status |
|---|---|---|---|
| 192, 296–298 | 16-bit "double precision" integers on an 8-bit CPU; binary point placed freely (pure, fractional, mixed) | Fixed-point formats of position, velocity, angles | ? |
| 297, 309 | Two's-complement fractions for sin/cos; $7FFF stands in for 1 | Trig table value range | ? |
| 301 | 32-bit intermediate in perspective: multiply, then divide by z, with no overflow loss | Projection routine | ? |
| 301 | Division bias fix: negative remainder → subtract 1 from the quotient, so points don't shift toward screen center | Post-divide adjustment | ? |
| 302–304 | Shift-and-add multiply, looped or unrolled inline; Booth variant | Multiply routine shape | ? |
| 309 | Multiplier of $7FFF treated as 1 (worst case for shift-and-add) | Compare against $7FFF before multiplying | ? |
| 304–305 | 16×16 from four 8×8 partial products, with correct signed/unsigned handling; drop low×low if 1–2 units of error is OK | Partial-product multiply | ? |
| 306–307 | Non-restoring divide; stop at the bits needed (8-bit quotient for projection) | Divide routine | ? |
| 307 | Shifts for power-of-2 multiplies; shift+add for near-power-of-2 constants | Constant scaling | ? |
| 308–309 | Trig: 0–90° table + linear interpolation, symmetry for other quadrants | Table size, interpolation | ? |
| 309 | **Pseudodegrees**: 1/256 or 1/65536 circle, free wraparound | Heading/pitch/bank storage format (strong candidate) | ? |
| 309–310 | Precompute at build, init, or once-per-frame instead of per point (e.g. fold bias into translation) | Pre-baked data, per-frame setup | ? |

## 3D pipeline

| Page | Technique | What to look for | Status |
|---|---|---|---|
| 205 | Heading → pitch → bank rotation order | Matrix build order | ? |
| 208 | World moves around a static viewer: subtract viewer position, then rotate | Transform order | ? |
| 214–216 | Fixed 90° viewing pyramid (x=±z, y=±z); field of view and zoom by scaling folded into the rotation matrix | Zoom implementation; clip plane tests | ? |
| 219–220 | 3D outcode clipping, 5 bits per point incl. behind-viewer bit; push to pyramid planes | Clipper structure | ? |
| 220 | Point clipped onto an edge plane projects to ±1, so skip the divide | Edge special case in projection | ? |
| 232 | 16-bit transforms adequate for low-resolution screen output | Precision choices | ? |
| 324–325 | **Clipping without dividing**: compute a line's slope once and reuse it across pushes; cache slopes when orientation is unchanged | Slope reuse in clipper | ? |
| 329 | **Reserved points**: shared vertex list, lines by index; transform each vertex once | Scenery object format | ? |
| 330 | **Space framing**: transform only a grid's corners and derive interior lines by averaging (add + shift) | Ground grid / runway generation | ? |
| 325–326 | Text placarding: map a text block's corner points, generate characters in 2D | Runway numbers or 3D text, if any | ? |

## Image generation

| Page | Technique | What to look for | Status |
|---|---|---|---|
| 290 | Double buffering / page flipping | Two VIC-II bitmaps? | ? |
| 291 | Intermediate display-file buffering: transform all lines into a buffer, then erase + draw in one burst | Line list between transform and draw | ? |
| 292 | Atari display-list split: only the 3D half double-buffers; instrument panel stays static | C64 equivalent: raster split / panel in separate memory | ? |
| 292 | Selective update and XOR erase | Instrument needles | ? |
| 310–312 | Fast erase via fastest store, unrolled | C64 bitmap clear | ? |
| 314–316 | 8-octant DDA; traps for horizontal, vertical, 45° lines | Line drawer entry dispatch | ? |
| 316–317 | Plot via rotating mask + `ADC #0` to advance address; y-steps from the bitmap layout, not a per-pixel address computation | Inner loop; VIC-II cell stepping (+1 within cell, cell-row jump at boundary) | ? |
| 318 | **Vectorized** line drawer: keep screen byte in a register until the line leaves it (shallow octants only; not worth it at 35–45°) | Register-held byte in horizontal octants | ? |
| 323–324 | Frame synthesis (interpolate in screen space) and control extrapolation to hide lag | Unlikely, but check | ? |
| 327–329 | **Incremental fill** for sky/ground: refill only the wedge between old and new horizon; 1 or 2 polygons depending on where the horizon lines intersect | Horizon/ground fill, if FS II C64 fills | ? |

## Book use

- Strong chapter openers: incremental fill, the Atari split, pseudodegrees, the A2-3D2 timing table. In each case, quote Artwick's intent (short), then show the C64 code.
- Table 8-1's 60/40 split is a baseline to compare against our own FS II frame-time measurement.
