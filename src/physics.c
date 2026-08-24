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


/* $80AFF9: the slew rate is a table lookup on the heading error.
 *
 *      cmp #$0200 / bcc + / lda #$01FF     clamp the error to $1FF
 *      asl x3 / xba / and #$0E             (err >> 5) & $0E, an even index
 *      adc $C8,x * 2                       per-kart handling row
 *      lda $06D0,y                         words 32.. of the physics blob
 *
 * i.e. word index = 32 + ((min(err, $1FF) >> 6) & 7) + row.
 */
uint16_t smk_physics_turn(const smk_physics *p, uint16_t err, int row)
{
    unsigned e = err;
    if (e > 0x1FF) e = 0x1FF;
    unsigned idx = SMK_PHYS_TURN + ((e >> 6) & 7) + (unsigned)row;
    if (idx >= SMK_PHYS_WORDS) idx = SMK_PHYS_WORDS - 1;
    return p->w[idx];
}


/* $80A590: coasting drag per surface type (8 entries in ROM; types 8-15
 * observed only via $80A65D so the drag for them mirrors its ratios). */
static const int16_t SURF_DRAG[16] = { -4, -8, -16, -24, -36, -56, -64, -85,
                                       -4, -6, -8, -10, -20, -48, -72, -85 };

/* $80A65D: ONE 16-entry per-type deceleration table - the "second row" is
 * types 8-15.  Ice road ($56/$58 = types 11/12) decelerates at only
 * -12/-28: low friction is in the ROM's own numbers. */
static const int16_t SURF_OVERCAP[16] = { -4, -10, -16, -24, -48, -112, -160, -192,
                                          -4, -7, -9, -12, -28, -72, -110, -160 };

int16_t smk_surface_drag(int type)
{
    return SURF_DRAG[type & 15];
}

int16_t smk_surface_overcap_decel(int type)
{
    return SURF_OVERCAP[type & 15];
}

/* Per-surface speed cap ($80A701 structure).  The ROM computes the cap per
 * kart into scratch; these are PLACEHOLDER defaults scaled by drag type -
 * road-family types uncapped, off-road types capped harder with depth.
 * Measuring the real values is blocked on driving a kart over each class
 * (NOTES 053); revisit when player input reaches a kart in the oracle. */
int16_t smk_surface_cap_frac(uint8_t surf)
{
    /* MEASURED from the ROM (NOTES 066): terminal speed per surface class
     * as a fraction of the road's, in thousandths.  Captured by driving
     * the live player kart (un-demo hook, flow-steered) while the WRAM
     * surface table made the whole road each class in turn; slow classes
     * re-measured with a recovery gate.  $26 is a full-stop hazard.
     * Classes not on the demo theme default to their nibble-neighbour. */
    static const struct { uint8_t cls; uint16_t millifrac; } M[] = {
        { 0x26,    0 }, { 0x40, 1000 }, { 0x42,  810 }, { 0x44,  920 },
        { 0x46,  940 }, { 0x48,  970 }, { 0x4A,  891 }, { 0x4C,  875 },
        { 0x4E,  938 }, { 0x50,  849 }, { 0x52,  661 }, { 0x54,  615 },
        { 0x56,  304 }, { 0x58,  565 }, { 0x5A,  603 }, { 0x5C,  601 },
        { 0x5E,  682 },
    };
    for (unsigned i = 0; i < sizeof M / sizeof *M; i++)
        if (M[i].cls == surf) return (int16_t)M[i].millifrac;
    if (surf == 0x00) return 250;            /* void band: crawl (unmeasured) */
    if (surf & 0x20) return 0;               /* solid */
    /* unmeasured class: fall back by type nibble against the measured set */
    uint8_t near = (uint8_t)(0x40 | (surf & 0x1E));
    for (unsigned i = 0; i < sizeof M / sizeof *M; i++)
        if (M[i].cls == near) return (int16_t)M[i].millifrac;
    return 1000;
}

int16_t smk_surface_decel(uint8_t surf)
{
    /* MEASURED (NOTES 067): deceleration toward the surface cap, in speed
     * units per frame, read from the calibration entry curves - e.g. $54
     * falls 788->700->620->580 at 5-frame spacing (~18/frame).  The
     * $80A65D table we used before gave type 10 only -9: playtest
     * correctly reported the bite as too mild.  $50's curve carried a
     * crash artefact and $52 decayed after the sample window: both take
     * the generic measured rate.  $26 collapses to zero within frames. */
    static const struct { uint8_t cls; uint8_t decel; } M[] = {
        { 0x54, 18 }, { 0x56, 22 }, { 0x58, 22 },
        { 0x5A, 16 }, { 0x5C, 16 }, { 0x5E, 22 },
        { 0x50, 18 }, { 0x52, 18 }, { 0x26, 160 },
    };
    for (unsigned i = 0; i < sizeof M / sizeof *M; i++)
        if (M[i].cls == surf) return M[i].decel;
    return 18;                       /* generic measured off-road rate */
}

int16_t smk_surface_cap(uint8_t surf)
{
    /* kept for callers wanting an absolute cap at the legacy scale */
    int frac = smk_surface_cap_frac(surf);
    return (int16_t)(frac >= 1000 ? 0 : (951 * frac) / 1000);
}
