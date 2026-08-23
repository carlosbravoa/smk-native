#include "smk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

uint32_t smk_snes_to_pc(const smk_rom *rom, uint32_t snes)
{
    /* HiROM.  Mirrors collapse onto the same bytes, which is deliberate:
     * the game really does call into several aliases of one routine. */
    uint32_t bank = (snes >> 16) & 0x3Fu;
    return ((bank << 16) | (snes & 0xFFFFu)) & (uint32_t)(rom->size - 1);
}

bool smk_rom_load(smk_rom *rom, const char *path, char *err, size_t errsz)
{
    memset(rom, 0, sizeof *rom);

    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, errsz, "cannot open '%s'", path); return false; }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); snprintf(err, errsz, "'%s' is empty", path); return false; }

    /* A 512-byte copier header is common on SNES dumps; skip it. */
    long skip = (len % 1024 == 512) ? 512 : 0;
    len -= skip;
    fseek(f, skip, SEEK_SET);

    if (len != (long)SMK_ROM_SIZE) {
        fclose(f);
        snprintf(err, errsz,
                 "'%s' is %ld bytes; Super Mario Kart is %u (headerless)",
                 path, len, SMK_ROM_SIZE);
        return false;
    }

    rom->data = malloc((size_t)len);
    if (!rom->data) { fclose(f); snprintf(err, errsz, "out of memory"); return false; }
    if (fread(rom->data, 1, (size_t)len, f) != (size_t)len) {
        fclose(f); free(rom->data); rom->data = NULL;
        snprintf(err, errsz, "short read on '%s'", path);
        return false;
    }
    fclose(f);
    rom->size = (size_t)len;

    /* HiROM header at $FFC0. */
    const uint8_t *h = rom->data + 0xFFC0;
    memcpy(rom->title, h, 21);
    rom->title[21] = 0;
    for (int i = 20; i >= 0 && rom->title[i] == ' '; i--) rom->title[i] = 0;

    /* Checksum pair must be complementary, and the sum must match. */
    uint16_t comp = rd16(h + 0x1C), sum = rd16(h + 0x1E);
    uint32_t calc = 0;
    for (size_t i = 0; i < rom->size; i++) calc += rom->data[i];
    /* the stored pair contributes $FFFF + $0000 to the real sum */
    calc = (calc - comp - (comp >> 8) - sum - (sum >> 8)) & 0xFFFF;
    calc = 0; /* recompute cleanly with the fields neutralised */
    for (size_t i = 0; i < rom->size; i++) {
        if (i >= 0xFFDC && i < 0xFFE0) continue;
        calc += rom->data[i];
    }
    calc = (calc + 0xFF + 0xFF) & 0xFFFF;

    rom->recognised = ((comp ^ sum) == 0xFFFF) && (calc == sum)
                      && strncmp(rom->title, "SUPER MARIO KART", 16) == 0;

    if (!rom->recognised)
        snprintf(err, errsz,
                 "'%s' does not look like Super Mario Kart (USA) "
                 "(title \"%s\", checksum %04X/%04X)", path, rom->title, sum, comp);
    return true;   /* loaded; caller decides whether to insist on recognised */
}

void smk_rom_free(smk_rom *rom)
{
    free(rom->data);
    rom->data = NULL;
    rom->size = 0;
}
