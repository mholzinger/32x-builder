# Sample Makefile for Marsdev (32X version)
# For 32X both m68k and SH-2 code has to be built
# The usual variables are split into "MD" and "SH" versions

# Default paths, can be overridden by setting MARSDEV before calling make
MARSDEV ?= ${HOME}/mars
TOOLSBIN = $(MARSDEV)/bin
MDBIN    = $(MARSDEV)/m68k-elf/bin
SHBIN    = $(MARSDEV)/sh-elf/bin

ROMDIR  := rom

# ---- Build profile: which tier of content compiles into this ROM ------------
# One repo, three ROMs (see registry.json "roles" and MAPPING.md):
#   core        the flagship          maps/core + maps/test + maps/curated,
#                                     first-party assets      -> backrooms.32x
#   community   everything            + maps/community + community assets
#                                                        -> backrooms-community.32x
#   author:<h>  a contributor's own   flagship + that author's community maps
#                                                        -> backrooms-<h>.32x
# Usage:  make                      (flagship)
#         make community
#         make author AUTHOR=doublek
# The profile is stamped into $(ROMDIR)/.profile, so switching profiles
# regenerates the two codegen'd files instead of silently reusing the last
# build's map table.
PROFILE ?= core
PROFILE_TAG := $(subst author:,,$(PROFILE))
PROFILE_STAMP := $(ROMDIR)/.profile

TARGET  ?= $(ROMDIR)/backrooms$(if $(filter core,$(PROFILE)),,-$(PROFILE_TAG))
MDTARGET = $(ROMDIR)/md_start

# m68k GCC and Binutils
MDCC   = $(MDBIN)/m68k-elf-gcc
MDCXX  = $(MDBIN)/m68k-elf-g++
MDAS   = $(MDBIN)/m68k-elf-as
MDLD   = $(MDBIN)/m68k-elf-ld
MDNM   = $(MDBIN)/m68k-elf-nm
MDOBJC = $(MDBIN)/m68k-elf-objcopy
# sh2 GCC and Binutils
SHCC   = $(SHBIN)/sh-elf-gcc
SHCXX  = $(SHBIN)/sh-elf-g++
SHAS   = $(SHBIN)/sh-elf-as
SHLD   = $(SHBIN)/sh-elf-ld
SHNM   = $(SHBIN)/sh-elf-nm
SHOBJC = $(SHBIN)/sh-elf-objcopy

# Some files needed are in a versioned directory
MDCC_VER := $(shell $(MDCC) -dumpversion)
SHCC_VER := $(shell $(SHCC) -dumpversion)

# Need the LTO plugin so NM can dump our symbol table
MDPLUGIN = $(MARSDEV)/m68k-elf/libexec/gcc/m68k-elf/$(MDCC_VER)/liblto_plugin.so
SHPLUGIN = $(MARSDEV)/sh-elf/libexec/gcc/sh-elf/$(SHCC_VER)/liblto_plugin.so

MDINCS   = -Isrc_md -Iinc_md
MDINCS  += -I$(MARSDEV)/m68k-elf/lib/gcc/m68k-elf/$(MDCC_VER)/include
SHINCS   = -Isrc -Iinc
SHINCS  += -I$(MARSDEV)/sh-elf/lib/gcc/sh-elf/$(SHCC_VER)/include

MDLIBS   = -L$(MARSDEV)/m68k-elf/lib/gcc/m68k-elf/$(MDCC_VER) -lgcc
SHLIBS   = -L$(MARSDEV)/sh-elf/lib/gcc/sh-elf/$(SHCC_VER) -lgcc

# Any C or C++ standard should be fine here as long as GCC supports it
MDCCFLAGS  = -m68000 -mshort -Wall -Wextra -pedantic -std=c99 -ffreestanding
MDCXXFLAGS = -m68000 -mshort -Wall -Wextra -pedantic -std=c++17 -ffreestanding
SHCCFLAGS  = -m2 -mb -Wall -Wextra -pedantic -std=c99 -ffreestanding
SHCXXFLAGS = -m2 -mb -Wall -Wextra -pedantic -std=c++17 -ffreestanding

# Assembler flags
MDASFLAGS  = -x assembler-with-cpp -Imd_src -m68000 -Wa,--register-prefix-optional
SHASFLAGS  = -Ish_src --small

# Linker flags
MDLDFLAGS  = -T md_src/md.ld -nostdlib
SHLDFLAGS  = -T sh_src/mars.ld -nostdlib

# Extra options set by debug or release target
MDEXTRA = 
SHEXTRA = 

# Generate m68k object target list
MDSS    = $(wildcard md_src/*.s)
MDCS    = $(wildcard md_src/*.c)
MDCPPS  = $(wildcard md_src/*.cpp)
MDOBJS  = $(MDSS:.s=.o)
MDOBJS += $(MDCS:.c=.o)
MDOBJS += $(MDCPPS:.cpp=.o)

# Generate sh object target list
SHSS    = $(wildcard sh_src/*.s)
# custom_maps.c is generated (and may not exist at glob time on a clean build),
# so exclude it from the wildcard and add its object explicitly below.
SHCS    = $(filter-out sh_src/custom_maps.c,$(wildcard sh_src/*.c))
SHCPPS  = $(wildcard sh_src/*.cpp)
SHOBJS  = $(SHSS:.s=.o)
SHOBJS += $(SHCS:.c=.o)
SHOBJS += sh_src/custom_maps.o
SHOBJS += $(SHCPPS:.cpp=.o)

# Speex narrowband decoder strip (third-party, sh_src/speex/): UNLINKED.
# The port works (host-verified, played on hardware) but one 20 ms frame
# cost ~8 ms to decode on the cache-starved SH-2 (DT:1416, B00253) — the
# hello ships as IMA ADPCM instead (sound.c). Sources stay as reference;
# uncomment these two lines (and the rules below) to relink it.
# SPXCS   = $(wildcard sh_src/speex/*.c)
# SHOBJS += $(SPXCS:.c=.o)

.PHONY: all release debug deploy deploy-tv publish lint procgen-test community author FORCE

# Override on command line: make deploy MISTER=root@othermister.local
# Both targets probe usb0 then usb1 over ssh before scp'ing, so USB
# drive renumber doesn't break the push.
#
# The probe checks /proc/mounts as well as the directory. /media/usbN stays
# behind as an EMPTY directory after a drive ejects, so a bare `[ -d ]` test
# passes on a dead mount and the ROM copies into nothing -- silently, with a
# success exit code. Seen for real on the TV MiSTer: the drive threw I/O errors
# mid-write ("[EXFAT] unmounted with media errors"), a 1.6 MB scp crawled to
# 2m49s against a failing link, and the target directory then vanished.
# /proc/mounts is used rather than the `mountpoint` binary so this does not
# depend on what a given MiSTer image ships.
MISTER     ?= root@mister.office.local
MISTER_TV  ?= root@mister.tv.local

all: release

release: MDEXTRA  = -O2 -fomit-frame-pointer -flto -fuse-linker-plugin
# -fno-thread-jumps: sh-elf GCC 15.2.0 at O2+LTO jump-threads the main-loop
# control flow so SHARED_UC->frame_count++ (and the ULTRA gate tail) is
# bypassed -- the frame clock pins at the fade-in count and every
# frame-paced effect freezes (first seen as the PVM static becoming a
# photograph). Bisected 2026-08-21 with ares-headless: -O1 ticks, -O2
# freezes, -O2 -fno-thread-jumps ticks. CI's pinned marsdev (v1.0.0-rc1)
# does not need it but is not hurt by it; keep until a local toolchain
# proves clean without it.
release: SHEXTRA  = -O2 -fomit-frame-pointer -flto -fuse-linker-plugin -fno-thread-jumps
release: $(MDTARGET).bin $(MDTARGET).lst $(TARGET).32x $(TARGET).lst

# The community ROM (everything contributors have sent in) and a single
# contributor's personal ROM. Both re-enter make with a different PROFILE, so
# the codegen + ROM name follow automatically.
community:
	@$(MAKE) --no-print-directory PROFILE=community release
author:
	@test -n "$(AUTHOR)" || { echo "usage: make author AUTHOR=<handle>   (the map's author:, folded through registry author_aliases)"; exit 1; }
	@$(MAKE) --no-print-directory PROFILE=author:$(AUTHOR) release

# Office MiSTer: probe usb0 then usb1 for the S32X dir.
deploy: release
	@DIR=$$(ssh $(MISTER) 'for n in 0 1; do d=/media/usb$$n/Games/S32X; grep -q " /media/usb$$n " /proc/mounts && [ -d "$$d" ] && echo "$$d" && exit 0; done; exit 1') && \
		echo "==> Copying $(TARGET).32x to $(MISTER):$$DIR/" && \
		scp $(TARGET).32x $(MISTER):$$DIR/backrooms.32x

# TV MiSTer: probe usb0 first (typical layout after a clean boot), fall
# back to usb1 if the drives renumbered. Avoids the manual `find` dance
# the office target's comment describes.
deploy-tv: release
	@DIR=$$(ssh $(MISTER_TV) 'for n in 0 1; do d=/media/usb$$n/Games/S32X; grep -q " /media/usb$$n " /proc/mounts && [ -d "$$d" ] && echo "$$d" && exit 0; done; exit 1') && \
		echo "==> Copying $(TARGET).32x to $(MISTER_TV):$$DIR/" && \
		scp $(TARGET).32x $(MISTER_TV):$$DIR/backrooms.32x

# Deploy an ALREADY-BUILT rom to a MiSTer without rebuilding — the point is to
# put exact bytes there (e.g. a ROM fetched from a GitHub release by
# ./fetch-release.sh). `deploy` depends on `release`, so it would rebuild and
# defeat that: CI pins marsdev 13.1.0, this machine may not, so a local rebuild
# is NOT the released artifact.
#   make deploy-rom ROM=rom/release/backrooms-build-138.32x
deploy-rom:
	@test -n "$(ROM)" || { echo "usage: make deploy-rom ROM=<path to .32x> [MISTER=host]"; exit 1; }
	@test -f "$(ROM)" || { echo "error: $(ROM) not found"; exit 1; }
	@DIR=$$(ssh $(MISTER) 'for n in 0 1; do d=/media/usb$$n/Games/S32X; grep -q " /media/usb$$n " /proc/mounts && [ -d "$$d" ] && echo "$$d" && exit 0; done; exit 1') && \
		echo "==> Copying $(ROM) to $(MISTER):$$DIR/" && \
		scp "$(ROM)" $(MISTER):$$DIR/backrooms.32x

deploy-rom-tv:
	@$(MAKE) deploy-rom ROM="$(ROM)" MISTER=$(MISTER_TV)

# Build + publish a GitHub Release (ROM as the asset, commit log as notes).
# Git-derived tag build-<commit-count>. Needs the gh CLI, authenticated.
# Use 'make publish ARGS=--dry-run' to preview without building/publishing.
publish:
	@scripts/release.sh $(ARGS)

# Gens-KMod, BlastEm and UMDK support GDB tracing, enabled by this target
debug: MDEXTRA = -g -Og -DDEBUG -DKDEBUG
debug: SHEXTRA = -g -Og -DDEBUG -DKDEBUG
debug: $(MDTARGET).bin $(MDTARGET).lst $(TARGET).32x $(TARGET).lst

# Symbol listings for both CPUs
$(MDTARGET).lst: $(MDTARGET).elf
	$(MDNM) --plugin=$(MDPLUGIN) -n $< > $@

$(TARGET).lst: $(TARGET).elf
	$(SHNM) --plugin=$(SHPLUGIN) -n $< > $@

# m68k stuff

$(MDTARGET).bin: $(MDTARGET).elf
	@echo "Stripping ELF header from M68K program"
	@$(MDOBJC) -O binary $< $@

$(MDTARGET).elf: $(MDOBJS) | $(ROMDIR)
	$(MDCC) $(MDLDFLAGS) $^ -o $@ $(MDLIBS)

md_src/%.o: md_src/%.s
	@echo "MDAS $<"
	@$(MDCC) $(MDASFLAGS) -c $< -o $@

md_src/%.o: md_src/%.c
	@echo "MDCC $<"
	@$(MDCC) $(MDCCFLAGS) $(MDEXTRA) $(MDINCS) -MMD -MP -c $< -o $@

md_src/%.o: md_src/%.cpp
	@echo "MDCXX $<"
	@$(MDCXX) $(MDCXXFLAGS) $(MDEXTRA) $(MDINCS) -MMD -MP -c $< -o $@

# sh2 stuff

$(TARGET).32x: $(TARGET).elf
	@echo "Stripping ELF header from SH-2 program"
	@$(SHOBJC) -O binary $< temp.32x
	@dd if=temp.32x of=$@ bs=8192 conv=sync
	@rm -f temp.32x

$(TARGET).elf: $(SHOBJS) | $(ROMDIR)
	$(SHCC) $(SHLDFLAGS) $^ -o $@ $(SHLIBS)

$(ROMDIR):
	@mkdir -p $(ROMDIR)

# sh_src/mars_start.s embeds the assembled 68000 boot blob via
# `.incbin "md_start.bin"`, which the assembler resolves on -Ish_src. Stage it
# there from the MD build output and make the SH startup depend on it, so a clean
# build (CI, fresh checkout) orders the MD side first instead of relying on a
# stale leftover copy.
sh_src/md_start.bin: $(MDTARGET).bin
	@cp $< $@
sh_src/mars_start.o: sh_src/md_start.bin

# Speex rule MUST precede the generic sh_src/%.o rules: GNU make 3.81
# resolves pattern-rule collisions by definition order (shortest-stem
# came in 3.82), and macOS ships 3.81. Speex objects get config.h via
# -DHAVE_CONFIG_H, the freestanding shim headers via -Ish_src/speex,
# and -w (upstream code, not held to the project warning bar).
sh_src/speex/%.o: sh_src/speex/%.c
	@echo "SHCC $<"
	@$(SHCC) $(SHCCFLAGS) $(SHEXTRA) $(SHINCS) -w -DHAVE_CONFIG_H -Ish_src/speex -MMD -MP -c $< -o $@

# libmem defines memcpy/memmove/memset for the -nostdlib link (gcc emits
# builtin mem* calls late). It must be a REAL object, not LTO bytecode:
# whole-program analysis discards "unused" IR definitions before those
# calls appear, leaving them unresolvable. -fno-builtin also stops gcc
# rewriting the byte loops into calls to the very functions they
# implement. (The unlinked speex glue needs the same treatment if the
# strip is ever relinked.)
sh_src/libmem.o: sh_src/libmem.c
	@echo "SHCC $< (no-lto)"
	@$(SHCC) $(SHCCFLAGS) $(SHEXTRA) $(SHINCS) -fno-lto -fno-builtin -MMD -MP -c $< -o $@

sh_src/%.o: sh_src/%.s
	@echo "SHAS $<"
	@$(SHAS) $(SHASFLAGS) $< -o $@

sh_src/%.o: sh_src/%.c
	@echo "SHCC $<"
	@$(SHCC) $(SHCCFLAGS) $(SHEXTRA) $(SHINCS) -MMD -MP -c $< -o $@

sh_src/%.o: sh_src/%.cpp
	@echo "SHCXX $<"
	@$(SHCXX) $(SHCXXFLAGS) $(SHEXTRA) $(SHINCS) -MMD -MP -c $< -o $@

# --- Build version stamp -------------------------------------------------
# sh_src/version.h is regenerated every build from git + the clock, but only
# rewritten when its contents actually change (commit count / sha / date) —
# so an unchanged same-day rebuild stays byte-stable and doesn't needlessly
# recompile menu.o. Generated, not tracked (see .gitignore).
.PHONY: FORCE
FORCE:

sh_src/version.h: FORCE
	@BUILD=$$(git rev-list --count HEAD 2>/dev/null || echo 0); \
	SHA=$$(git rev-parse --short HEAD 2>/dev/null || echo nogit); \
	DATE=$$(date +%Y%m%d); \
	printf '/* AUTO-GENERATED by the Makefile each build. Do not edit; not tracked. */\n#ifndef VERSION_H\n#define VERSION_H\n#define VERSION_BUILD_STR "%05d"\n#define VERSION_DATE_STR  "%s"\n#define VERSION_SHA_STR   "%s"\n#endif\n' "$$BUILD" "$$DATE" "$$SHA" > sh_src/version.h.tmp; \
	if ! cmp -s sh_src/version.h.tmp sh_src/version.h 2>/dev/null; then \
		mv sh_src/version.h.tmp sh_src/version.h; \
		echo "  VERSION  build $$BUILD ($$SHA) $$DATE"; \
	else rm -f sh_src/version.h.tmp; fi

# menu.c and m_main.c both draw the version strings (menu screen + debug HUD),
# so they must see a fresh version.h. This is also the ORDER dependency that
# makes a clean checkout work: version.h is generated (not tracked), so without
# these prerequisites make would compile these TUs before the rule fires — CI
# has no stale copy to mask it (build-129 release failed exactly this way).
sh_src/menu.o sh_src/m_main.o: sh_src/version.h

# sh_src/custom_maps.c is codegen'd from maps/*.map + registry.json by the level
# editor's generator. Regenerate when a .map, the registry, or the generator
# changes; the sh_src/*.c wildcard then compiles it like any source. Tracked
# (unlike version.h) so the parse-time wildcard sees it on a clean checkout; the
# generator only rewrites the file when its contents actually change.
# Maps live in TIER folders (maps/core/, maps/test/, maps/curated/,
# maps/community/); gen_maps globs them recursively, keeps the ones this
# PROFILE ships, and lints maps+assets before emitting (a bad map fails the
# build).
sh_src/custom_maps.c: $(wildcard maps/*.map maps/core/*.map maps/test/*.map \
                                 maps/curated/*.map maps/community/*.map) \
                      registry.json tools/gen_maps.py tools/mapfmt.py tools/lint_maps.py \
                      $(PROFILE_STAMP)
	@python3 tools/gen_maps.py --profile $(PROFILE)

# Remembers which profile the generated files were built for: rewritten (and so
# newer than the codegen output) only when you ask for a DIFFERENT profile, so
# `make community` after `make` regenerates, and a plain rebuild does not.
$(PROFILE_STAMP): FORCE | $(ROMDIR)
	@[ "$$(cat $@ 2>/dev/null)" = "$(PROFILE)" ] || printf '%s' "$(PROFILE)" > $@
FORCE:

# sh_src/sprite_defs.h — the data-driven sprite table, codegen'd from
# registry.json "assets" + the referenced _tex.h (tools/gen_assets.py). raycast.c
# includes it; generated + gitignored, so the explicit raycast.o dep below makes a
# clean build emit it first (same pattern as md_start.bin).
sh_src/sprite_defs.h: registry.json tools/gen_assets.py $(wildcard sh_src/*_tex.h) \
                      $(PROFILE_STAMP)
	@python3 tools/gen_assets.py --profile $(PROFILE)
sh_src/raycast.o: sh_src/sprite_defs.h

# sh_src/sms_font.h — the MD boot font as 1bpp rows, codegen'd from md_src/font.s
# (tools/gen_sms_font.py). The SH-2's SMS32X picture and the Genesis VDP must
# draw TILEBUF through the SAME glyphs or the zoom handoff pops; one source,
# not two that agree today. Generated + gitignored, explicit m_main.o dep so a
# clean build emits it first (same pattern as sprite_defs.h).
sh_src/sms_font.h: md_src/font.s tools/gen_sms_font.py
	@python3 tools/gen_sms_font.py
sh_src/m_main.o: sh_src/sms_font.h

# sh_src/sms_tiles.h — the maze mini-game's 4bpp art tiles + picture palette,
# codegen'd from sms/tileset.json (authored in tools/tile-editor.html). The
# same run regenerates sms/games/maze/tiles.inc, the Z80-side metatile table:
# one source feeds both halves of the display duet. CHECKED IN (not
# gitignored): the tileset is authored content, like registry.json.
sh_src/sms_tiles.h: sms/tileset.json tools/gen_sms_tiles.py
	@python3 tools/gen_sms_tiles.py
sh_src/m_main.o: sh_src/sms_tiles.h

# Standalone gate (maps + assets + registry), no toolchain — used by CI.
lint:
	@python3 tools/lint_maps.py

# Procgen invariant harness: compiles the REAL generator for the host and
# runs it over thousands of seeds, checking the promises procgen.c makes in
# its own comments (console present, at least one neanderthal, a crawlspace,
# full reachability from spawn, no asset in a wall or sharing a cell). Needs
# no console toolchain. Found three shipped-broken promises the day it was
# written, including a set piece that had never appeared in any generated
# level. Run it after touching any placement rule.
procgen-test:
	@cc -Ish_src -o /tmp/pgtest tools/test_procgen.c sh_src/procgen.c
	@/tmp/pgtest 5000

# Shadow-VDP harness: the REAL mode-4 renderer (sh_src/smsvdp.c) driven on
# the host exactly the way SMS code drives the silicon — port writes only —
# with pixel assertions and a PPM test card for eyes. Run after touching
# smsvdp.c, before any ROM wiring. Same pattern as procgen-test.
vdp-test:
	@cc -Ish_src -o /tmp/vdptest tools/test_smsvdp.c sh_src/smsvdp.c
	@/tmp/vdptest /tmp/vdp_testcard.ppm

# Auto-generated header dependency files. -MMD emits one per .c next to
# the .o; -include silently ignores them on a clean tree. Without this,
# header changes don't trigger rebuilds and you ship stale .o files
# compiled against an outdated struct layout — which is exactly how
# we corrupted memory during the SH-2 split work.
-include $(MDOBJS:.o=.d)
-include $(SHOBJS:.o=.d)

.PHONY: clean

clean:
	rm -f $(MDOBJS) $(SHOBJS)
	rm -f $(MDOBJS:.o=.d) $(SHOBJS:.o=.d)
	rm -f $(MDTARGET).bin $(MDTARGET).elf $(MDTARGET).lst
	rm -f $(TARGET).32x $(TARGET).elf $(TARGET).lst
	rm -f m68k_crt0.bin.o m68k_crt0.bin
	rm -f sh_src/md_start.bin
	rm -f sh_src/sprite_defs.h
	rm -f sh_src/version.h sh_src/version.h.tmp
	rm -f $(PROFILE_STAMP)
