# Book / Web Series Plan

A companion to the faithful C port of subLOGIC's *Flight Simulator II* (Commodore 64). The model is Fabien Sanglard's **Game Engine Black Book** series (*Wolfenstein 3D*, *Doom*): explain the hardware first, so each engine technique reads as a response to a specific constraint.

## Format

- **Web series first, book second.** Write chapters as the port progresses, then collect and reorder them into a book once the port and text are stable. (Sanglard's books grew out of years of code-review articles on his site.)
- **One source, two outputs.** Text lives in Markdown/AsciiDoc and figures in SVG sources plus wasm embeds. The web build gets live figures; the print/PDF build swaps each embed for a static frame (Pandoc or Asciidoctor). The book is a build target, not a rewrite.

## Relationship to the C port

- **Faithful translation first, then ports and reimplementations.** The faithful port keeps the original data layouts, fixed-point formats and control structure. It's the reference the text cites when it says "this is how it actually worked."
- **Tag a citable baseline** (e.g. `faithful-1.0`). The book cites that tag, so later ports can diverge without breaking references.
- **A ported subsystem is a chapter ready to write.** Once a subsystem is ported and verified, it is understood. Writing it up exposes gaps early.
- **Diff testing.** Compare the faithful port against the original (VICE memory/bitmap dumps, same inputs). This builds confidence and gives the text verified claims, e.g. "matches the original frame-for-frame over N frames of approach X."
- **Three-way cross-reference.** Keep a stable map from original address to C function to chapter section, e.g. `$xxxx` → `clip_line()` → §5.3. Readers can follow prose → C → 6502.

## Draft outline

1. **The Machine**
   - 6510: no multiply/divide, 8-bit registers, zero page as pseudo-registers
   - VIC-II bitmap layout: 8×8 cell organization makes pixel plotting expensive
   - Memory map and banking around ROMs and I/O
   - SID
   - 1541: slow serial bus; subLOGIC's copy protection
2. **People and Lineage**
   - Bruce Artwick, his 1976 thesis on a computer-generated flight display, subLOGIC
   - Apple II original; Atari and C64 ports and how they differ
   - Sibling: Microsoft Flight Simulator 1.0 (PC)
3. **The Engine** (one chapter each)
   - Number representation: fixed-point formats, table-driven multiply, trig
   - World database: scenery encoding, scenery-disk format
   - 3D pipeline: transform, clipping (likely the most interesting part on a 6502), projection, line drawing into the bitmap
   - Frame composition: horizon/ground, instrument panel, how 3D view and panel share the screen
   - Flight model: integration step and how it is split across frames
   - Main loop and timing: the frame budget and where cycles go
4. **Ports and Descendants**
   - Faithful C vs. modern reimplementation (floats vs. fixed-point, GLES vs. bitmap rasterizer): what the 1984 constraints cost and what they bought

Chapter order during writing follows the port, not this outline. Chapters 1–2 don't depend on the port and can be written in parallel.

## Figures

The hand-drawn diagrams do most of the explaining in Sanglard's books. Must-haves here:

- Memory map
- VIC-II bitmap addressing
- Clipping cases
- Frame-time breakdown

Interactive (wasm builds of faithful-port pieces, web only):

- Step-through line clipper
- Bitmap plot routine highlighting the bytes it touches
- Live frame-budget bar

## Code excerpts

Short annotated 6502 listings side by side with their C equivalents. Short and commented, not long dumps.

## Primary sources

- Bruce Artwick, 1976 University of Illinois thesis on a computer-generated dynamic flight display
- Bruce Artwick, *Applied Concepts in Microcomputer Graphics* (1984): describes the techniques generically and names Sublogic's A2-3D package; doesn't name FS II. Checklist in [notes/artwick-1984.md](notes/artwick-1984.md)
- Sanglard, *Game Engine Black Book: Wolfenstein 3D* (2017) and *Doom* (2018), for structure and tone

## Next step

Map which subsystems are already ported, and order the first web chapters to follow them.
