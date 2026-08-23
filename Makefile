# Super Mario Kart - reverse engineering & rebuild
#
# Place your own Super Mario Kart (USA) ROM at rom/smk_usa.sfc.
# No ROM or game data is distributed with this project.

PY      := python3
SMK     := ./tools/smk
BASE    := rom/smk_usa.sfc
OUT     := build/smk.sfc
ASAR    := vendor/asar-build/asar/bin/asar

.PHONY: all build verify info trace dis roundtrip tools clean distclean help

all: build

## build the patched ROM
build: $(ASAR) $(BASE)
	@$(PY) tools/build.py

## check the base ROM is the expected dump
verify: $(BASE)
	@$(SMK) verify

## header, vectors, cartridge info
info: $(BASE)
	@$(SMK) info

## run the tracing disassembler and report coverage
trace: $(BASE)
	@$(SMK) trace --coverage-map

## full annotated listing -> build/smk.asm
dis: $(BASE)
	@mkdir -p build && $(SMK) dis -o build/smk.asm

## prove the disassembler round-trips byte-exactly
roundtrip: $(ASAR) $(BASE)
	@$(PY) tools/roundtrip.py

## build the vendored assembler
tools: $(ASAR)

$(ASAR):
	@echo ">> building asar"
	@test -d vendor/asar || git clone --depth 1 https://github.com/RPGHacker/asar.git vendor/asar
	@cmake -S vendor/asar/src -B vendor/asar-build -DCMAKE_BUILD_TYPE=Release >/dev/null
	@cmake --build vendor/asar-build -j$$(nproc) >/dev/null
	@echo ">> asar ready: $@"

$(BASE):
	@echo "ERROR: $(BASE) not found."
	@echo "Supply your own Super Mario Kart (USA) ROM (sha1 47e103d8398cf5b7cbb42b95df3a3c270691163b)."
	@exit 1

clean:
	rm -rf build

distclean: clean
	rm -rf vendor/asar vendor/asar-build

help:
	@grep -B1 -E '^[a-z-]+:' Makefile | grep -E '^##|^[a-z-]+:' | sed 's/^## /  /'
