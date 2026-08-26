/* Coins and item boxes, DECODED from the collector at $81B73B..$81B7D6
 * (NOTES 110).
 *
 * Every frame, for each human player's kart that is on the ground and not
 * under the game's control ($10 bit 5), the game looks at the CLASS of the
 * tilemap cell under the kart ($68,x, from cell $58,x):
 *
 *   $14  item box: if the player has no item running ($0D70,y >= 0), the
 *        roulette starts ($0D70 = $A000, $0D78 = $C1) and the box's 2x2
 *        stamp is rewritten with the "used box" tiles: the cell's tile id
 *        & 3 is the quadrant, $81B723 corrects to the top-left cell, and
 *        the tiles come from $81B72B at (tile & $0C) - $D0.. which are road
 *        class, so a used box is inert until the game respawns it.
 *   $1A  coin: $0E00,y += 1, WRAPPING at 100 (`cmp #$64 / bcc / lda #0`),
 *        the cell is rewritten with the theme's erase tile ($0D2A, from
 *        the table at $81:8BBD by theme) and $0FC0,y = 1 (the pickup
 *        sound).  It is the CELL that decides, not a radius.
 *
 * No coin is ever LOST through $0E00 in banks $80-$85 (all addressing
 * forms searched); the "hit while holding coins" code at $80D82D only
 * plays a sound.  LABELLED: coin loss on a hit, the box respawn timer and
 * the item roulette are not ported.
 */
#include "smk.h"

#define T_QUADRANT  0x81B723u   /* 4 words: cell offset to the box's top-left */
#define T_USED_BOX  0x81B72Bu   /* 16 bytes: the used-box tiles by variant     */
#define T_ERASE     0x818BBDu   /* per-theme coin erase tile                   */

static uint8_t rd8(const smk_rom *rom, uint32_t snes)
{
    uint32_t pc = smk_snes_to_pc(rom, snes);
    return pc < rom->size ? rom->data[pc] : 0;
}

bool smk_pickup_step(const smk_rom *rom, smk_track *t, smk_player *p,
                     const smk_kart *k, bool grounded)
{
    /* $1F,x as it was BEFORE this frame's jump update: the collector runs
     * after the position integration and before the kart update, so a
     * coin on the hop's launch cell still counts (demo frame 919) */
    if (!grounded) return false;
    int px = smk_kart_px(k->x) & (SMK_WORLD_PX - 1);
    int py = (smk_kart_px(k->y) - 1) & (SMK_WORLD_PX - 1);      /* $80FA62: y - 1 */
    int cell = (py >> 3) * 128 + (px >> 3);                       /* $58,x */
    uint8_t tile = t->map[cell];
    uint8_t cls = t->surface[tile];                               /* $68,x */
    if (cls == 0x1A) {
        p->coins = (p->coins + 1) % 100;
        t->map[cell] = rd8(rom, T_ERASE + (uint32_t)t->theme);
        return true;
    }
    if (cls == 0x14) {
        /* $81B75D: with an item running or held ($0D70,y negative) the
         * box is left alone.  The port has no item system: the first box
         * starts the (unmodelled) roulette and the slot stays taken until
         * the mushroom is used (LABELLED) */
        if (p->item_held) return false;
        p->item_held = true;
        int q = tile & 3;
        int16_t off = (int16_t)(rd8(rom, T_QUADRANT + (uint32_t)q * 2u)
                              | (rd8(rom, T_QUADRANT + (uint32_t)q * 2u + 1u) << 8));
        int tl = (cell + off) & (SMK_MAP_BYTES - 1);
        int v = tile & 0x0C;
        t->map[tl]                              = rd8(rom, T_USED_BOX + (uint32_t)v);
        t->map[(tl + 1) & (SMK_MAP_BYTES - 1)]   = rd8(rom, T_USED_BOX + (uint32_t)v + 1u);
        t->map[(tl + 128) & (SMK_MAP_BYTES - 1)] = rd8(rom, T_USED_BOX + (uint32_t)v + 2u);
        t->map[(tl + 129) & (SMK_MAP_BYTES - 1)] = rd8(rom, T_USED_BOX + (uint32_t)v + 3u);
        return true;
    }
    return false;
}
