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

# --- runtime -----------------------------------------------------------
SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS   := $(shell sdl2-config --libs)
RT_SRC = src/main.c src/c64.c src/cpu6502.c src/snapshot.c src/gfx.c \
         src/kbd.c src/diskio.c

.PHONY: all tools extract disasm vice-dump clean re fs2 fbtest web
all: tools fs2

fs2: $(BUILD)/fs2
$(BUILD)/fs2: $(RT_SRC) src/c64.h src/cpu6502.h src/gfx.h src/kbd.h \
              src/snapshot.h src/palette.h src/diskio.h | $(BUILD)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $(RT_SRC) $(SDL_LIBS) \
	    -framework OpenGL

fbtest: $(BUILD)/fbtest
$(BUILD)/fbtest: tools/fbtest.c src/c64.c src/cpu6502.c src/snapshot.c \
                 src/diskio.c src/c64.h src/diskio.h src/palette.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tools/fbtest.c src/c64.c src/cpu6502.c \
	    src/snapshot.c src/diskio.c

# Emscripten web build.  The snapshot is preloaded from build/ into the
# virtual FS; the resulting web/ output contains game data extracted
# from your own disks - do not publish it.
web: | $(BUILD)
	emcc -std=gnu90 -O2 $(RT_SRC) \
	    -s USE_SDL=2 -s FULL_ES2=1 -s WASM=1 \
	    -s ALLOW_MEMORY_GROWTH=1 \
	    --preload-file $(BUILD)/fs2-vice-mem.bin@fs2-snapshot.bin \
	    --preload-file $(BUILD)/fs2-vice-mem.bin.ram@fs2-snapshot.bin.ram \
	    --preload-file $(BUILD)/fs2-vice-mem.bin.regs@fs2-snapshot.bin.regs \
	    --preload-file "$(GAME_D64)@fs2-disk.d64" \
	    -o web/fs2.html
	@echo "web build: web/fs2.html (serve web/ over http)"

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
