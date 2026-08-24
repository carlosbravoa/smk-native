/* Per-engine-class physics constants, read from the ROM at runtime.
 *
 * $81FEB6:
 *      ldx $0030          ; engine class
 *      ldy $FED5,x        ; -> source address for that class
 *      ldx #$0000
 *  -   lda $0000,y / and #$00FF
 *      asl A x4           ; the ROM stores BYTES; the game widens by <<4
 *      sta $0690,x
 *      iny / inx / inx
 *      cpx #$0080         ; 128 bytes written = 64 words
 *      bne -
 *
 * Storing them as bytes is why the values are all multiples of 16.
 */
#include "smk.h"
#include <string.h>

#define TBL_PHYS_PTRS  0x81FED5u   /* one 16-bit pointer per engine class */

bool smk_physics_load(const smk_rom *rom, int engine_class, smk_physics *out)
{
    if (engine_class < 0 || engine_class >= SMK_PHYS_CLASSES)
        return false;
    memset(out, 0, sizeof *out);
    out->engine_class = engine_class;

    uint32_t tp = smk_snes_to_pc(rom, TBL_PHYS_PTRS) + (uint32_t)engine_class * 2u;
    uint32_t addr = (uint32_t)rom->data[tp] | ((uint32_t)rom->data[tp + 1] << 8);
    /* the pointer is bank-relative to the table itself, which lives in $81 */
    uint32_t src = smk_snes_to_pc(rom, 0x810000u | addr);
    if (src + SMK_PHYS_WORDS > rom->size)
        return false;

    for (int i = 0; i < SMK_PHYS_WORDS; i++)
        out->w[i] = (uint16_t)(rom->data[src + i] << 4);
    return true;
}

/* $80A7E1: clamp the speed to $03FF, multiply by 8, keep bits 15..9, then
 * `xba` - an arithmetic shift right by 8 - to get a byte offset into the
 * table.  Written out, that is a word index of (speed >> 6). */
int16_t smk_physics_accel(const smk_physics *p, int16_t speed)
{
    int s = speed < 0 ? 0 : speed;
    if (s > 0x03FF) s = 0x03FF;
    unsigned byte_off = ((unsigned)(s << 3) & 0xFE00u) >> 8;
    unsigned idx = SMK_PHYS_ACCEL + (byte_off >> 1);
    if (idx >= SMK_PHYS_WORDS) idx = SMK_PHYS_WORDS - 1;
    return (int16_t)p->w[idx];
}
