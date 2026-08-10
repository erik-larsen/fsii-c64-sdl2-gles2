# Flight Simulator II (C64) -> C90 / SDL2 / OpenGLES2 port
#
# This Makefile builds the reverse-engineering toolchain and reproduces the
# extraction + disassembly pipeline from the original SubLogic disks.  The
# original disks, extracted binaries, and disassembly are NOT committed;
# everything under build/ is regenerated locally from original-disks/.
#
# Existing tools used by the extraction path:
#   VICE (x64sc)  - runs the disk through full 1541 fastloader emulation
#   da65          - cc65's 6502 disassembler
# Our own C90 tools live in tools/ and cover T64/D64 handling, a tracing
# disassembler, and a 6502 interpreter used as extraction/verification core.

CC      ?= cc
CFLAGS  ?= -std=c90 -pedantic -Wall -O2
BUILD    = build
DISKS    = original-disks
GAME_D64 = $(DISKS)/Flight_Simulator_II_(Disk)_A_-_Game.d64

TOOLS = $(BUILD)/t64x $(BUILD)/d64x $(BUILD)/dis6502 $(BUILD)/unpack

.PHONY: all tools extract disasm vice-dump clean re
all: tools

# --- toolchain ---------------------------------------------------------
tools: $(TOOLS)

$(BUILD)/t64x: tools/t64x.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<
$(BUILD)/d64x: tools/d64x.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<
$(BUILD)/dis6502: tools/dis6502.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<
$(BUILD)/unpack: tools/unpack.c src/cpu6502.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD):
	mkdir -p $(BUILD)

# --- extract the disk boot chain (no emulation needed) -----------------
extract: $(BUILD)/d64x
	$(BUILD)/d64x "$(GAME_D64)" dir
	$(BUILD)/d64x "$(GAME_D64)" chain 18 2 $(BUILD)/fs2boot.prg

# --- run the disk in VICE and dump the fully-loaded 64K image ----------
# Requires VICE (x64sc) and python3.  Launches headless in warp mode.
vice-dump: | $(BUILD)
	@pkill -9 x64sc 2>/dev/null || true
	SDL_VIDEODRIVER=dummy x64sc -warp -sounddev dummy \
	    -binarymonitor -binarymonitoraddress ip4://127.0.0.1:6502 \
	    -autostart "$(GAME_D64)" \
	    -exitscreenshot "$(PWD)/$(BUILD)/fs2-vice.png" \
	    > $(BUILD)/vice.log 2>&1 & \
	sleep 6
	python3 tools/vice_dump.py $(BUILD)/fs2-vice-mem.bin 127.0.0.1 6502 12
	@echo "golden image: $(BUILD)/fs2-vice-mem.bin, reference: $(BUILD)/fs2-vice.png"

# --- disassemble the golden image --------------------------------------
# Traced listing (our tracer) + aligned da65 listing (code map fed in).
disasm: $(BUILD)/dis6502 $(BUILD)/fs2-vice-mem.bin
	$(BUILD)/dis6502 $(BUILD)/fs2-vice-mem.bin 0000 \
	    -a tools/fs2.anno -e 2285 -e 2700 -e 4544 -e 8180 \
	    -o $(BUILD)/fs2-golden.lst -m $(BUILD)/fs2-golden.map
	python3 tools/gen_da65_info.py $(BUILD)/fs2-vice-mem.bin $(BUILD)/fs2.info
	python3 tools/map_to_da65.py $(BUILD)/fs2-golden.map \
	    $(BUILD)/fs2.info $(BUILD)/fs2-full.info
	da65 --info $(BUILD)/fs2-full.info
	@echo "listings: $(BUILD)/fs2-golden.lst (traced), build/fs2-da65-aligned.s"

# full pipeline from disks to disassembly
re: tools extract vice-dump disasm

clean:
	rm -rf $(BUILD)
