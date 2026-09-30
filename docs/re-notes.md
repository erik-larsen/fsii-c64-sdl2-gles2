# FS2 (C64) reverse-engineering notes

**Authoritative source: the DISK build.**  The tape image is a cut-down
version; the disk (`Flight_Simulator_II_(Disk)_A_-_Game.d64`) is the real
game and is what we port.  The two are closely related builds — e.g. VIC
init is at `$25D6` (tape) vs `$25EB` (disk), a ~$15-byte shift — so tape
analysis remains a useful cross-reference, but disk addresses win.

## Extraction pipeline (reproducible: `make re`)

The disk uses a custom sector-level fastloader (the CBM directory holds a
single dummy `FLIGHT SIMULATOR` entry), so ordinary file tools can't
recover the game.  We use existing tools to run and dump it:

- **VICE `x64sc`** runs the disk headless in warp with full 1541
  emulation (the fastloader Just Works), and `tools/vice_dump.py` drives
  VICE's binary monitor to snapshot the fully-loaded 64K CPU image to
  `build/fs2-vice-mem.bin`.  `-exitscreenshot` also captures the running
  game (`build/fs2-vice.png`) — the golden reference for the port.
- **`da65`** (cc65) disassembles the golden image; `tools/map_to_da65.py`
  feeds it our tracer's code/data map as RANGE directives so its decode
  stays aligned across data (`build/fs2-da65-aligned.s`, ~17.7k lines).
- **`tools/dis6502`** (ours) produces a control-flow-traced listing
  seeded from the live interrupt vectors (`build/fs2-golden.lst`).

The golden image: `$01=$35` (all-RAM banking — game runs with ROMs banked
out), 79.6% of RAM populated.  Live vectors read from it:
`IRQ $FFFE -> $2285`, `NMI $FFFA -> $2700`, `CINV $0314 -> $4544`,
`NMINV $0318 -> $8180`, `RESET $FFFC -> $B520`.

## Disk boot chain

1. `LOAD"*",8,1` loads the 1-block boot file at 18/2 to `$034B`.
2. Boot opens the drive command channel (`OPEN 15,8,15`) and a
   direct-access buffer (`OPEN 2,8,2,"#"`), then issues `U1` block-read
   commands (`"U1:2 0 1 s"`) for track 1 sectors 0..7, copying each 256-
   byte sector via `CHRIN` to `$7300..$7AFF`, then `JMP $7300` (stage 2).
3. Stage 2 (`$7300`) has its own drive routines / fastloader (uploads
   drive-side code) that pull in the rest of the game.  In the hybrid
   runtime this is the seam where a C reimplementation services sector
   reads directly from the `.d64` instead of emulating drive-side code.

## Stage-2 loader ($7300-$7AFF): fully decoded

Jump table `$7300+`: `$78A2` cold-boot init/load, `$791E` system-error
handler, `$7410` drive init (I0 + OPEN 2,#), `$7367` read 4-sector
block, `$73B4` write 4-sector block (U2), `$7449` close, `$7457` format
("N0:SUBLOGIC CORP.,FS").  The drive runs *stock* CBM DOS — commands are
plain "U1:2 0 tt ss" strings; only the IEC serial protocol itself is
custom code (`$76xx-$78xx` bit-banging `$DD00`, needed because the
Kernal is banked out).

Data flow of a block read (`$7367`): linear block index `$731C` -> BCD
track/sector via per-track sector bitmap at `$7473` (3 bytes/track,
MSB-first; excludes reserved sectors) computed in `$74DC`; ASCII into
command template at `$7334` (`$7335` toggles U1/U2); send to ch 15;
error channel read (`$75A1` into "00, OK,00,00" buffer at `$7579`);
data received via `$7618` into memory at `$C2/$C3` (auto-increment,
loaded from `$731E/$731F`, guarded below `$C000` when `$7327` set);
4 sectors per call, cursor+pointer written back.

Cold boot (`$78A2`) also runs a protection-flavored disk check
(`$7848`): reads sector 0 of tracks from a table at `$7836` expecting
specific error codes, accumulating mismatches into `$7326`.

**C port seam (src/diskio.c)**: hooks replace the five channel
primitives (`$7657`/`$765B` send, `$7618` receive, `$75DD` open,
`$75FC` close) with a small CBM-DOS interpreter serving a .d64 image.
All block math and loader logic still executes as original 6502 code.
U2 writes go to the in-memory image only.

`$7A00-$7AFF` holds the four multicolor panel sprites (pointers
`$E8-$EB` in VIC bank 1).

## Keyboard and command dispatch

- `$2574` scans one matrix column per call (double-read debounce);
  `$2418` walks all 8 columns per pass, decoding through the 64-entry
  table at `$2482` (index = column*8 + row).  New keys enter a 4-slot
  rollover list `$089B-$089E` with a per-slot countdown `$0E71,X`
  (auto-repeat), then a ring buffer `$08A9` (`$25DC` push, `$25C4`
  pop).
- CTRL decodes to `$FB` and latches `$0E67`; later keys get `-$40`
  (CTRL-Z = `$1A`).  CTRL must be down a scan pass *before* the key.
- `$27D8` pops a key and dispatches through a self-modified
  `JMP ($0000)` into the 96-entry handler table at `$2718`
  (default `$2CCD` = no-op).  **v1.0 differs from the reference card:**
  CTRL-Z has no top-level handler, CTRL-X (`$2B0D`) cycles a radio
  selector, CTRL-E (`$2F4C`) sets `$0E87` (disk database request).
- While `$08BA` is set (editor/prompt), keys go to `$08C4` instead.

## Editor overlay and mode library

- `E` sets `$08BA`; main loop (`$204B`) calls the overlay entry `$A7E8`.
  The editor code (track 10) loads to `$A7E0` and is paged to/from
  `$D000-$E3FF` (RAM under I/O) by `$E6D8` (copy) / `$E6DB` (swap).
- Pages: Simulation Control, Aircraft Position, Environmental Control.
  Display is **text mode, VIC bank 0, screen `$0400`, charset `$1800`**
  (`D018=$16`) — i.e. the C64 character ROM, not the game's own font.
- Mode library disk I/O lives in the editor's key handler: CTRL-Z
  (`$DA43`) = init, **format** (`N0:SUBLOGIC CORP.,FS`), write block `$40`
  from the library at (`$0961`); CTRL-X (`$DA11`) = read block `$40` back.
  Because it formats, the manual insists on a separate user disk.

## Copy protection

- Cold boot `$7848` reads sector 0 of tracks 1-9 and sums (actual -
  expected) DOS error codes from the table at `$7836` into `$7326`.  The
  on-disk table expects no errors (the game disk has none); after boot
  the track-3 entry reads `$21`, so later checks require **21 READ ERROR
  on track 3** — which every scenery/Star disk image records in its
  error bytes (all of track 3 = `$03`).
- `$E565` (called at `$207D` after scenery loads): if `$7326 != 0` and
  `$5F`/`$22` bit 7 set, points NMI at an RTI and hangs forever.
- Consequence for the port: honoring .d64 error bytes is required for
  scenery disks to work.  diskio does; it never bypasses the check.

## Flight display: double-buffered raster split

The `$2285` IRQ alternates two phases via `$4A`: phase B (raster 32,
top border) sets multicolor + the 3D-window bitmap and latches raster
163; phase A (163) sets hires bitmap `$4000` for the panel region and
latches 32.  The phase-B `D018` immediate at `$22B0` is **self-modified
by the game** (`$F0` <-> `$F8`) to page-flip the 3D view between
bitmaps `$4000`/`$6000`.  Phase B also drives the panel updater
(`$2418`/`$2505`) every few frames, acknowledging the IRQ only at the
end — which is why the VIC IRQ line must be emulated level-sensitively:
an ack must drop the line immediately, or the other phase re-enters
right after RTI and the split collapses (the bug behind the striped 3D
window; fixed in c64.c `vic_update_irq`).

## Disk display architecture (IRQ `$2285`)

Raster-split IRQ: banks `$01=$35`, then rewrites `$D018` (screen/bitmap
pointers) and `$D016` mid-frame to split the screen into the top 3D
bitmap window and the bottom instrument panel; `$D020` border tweaked per
region.  VIC init `$25EB`: `$D011=$3B` (bitmap on), `$DD00=$96` (VIC bank
1 = `$4000-$7FFF`), 4 multicolor sprites (panel needles/indicators).

---

## Tape crack notes (secondary reference)

Notes below are from the tape crack image
(`Flight_Simulator_II_(Tape).t64`, "ASS PRESENTS" crack, single file
`$0801-$C85B`); addresses refer to its unpacked image
(`build/fs2-unpacked.bin`).

## Boot chain

1. T64 file loads at `$0801`; BASIC stub `SYS 2065` -> `$0811`:
   `LDA #$36 / STA $01 / JMP $C783` (crack stub).
2. `$C783` relocates an RLE depacker to `$0333-$03EA`, runs it at
   `$0384`.  Stage 1: escape byte `$D3`, reads source *backward* from
   `$C782`, writes downward from `$C9FF` to `$0800`.  Terminator is
   `$D3 $D3`.  Literal `$D3` bytes cannot be encoded; a fix-up list of
   20 absolute addresses is patched afterward ($2040, $2046, $2063,
   $43C4, $5F2C, $71E6, $72C9, $7424, $85C6, $9806, $980A, $9CC0,
   $9E99, $9FFE, $A026, $A1F4, $A333, $A58D, $B120, $B1D0).
   The depacker template's first $1F bytes restore the original
   `$0801-$081F`.
3. Crack tail ends `JMP $A8BC`, which in this dump is data (probably a
   quirk of how the T64 was produced).  The *original* boot path
   restored at `$0801` works: `$0811`: `LDA #$36 / STA $01 /
   JSR $A960 / JMP $A88E`.
4. `$A960`: copies `$2000` bytes `$AA00-$C9FF` -> `$E000-$FFFF`
   (game code/data lives in RAM under the Kernal ROM, including the
   hardware IRQ vectors).
5. `$A88E`: stage-2 relocator, same depacker structure, escape byte
   `$E3`, source backward from `$A88D`, unpacks `$BFFF` down to
   `~$1178`.  Fix-up handled the same way.
6. Real game init follows; first I/O touches at `$CBA6` (CIA int
   masks).  IRQ vector `$0314` set to `$4544` during early init; later
   the game switches `$01` to `$35`/`$30` (all-RAM) and installs its
   own hardware vectors directly at `$FFFE/$FFFF`.

## Memory map (so far)

| Range        | Contents                                             |
|--------------|------------------------------------------------------|
| `$0801-$081F`| original boot stub                                   |
| `$1178-$BFFF`| main program + data (stage-2 unpack target)          |
| `$4000-$7FFF`| VIC bank 1: bitmap `$6000-$7F3F`, screen `$7C00`     |
| `$AA00-$C9FF`| copied to `$E000-$FFFF` at boot                      |
| `$E000-$FFFF`| high code + data (RAM under Kernal), hw vectors      |

## Display architecture (from `$25D6` and `$789B` init routines)

- `$D011 = $3B`: standard bitmap mode, display on, 25 rows.
- `$DD00 = $96`: VIC bank 1 (`$4000-$7FFF`).
- Screen matrix at `$7C00` (sprite pointers at `$7FF8+`), so `$D018`
  high nibble = `%1111`; bitmap at `$6000`.
- Raster-split display: IRQ at raster `$97` (line 151), hardware
  vector -> `$2270`; a second handler at `$22EF` (installed by the
  routine at `$265E`).  Top region = 3D window (bitmap), bottom =
  instrument panel.
- 4 sprites enabled, multicolor, pointers `$E8-$EB` (data at
  `$7A00-$7AFF`), parked around X=`$58-$68`, Y=`$AA-$B5` — panel
  indicator sprites (control positions / needles).
- Background color loaded from variable `$0E69`.

## Sound

- SID init at `$CC1D-$CC79`; voice setup around `$2370-$2390`
  (`$D40E/$D40F` voice 3 freq — engine drone, and voice-3-as-noise
  reads are common for RNG; check `$D41B` reads).

## Interrupts / timing

- CIA1 timers configured at `$CB42-$CB48` (`$DC0E/$DC0F/$DC08`).
- VIC raster IRQ drives the display split and pacing.
- Handlers: `$2270`, `$22EF` (hardware vector path, all-RAM mode);
  `$4544` via `$0314` during boot (Kernal-mapped phase).

## Hot subroutines (JSR histogram from boot trace)

- `$17EF` (45 calls), `$188A` (28), `$1DDE` (23), `$16AA` (16),
  `$1961` (9), `$3A49` (8) — candidates: math primitives
  (multiply/divide), memory fills, drawing helpers.  To be identified.

## Порting methodology

Hybrid runtime (`src/`): the 6502 core executes the original image;
routines are ported to C90 one at a time and hooked at their entry
addresses (see `fs2_hooks.c`).  A ported routine must produce the same
RAM/register effects as the original; verification = run both paths,
diff memory + framebuffer.

## Open questions / next steps

- Full exec-map coverage: run the sim interactively (runtime task) and
  re-dump the exec map to classify flight-time code.
- Identify the 3D pipeline: world database format, rotation math
  (`$17EF` etc.), line clipper, bitmap line drawer.
- Disk version: `d64x map` shows the game disk uses a custom
  sector-level format (dummy CBM directory).  Locate the loader on
  track 18 neighborhood and map which sectors load where; needed for
  scenery-disk support.
- The scenery-disk database format (nav aids, runways, polygons).
