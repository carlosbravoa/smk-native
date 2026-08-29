/* The item word - the roulette, the hold, the use.
 *
 * DECODED from $81:B387 (per frame), $81:B34A (the box) and $81:B698 /
 * $81:B6D1 / $81:B6F2 (which item), and MEASURED against the user's two
 * recorded races frame by frame (tools/labs/mame/roulette_flag.csv is the
 * gate).  docs/ITEMS.md has the whole picture.
 */
#include "smk.h"
#include <string.h>

#define T_SEQ_PTRS   0x81B491u   /* 6 pointers to the sequences (7th at $B4DB) */
#define T_SEQ_BATTLE 0x81B4DBu
#define T_BLOCK_PTRS 0x81B471u   /* 8 pointers, by $0D28                        */
#define T_REC_INDEX  0x81B666u   /* 40 bytes: record offset by lap*8 + rank     */
#define T_TRACK_0D28 0x818B73u   /* 20 bytes: $0D28 by track                    */

static uint8_t rd8(const smk_rom *rom, uint32_t snes)
{
    uint32_t pc = smk_snes_to_pc(rom, snes);
    return pc < rom->size ? rom->data[pc] : 0;
}
static uint16_t rd16(const smk_rom *rom, uint32_t snes)
{
    return (uint16_t)(rd8(rom, snes) | rd8(rom, snes + 1) << 8);
}

bool smk_items_load(const smk_rom *rom, smk_itemtab *t)
{
    memset(t, 0, sizeof *t);
    for (int s = 0; s < SMK_ITEM_SEQS; s++) {
        uint32_t a = s < 6 ? (0x810000u | rd16(rom, T_SEQ_PTRS + (uint32_t)s * 2u))
                           : T_SEQ_BATTLE;
        int n = 0;
        /* ids until the $80 marker, which is followed by the loop pointer */
        while (n < 11 && rd8(rom, a + (uint32_t)n) < 0x80) { t->seq[s][n] = rd8(rom, a + (uint32_t)n); n++; }
        t->seq[s][n] = 0xFF;
        t->seq_len[s] = (uint8_t)n;
        if (n == 0) return false;
    }
    for (int b = 0; b < SMK_ITEM_BLOCKS; b++) {
        uint32_t a = 0x810000u | rd16(rom, T_BLOCK_PTRS + (uint32_t)b * 2u);
        for (int i = 0; i < 27; i++) t->block[b][i] = rd8(rom, a + (uint32_t)i);
    }
    for (int i = 0; i < 40; i++) t->rec_by_lap_rank[i] = rd8(rom, T_REC_INDEX + (uint32_t)i);
    for (int i = 0; i < 20; i++) t->block_of_track[i] = (uint8_t)(rd8(rom, T_TRACK_0D28 + (uint32_t)i) >> 1);
    t->ok = true;
    return true;
}

/* $81:B6D1 (GP): the record by lap and rank, then $81:B6F2: the roll. */
void smk_item_box(smk_item *it, const smk_itemtab *t, int track, int lap,
                  int rank, unsigned roll)
{
    memset(it, 0, sizeof *it);
    /* $C1 - $80, clamped at 0: the port's lap 1 (first crossing done) is $80 */
    int li = lap - 1;
    if (li < 0) li = 0;
    if (li > 4) li = 4;
    if (rank < 0) rank = 0;
    if (rank > 7) rank = 7;
    int blk = (track >= 0 && track < 20) ? t->block_of_track[track] : 1;
    if (blk >= SMK_ITEM_BLOCKS) blk = SMK_ITEM_BLOCKS - 1;
    const uint8_t *rec = t->block[blk] + t->rec_by_lap_rank[li * 8 + rank];
    it->seq = rec[8] & 0x0F;
    if (it->seq >= SMK_ITEM_SEQS) it->seq = 0;
    /* first threshold the roll is under; the meta byte (>= $80) always is */
    int y = 0;
    roll &= 0x1F;
    while (y < 8 && roll >= rec[y]) y++;
    it->target = y;                          /* $81:B49D[y] == y */
    it->word   = 0xA000;                     /* $81:B35C */
    it->timer  = SMK_ITEM_ROULETTE;          /* $81:B362 */
    it->cursor = 0;
}

static void show(smk_item *it, const uint8_t *seq, int len)
{
    if (it->cursor >= len) it->cursor = 0;   /* the $80 loop pointer */
    it->word = (uint16_t)((it->word & 0xFF00) | seq[it->cursor]);
}

int smk_item_step(smk_item *it, bool button, bool can_use)
{
    if (!(it->word & 0x8000)) return -1;                        /* $81:B38C */
    const smk_itemtab *t = smk_item_tables;
    const uint8_t *seq = t ? t->seq[it->seq] : (const uint8_t *)"\0";
    int len = t ? t->seq_len[it->seq] : 1;

    if (it->word & 0x2000) {                                    /* $81:B3B8: spinning */
        it->timer--;
        if (it->timer >= 0 && button) it->timer |= (int16_t)0xFFF0;   /* $81:B3C6 */
        if (it->timer < 0) {
            /* $81:B3CC: past -64 stop regardless; else stop on the target */
            if (it->timer < -64 || (it->word & 0xFF) == it->target) {
                it->word &= (uint16_t)~0x2000u;                 /* $81:B3DD */
                it->timer = SMK_ITEM_HOLD;
                return -1;
            }
        }
        if ((it->timer & 3) == 0) { it->cursor++; show(it, seq, len); }   /* $81:B3EE */
        return -1;
    }
    if (it->word & 0x4000) {                                    /* $81:B3FB: ready */
        if (button && can_use) {
            int id = it->word & 0xFF;
            it->word = 0;                                       /* $81:B413 */
            return id;
        }
        return -1;
    }
    if (it->word & 0x1000) {                                    /* $81:B369: empty flash */
        if (it->timer == 0) { it->word = 0; return -1; }
        it->timer--;
        return -1;
    }
    /* $81:B39D: held and blinking */
    if (it->timer == 0) { it->word |= 0x4000; return -1; }      /* $81:B3AF */
    it->timer--;
    return -1;
}

const smk_itemtab *smk_item_tables;


/* ---- the icons (smk.h) ------------------------------------------------- */
#define T_ICON_TILES 0x81B320u   /* 9 x (tile, attr); the blank is at +$12 */

bool smk_itemicons_load(const smk_rom *rom, smk_itemicons *out)
{
    static uint8_t buf[8192];
    memset(out, 0, sizeof *out);
    long n = smk_decompress_into(rom->data, rom->size,
                                 smk_snes_to_pc(rom, SMK_ICON_SRC),
                                 buf, sizeof buf, 0, NULL);
    if (n < SMK_ICON_TILES * 16) return false;
    for (int t = 0; t < SMK_ICON_TILES; t++) {
        const uint8_t *src = buf + t * 16;
        for (int y = 0; y < 8; y++) {
            uint8_t lo = src[y * 2], hi = src[y * 2 + 1];
            for (int x = 0; x < 8; x++) {
                int bit = 7 - x;
                out->px[t][y * 8 + x] = (uint8_t)(((lo >> bit) & 1) | (((hi >> bit) & 1) << 1));
            }
        }
    }
    for (int i = 0; i < SMK_ITEMS; i++) {
        out->tile[i] = rd8(rom, T_ICON_TILES + (uint32_t)i * 2u);
        out->pal[i]  = (uint8_t)((rd8(rom, T_ICON_TILES + (uint32_t)i * 2u + 1u) >> 2) & 7);
    }
    out->tile[SMK_ITEMS]     = rd8(rom, T_ICON_TILES + 0x12u);          /* the blink blank */
    out->pal[SMK_ITEMS]      = (uint8_t)((rd8(rom, T_ICON_TILES + 0x13u) >> 2) & 7);
    out->tile[SMK_ITEMS + 1] = 0xE8;                                    /* $81:B433: the empty box, $E9 over $E8 */
    out->pal[SMK_ITEMS + 1]  = (0x3C >> 2) & 7;
    out->ok = true;
    return true;
}
