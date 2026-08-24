/* Per-track course data, read from the ROM at runtime.
 *
 * Mirrors the game's own loader at $81FBC0-$81FEB5: a record stream paints
 * a 64x64 sector map (one sector per record, seven paint shapes), a second
 * stream provides one racing-line waypoint per sector, and a parameter
 * table adds the finish-line rectangle.  The Python twin in
 * tools/smktool/course.py is verified byte-exact against the running game;
 * the selftest keeps the two implementations agreeing.
 */
#include "smk.h"
#include <string.h>

#define TBL_RECORDS   0x81FF9Bu
#define TBL_WAYPOINTS 0x81FFCBu
#define TBL_PARAMS    0x8180D4u
#define DATA_BANK     0xC60000u

static uint32_t stream_pc(const smk_rom *rom, uint32_t table, int track)
{
    uint32_t p = smk_snes_to_pc(rom, table) + (uint32_t)track * 2u;
    uint32_t addr = (uint32_t)rom->data[p] | ((uint32_t)rom->data[p + 1] << 8);
    return smk_snes_to_pc(rom, DATA_BANK | addr);
}

bool smk_course_load(const smk_rom *rom, int track, smk_course *out)
{
    if (track < 0 || track >= SMK_TRACK_COUNT) return false;
    memset(out, 0, sizeof *out);

    /* --- sector records ---------------------------------------------- */
    uint32_t p = stream_pc(rom, TBL_RECORDS, track);
    int sector = 0;
    for (int guard = 0; guard < 1024; guard++) {
        uint8_t t = rom->data[p];
        if (t == 0xFF) break;
        unsigned pos = rom->data[p + 1] | ((unsigned)rom->data[p + 2] << 6);
        p += 3;
        #define PAINT(i) (out->map[(unsigned)(i) & (SMK_SECT_CELLS - 1)] = (uint8_t)sector)
        if (t == 0) {
            unsigned w = rom->data[p], h = rom->data[p + 1]; p += 2;
            for (unsigned row = 0; row < h; row++)
                for (unsigned i = 0; i < w; i++)
                    PAINT(pos + row * SMK_SECT_W + i);
        } else if (t == 2 || t == 4 || t == 6 || t == 8) {
            int n = rom->data[p]; p += 1;
            int xstep = (t == 2 || t == 8) ? 1 : -1;
            int ystep = (t == 2 || t == 4) ? SMK_SECT_W : -SMK_SECT_W;
            int base = (int)pos;
            while (n > 0) {
                for (int i = 0; i < n; i++) PAINT(base + i * xstep);
                base += ystep;
                n--;
            }
        } else if (t == 10 || t == 12) {
            int h = rom->data[p + 1]; p += 2;         /* first byte unused */
            int step = (t == 10) ? 63 : 65;
            int base = (int)pos;
            while (h > 0) {
                for (int i = 0; i < h; i++) PAINT(base + i * SMK_SECT_W);
                base += step;
                h--;
            }
        } else {
            return false;                              /* unknown record */
        }
        #undef PAINT
        if (++sector >= SMK_MAX_SECTORS) return false;
    }
    out->sectors = sector;
    if (sector == 0) return false;

    /* --- racing line -------------------------------------------------- */
    p = stream_pc(rom, TBL_WAYPOINTS, track);
    for (int i = 0; i < sector; i++) {
        out->wx[i] = (uint16_t)(rom->data[p] * 8);
        out->wy[i] = (uint16_t)(rom->data[p + 1] * 8);
        out->wattr[i] = rom->data[p + 2];
        p += 3;
    }
    out->wx[sector] = out->wx[0];                      /* close the loop */
    out->wy[sector] = out->wy[0];

    /* --- finish-line rectangle ---------------------------------------- */
    uint32_t q = smk_snes_to_pc(rom, TBL_PARAMS) + (uint32_t)track * 6u;
    out->lap_word = (uint16_t)(rom->data[q] | rom->data[q + 1] << 8);
    unsigned cell = rom->data[q + 2] | (unsigned)rom->data[q + 3] << 8;
    unsigned w = rom->data[q + 4], h = rom->data[q + 5];
    for (unsigned row = 0; row < h; row++)
        for (unsigned i = 0; i < w; i++)
            out->map[(cell + row * SMK_SECT_W + i) & (SMK_SECT_CELLS - 1)] |= SMK_SECT_FINISH;
    return true;
}
