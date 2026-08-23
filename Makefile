# Super Mario Kart - reverse engineering & rebuild
#
# Place your own Super Mario Kart (USA) ROM at rom/smk_usa.sfc.
# No ROM or game data is distributed with this project.

PY      := python3
SMK     := ./tools/smk
BASE    := rom/smk_usa.sfc
OUT     := build/smk.sfc
ASAR    := vendor/asar-build/asar/bin/asar

.PHONY: all build verify info trace dis jumptables health roundtrip test extract tools clean distclean help

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

## discover indirect dispatch tables (writes symbols/10_jumptables.sym)
jumptables: $(BASE)
	@$(SMK) jumptables

## trace-quality metrics
health: $(BASE)
	@$(SMK) health

## full regression suite (ROM identity, disassembly, codec, build)
test: $(ASAR) $(BASE)
	@$(PY) tools/test.py

## export every known compressed asset to assets/extracted/
extract: $(BASE)
	@$(SMK) assets export-all

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
	@awk '/^## /{d=substr($$0,4); next} \
	      /^[a-z][a-z-]*:/{if(d!=""){split($$0,a,":"); printf "  %-12s %s\n", a[1], d; d=""}}' Makefile
