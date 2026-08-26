/* Breakable blocks - Ghost Valley's rails, Vanilla Lake's ice (NOTES 123).
 *
 * Decoded from a session the user played and recorded, replayed here with
 * a watchpoint on the cells that changed:
 *
 *   $80FADC -> $80FBBC   the wall response, with the surface class in A:
 *                          class <  $82   an ordinary wall
 *                          class == $84   arm, then $84D7BA
 *                          class >= $82   arm, then $84D7FA
 *   $80FBF3 (the arm)    take the current slot from $7F:DE30; if its
 *                        counter $7F:DE02,x is still running, do nothing
 *                        (eight slots, so only eight blocks crumble at
 *                        once); otherwise counter = 4 for a PLAYER, 1 for
 *                        anyone else, cell = $02, advance the slot.
 *   $80FC2C (the anim)   once a frame, service the next slot: decrement
 *                        its counter and write the tile it now indexes to
 *                        both VRAM and the tilemap.
 *
 * The tile sequences are per theme ($0126): Ghost Valley (theme 0) uses
 * $80FC70 = 00 28 27 26, every other theme $80FC6C = 08 7D 7C 7B.  Counting
 * down from 4 that gives $26, $27, $28 and finally $00 - and $00's class is
 * $20, the void, which is why a broken block leaves a hole you fall through.
 */
#include "smk.h"
#include <string.h>

#define BLK_SLOTS 8

static struct { int cell, count; } slot[BLK_SLOTS];
static int arm_slot, step_slot;
static smk_track *bound;

void smk_blocks_bind(smk_track *t)
{
    bound = t;
    memset(slot, 0, sizeof slot);
    arm_slot = step_slot = 0;
}

bool smk_blocks_breakable(uint8_t cls)
{
    /* $80FBBC: below $82 is an ordinary wall; $82, $84 and above crumble */
    return cls >= 0x82;
}

bool smk_blocks_hit(int cell, bool by_player)
{
    if (!bound) return false;
    if (slot[arm_slot].count) return false;          /* every slot busy */
    slot[arm_slot].cell = cell;
    slot[arm_slot].count = by_player ? 4 : 1;        /* $80FC0C/$80FC10 */
    arm_slot = (arm_slot + 1) & (BLK_SLOTS - 1);
    return true;
}

void smk_blocks_step(void)
{
    if (!bound) return;
    /* $80FC2C services ONE slot a frame, rotating - so a block takes
     * four times eight frames to crumble away. */
    int s = step_slot;
    step_slot = (step_slot + 1) & (BLK_SLOTS - 1);
    if (!slot[s].count) return;
    static const uint8_t GHOST[4] = { 0x00, 0x28, 0x27, 0x26 };  /* $80FC70 */
    static const uint8_t OTHER[4] = { 0x08, 0x7D, 0x7C, 0x7B };  /* $80FC6C */
    int n = --slot[s].count;
    const uint8_t *seq = (bound->theme == 0) ? GHOST : OTHER;
    int cell = slot[s].cell & (SMK_MAP_BYTES - 1);
    bound->map[cell] = seq[n & 3];
}
