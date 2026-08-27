# Super Mario Kart - native reimplementation + the RE toolkit behind it
#
# Supply your own Super Mario Kart (USA) ROM at rom/smk_usa.sfc.
# No ROM and no game data is distributed with this project.

PY      := python3
SMK     := ./tools/smk
BASE    := rom/smk_usa.sfc
NATIVE  := build-native
GAME    := $(NATIVE)/smk
ASAR    := vendor/asar-build/asar/bin/asar

.PHONY: all game run bench shots selftest test verify info trace dis \
        jumptables health extract roundtrip romhack tools clean distclean help

all: game

## build the native SDL game
game: $(NATIVE)/Makefile
	@cmake --build $(NATIVE) -j$$(nproc)

$(NATIVE)/Makefile:
	@cmake -S . -B $(NATIVE) -DCMAKE_BUILD_TYPE=Release

## build and play
run: game $(BASE)
	@$(GAME)

## headless render benchmark
bench: game $(BASE)
	@SDL_VIDEODRIVER=dummy $(GAME) --width 1920 --height 1080 --pixel 1 --frames 300

## render a still from every track to assets/extracted/
shots: game $(BASE)
	@mkdir -p assets/extracted
	@for t in $$(seq 0 23); do \
	   SDL_VIDEODRIVER=dummy $(GAME) --track $$t --width 512 --height 448 \
	     --pixel 2 --shot assets/extracted/shot_$$t.bmp >/dev/null; done
	@echo "wrote assets/extracted/shot_0..23.bmp"

## C-side verification of the asset pipeline
selftest: game $(BASE)
	@$(NATIVE)/smk_selftest $(BASE)

ailap: game $(BASE)
	@$(NATIVE)/smk_ailap $(BASE)

## verify the ported kinematics against the game running in the oracle (slow)
verify-physics: $(BASE)
	@$(PY) tools/verify_physics.py 120

## full Python regression suite (ROM identity, disassembly, codec)
test: $(ASAR) $(BASE)
	@$(PY) tools/test.py

## ---- reverse-engineering toolkit ----------------------------------------

## check the base ROM is the expected dump
verify: $(BASE)
	@$(SMK) verify

## header, vectors, cartridge info
info: $(BASE)
	@$(SMK) info

## tracing disassembler coverage report
trace: $(BASE)
	@$(SMK) trace --coverage-map

## annotated listing -> build/smk.asm
dis: $(BASE)
	@mkdir -p build && $(SMK) dis -o build/smk.asm

## discover indirect dispatch tables
jumptables: $(BASE)
	@$(SMK) jumptables

## trace-quality metrics
health: $(BASE)
	@$(SMK) health

## export every known compressed asset
extract: $(BASE)
	@$(SMK) assets export-all

## prove the disassembler round-trips byte-exactly
roundtrip: $(ASAR) $(BASE)
	@$(PY) tools/roundtrip.py

## build a patched ROM from romhack/ (the original ROM-hacking path)
romhack: $(ASAR) $(BASE)
	@$(PY) tools/build.py --asm romhack/src/main.asm

## build the vendored 65816 assembler
tools: $(ASAR)

$(ASAR):
	@echo ">> building asar"
	@test -d vendor/asar || git clone --depth 1 https://github.com/RPGHacker/asar.git vendor/asar
	@cmake -S vendor/asar/src -B vendor/asar-build -DCMAKE_BUILD_TYPE=Release >/dev/null
	@cmake --build vendor/asar-build -j$$(nproc) >/dev/null

$(BASE):
	@echo "ERROR: $(BASE) not found."
	@echo "Supply your own Super Mario Kart (USA) ROM"
	@echo "(sha1 47e103d8398cf5b7cbb42b95df3a3c270691163b)."
	@exit 1

clean:
	rm -rf build $(NATIVE)

distclean: clean
	rm -rf vendor/asar vendor/asar-build

help:
	@awk '/^## /{d=substr($$0,4); next} \
	      /^[a-z][a-z-]*:/{if(d!=""){split($$0,a,":"); printf "  %-12s %s\n", a[1], d; d=""}}' Makefile

## the one command that proves the tree: FULL build (fails loudly), both
## suites, and a headless smoke run of the actual game binary
check: $(BASE)
	@cmake --build build-native -j$$(nproc)
	@./build-native/smk_selftest rom/smk_usa.sfc | tail -1
	@./build-native/smk_ailap rom/smk_usa.sfc | tail -1
	@./build-native/smk_laptest rom/smk_usa.sfc | tail -1
	@./build-native/smk_demoreplay rom/smk_usa.sfc tools/labs/mame/demo_race.csv 1000 --gate | tail -1
	@./build-native/smk_demoreplay rom/smk_usa.sfc tools/labs/mame/demo_race.csv 1100 --gate | tail -1
	@./build-native/smk_demoreplay rom/smk_usa.sfc tools/labs/mame/demo_tt_track19.csv 1000 --gate | tail -1
	@./build-native/smk_demoreplay rom/smk_usa.sfc tools/labs/mame/crash_run.csv 1000 --gate --min 85 --resync 250 | tail -1
	@./build-native/smk_demoreplay rom/smk_usa.sfc tools/labs/mame/gv1_run.csv 1000 --gate --min 88 --resync 80 | tail -1
	@$(PY) tools/test.py | tail -1
	@SDL_VIDEODRIVER=dummy ./build-native/smk --frames 60 >/dev/null && echo "smoke: game binary runs"
