ROM = hello.sms

# In-game mini-games live in games/<name>/game.asm (see DESIGN.md). Each
# builds an 8KB image that runs from Z80 RAM; tools/gen_z80_game.py packages
# it into the header the 68K compiles against. One game is wired into the
# 68K side today (maze); multi-game selection is milestone M2.
GAME ?= maze
GDIR  = games/$(GAME)

all: $(ROM) $(GDIR)/game.bin games/hello-c/game.bin

# C games: SDCC -mz80 against harness.h (no SMSlib — the harness contract
# is the platform). crt0 at $0000, data above the map region, makebin
# pads to the 8KB image the packager expects.
games/hello-c/crt0.rel: games/hello-c/crt0.s
	cd games/hello-c && sdasz80 -o crt0.rel crt0.s

# NOTE: -o out.ihx, NOT game.ihx — SDCC names every intermediate after
# the output, and a generated game.asm would shadow the hand-written-asm
# convention (the packager treats game.asm as the contract source).
games/hello-c/game.bin: games/hello-c/main.c games/hello-c/harness.h games/hello-c/crt0.rel
	cd games/hello-c && sdcc -mz80 --no-std-crt0 --opt-code-size \
	    --code-loc 0x0080 --data-loc 0 crt0.rel main.c -o out.ihx
	cd games/hello-c && makebin -p out.ihx game.bin
	python3 ../tools/gen_z80_game.py hello-c

# tiles.inc (the metatile table) comes from sms/tileset.json via the same
# codegen that builds the SH-2's sms_tiles.h — see the root Makefile rule.
games/maze/tiles.inc: tileset.json ../tools/gen_sms_tiles.py
	python3 ../tools/gen_sms_tiles.py

$(GDIR)/game.o: $(GDIR)/game.asm games/maze/tiles.inc
	cd $(GDIR) && wla-z80 -o game.o game.asm

# Standalone maze: the same game.asm wrapped in a mode-4 shim, as a real
# .sms for Ares' MASTER SYSTEM core (fast art/iteration loop — no 32X
# boot). tiles_sms.inc rides the same codegen as tiles.inc.
maze-ares: games/maze/maze.sms
games/maze/maze.sms: games/maze/standalone.asm games/maze/game.asm games/maze/tiles.inc
	cd games/maze && wla-z80 -o standalone.o standalone.asm
	printf '[objects]\nstandalone.o\n' > games/maze/standalone.linkfile
	cd games/maze && wlalink -d standalone.linkfile maze.sms

$(GDIR)/game.linkfile: Makefile
	printf '[objects]\ngame.o\n' > $(GDIR)/game.linkfile

$(GDIR)/game.bin: $(GDIR)/game.o $(GDIR)/game.linkfile
	cd $(GDIR) && wlalink -d game.linkfile game.bin
	python3 ../tools/gen_z80_game.py $(GAME)

font_1bpp.bin: gen_font.py
	python3 gen_font.py

hello.o: hello.asm font_1bpp.bin
	wla-z80 -o hello.o hello.asm

linkfile: Makefile
	printf '[objects]\nhello.o\n' > linkfile

$(ROM): hello.o linkfile
	wlalink -d linkfile $(ROM)
	python3 verify.py $(ROM)

install: $(ROM)
	cp $(ROM) ../rom/sms-diag.sms

clean:
	rm -f hello.o $(ROM) linkfile font_1bpp.bin \
	      games/*/game.o games/*/game.bin games/*/game.linkfile \
	      games/*/*.rel games/*/*.ihx games/*/*.lst games/*/*.sym \
	      games/*/*.asm.orig games/*/*.map games/*/*.noi games/*/*.lk \
	      games/*/main.asm games/*/*.rst

.PHONY: all install clean
