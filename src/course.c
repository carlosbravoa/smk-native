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
#include <math.h>
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

    /* Unpainted cells are $7F, NOT 0 - MEASURED against the game's own
     * $7F:5000 (NOTES 124): it holds 1412 cells at $7F and 78 at sector 0,
     * so a zero default silently turns every off-course cell into sector
     * 0.  That is what dropped a rescued kart back at the start line. */
    memset(out->map, SMK_SECT_OFF, sizeof out->map);

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
    out->fin_cell = (int)cell;
    out->fin_w = (int)w;
    out->fin_h = (int)h;

    /* --- track objects ($84F15D: $85:D000 + track*128) --------------- */
    {
        uint32_t p2 = smk_snes_to_pc(rom, 0x85D000u) + (uint32_t)track * 128u;
        out->nobj = 0;
        for (int i = 0; i < 42; i++) {
            uint8_t kind = rom->data[p2];
            unsigned pos = rom->data[p2 + 1] | (unsigned)rom->data[p2 + 2] << 8;
            if (pos == 0xFFFF) break;
            out->obj[out->nobj].kind = kind;
            out->obj[out->nobj].x = (uint16_t)((pos & 0x7F) * 8);
            out->obj[out->nobj].y = (uint16_t)(((pos >> 7) & 0x7F) * 8);
            out->nobj++;
            p2 += 3;
        }
    }

    /* --- the lap segment tables (NOTES 127) --------------------------
     * $818E7E/$818E8D fill $0D28 and $0D2C from two per-track byte
     * tables; $84DBD5 turns them into a threshold list, and $84DBFF
     * counts how many of its bytes the waypoint has passed. */
    {
        uint32_t t73 = smk_snes_to_pc(rom, 0x818B73u) + (uint32_t)track;
        uint32_t t8c = smk_snes_to_pc(rom, 0x818B8Cu) + (uint32_t)track;
        int d28 = rom->data[t73], d2c = rom->data[t8c];
        out->nseg = 0;
        uint32_t pp = smk_snes_to_pc(rom, 0x84DB83u) + (uint32_t)d28;
        unsigned set = rom->data[pp] | (unsigned)rom->data[pp + 1] << 8;
        if (set) {                       /* $84DBDF: 0 means no obstacles */
            uint32_t lp = smk_snes_to_pc(rom, 0x840000u | set) + (uint32_t)d2c;
            unsigned lst = rom->data[lp] | (unsigned)rom->data[lp + 1] << 8;
            if (lst) {
                uint32_t tp = smk_snes_to_pc(rom, 0x840000u | lst);
                for (int i = 0; i < 8; i++) {
                    uint8_t v = rom->data[tp + (uint32_t)i];
                    out->seg_thresh[out->nseg++] = v;
                    if (v == 0xFF) break;     /* the list's terminator */
                }
            }
        }
    }

    /* --- sprite obstacles ($84DC20: $85:C800 + track*64) ------------- */
    {
        uint32_t p3 = smk_snes_to_pc(rom, 0x85C800u) + (uint32_t)track * 64u;
        out->nent = 0;
        for (int i = 0; i < 32; i++) {
            unsigned wd = rom->data[p3] | (unsigned)rom->data[p3 + 1] << 8;
            if (wd == 0) break;
            out->ent[out->nent].kind = (uint8_t)(wd >> 14);
            out->ent[out->nent].x = (uint16_t)((wd & 0x7F) * 8 + 4);
            out->ent[out->nent].y = (uint16_t)(((wd >> 7) & 0x7F) * 8 + 4);
            out->nent++;
            p3 += 2;
        }
    }

    /* --- the AI direction field ($81FCFC) --------------------------- */
    for (int i = 0; i < SMK_SECT_CELLS; i++) {
        int s2 = out->map[i] & SMK_SECT_OFF;
        if (s2 == SMK_SECT_OFF || s2 >= sector)
            continue;                            /* $81FD08: only $7F */
        float cx = (float)((i & 63) * SMK_SECT_CELL_PX + 8);
        float cy = (float)((i >> 6) * SMK_SECT_CELL_PX + 8);
        float ang = atan2f((float)out->wx[s2] - cx, -((float)out->wy[s2] - cy));
        unsigned a16 = (unsigned)((int)(ang * 65536.0f / (2.0f * (float)M_PI))
                                  & 0xFFFF);
        out->flow[i] = (uint8_t)(((a16 + 0x80) >> 8) & 0xFF);
    }
    return true;
}


void smk_course_start(const smk_course *c, int slot,
                      float *x, float *y, uint16_t *heading)
{
    /* travel direction across the line: last waypoint toward the first */
    float dx = (float)c->wx[0] - (float)c->wx[c->sectors - 1];
    float dy = (float)c->wy[0] - (float)c->wy[c->sectors - 1];
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) { dx = 0.0f; dy = -1.0f; len = 1.0f; }
    dx /= len; dy /= len;
    float px = -dy, py = dx;                  /* perpendicular */

    float cx = ((float)(c->fin_cell % SMK_SECT_W) + c->fin_w * 0.5f)
               * SMK_SECT_CELL_PX;
    float cy = ((float)(c->fin_cell / SMK_SECT_W) + c->fin_h * 0.5f)
               * SMK_SECT_CELL_PX;

    /* two columns, rows going backward from the strip, like the game */
    float back = 24.0f + 24.0f * (float)(slot / 2);
    float side = (slot & 1) ? 14.0f : -14.0f;
    *x = cx - dx * back + px * side;
    *y = cy - dy * back + py * side;
    *heading = (uint16_t)(atan2f(dx, -dy) * 65536.0f / (2.0f * (float)M_PI));
}


/* $84DBFF: y walks the threshold list while the waypoint is still at or
 * past the entry, so y is the first index the waypoint falls short of.
 * $FF terminates, which is why the last segment always wins. */
int smk_course_segment(const smk_course *c, int waypoint)
{
    if (c->nseg == 0) return -1;
    int y = 0;
    while (y < c->nseg && waypoint >= (int)c->seg_thresh[y]) y++;
    return y;
}

/* $84DC17: when the segment changes the whole set is respawned from the
 * track's list at segment * 8 ($84DAC5), one word per live slot - two of
 * them in a one-player race, four in two ($819136). */
void smk_course_spawn(smk_course *c, int waypoint, bool two_player)
{
    int seg = smk_course_segment(c, waypoint);
    int want = two_player ? 4 : 2;
    if (seg < 0) { c->nlive = 0; c->seg = -1; return; }
    if (seg == c->seg && c->nlive == want) return;
    c->seg = seg;
    c->nlive = 0;
    int first = seg * 4;                     /* offset seg*8 bytes = 4 words */
    if (first >= c->nent) first = 0;         /* $84DC35: fall back to the start */
    for (int i = 0; i < want && first + i < c->nent; i++)
        c->live[c->nlive++] = first + i;
}
