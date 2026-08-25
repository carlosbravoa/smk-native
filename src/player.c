/* The player kart's control, DECODED from the ROM and verified frame-exact
 * against the running game (docs/NOTES.md 103).
 *
 * Nothing in here is a feel constant.  Every table is read from the ROM at
 * setup, and every branch below is a transcription of the routine named in
 * its comment.  The re-simulation in tools/labs/resim.py reproduces the
 * attract race's two human-driven karts field for field - turn rate,
 * heading, both slide offsets, the spin accumulator, the state machine and
 * the 32-bit speed - for every frame of the race.
 *
 * The per-frame order for a player kart ($80A2A3 loop, then $80A2D3):
 *
 *   1. $80A4D0   speed32 += accel32; velocity = speed * (sin, -cos)($A2)
 *   2. $80B1BE   jump machine on $A0 (hop launch $80B53D, flight $80B1D5)
 *   3. $80A892   heading: $A4 += $B2 >> 3;  $A2 = $A4 + $A8;  $2A = $A4 - $AA
 *   4. $80A3B7   control: pad -> $C4, target $D6, drift row $28, steer row
 *                $DE; slide machine $80AA18 on $A6; turn rate $80A80F;
 *                drive/acceleration $80A553 on $AC.
 *
 * Three angles per kart, and the port keeps all three:
 *   $A4  heading    - what the stick turns, and what the camera follows
 *   $A2  velocity   - heading + $A8: the travel direction lags in a slide
 *   $2A  pose       - heading - $AA: the sprite turns INTO the slide
 */
#include "smk.h"
#include <math.h>
#include <string.h>

/* ---- ROM tables ------------------------------------------------------ */
#define T_TOP      0x818000u  /* 8 words: base top speed per character -> $B4 */
#define T_ACCEL_P  0x818010u  /* 8 pointers -> 16 bytes, <<4 (x1.5 at 150cc)  */
#define T_CAP_P    0x818060u  /* 8 pointers -> 6 bytes, types 10..15, <<4    */
#define T_STEER_P  0x818088u  /* 8 pointers -> 3 x [max.w, rev.b, ramp.b, decay.b] */
#define T_DRIFT    0x80AC36u  /* 8 rows x 8 words, the drift parameters       */
#define T_ROWBASE  0x80A4A0u  /* 16 words: drift row base by surface type    */
#define T_ROWCHAR  0x80A4C0u  /* 8 words: drift row adjust by character      */
#define T_LOWTURN  0x80A9B8u  /* 9 words: turn per frame below speed $80     */
#define T_DAMP     0x80A7FFu  /* 8 words: turn-rate damping factor by speed  */
#define T_OVERCAP  0x80A65Du  /* 8 words: decel when over the surface cap    */
#define T_BRAKE    0x80A66Du  /* 8 words: decel with Y held                  */
#define T_COAST    0x80A67Du  /* 8 words: decel with B released              */
#define T_OVERTGT  0x80A68Du  /* 8 words: decel when over the target speed   */
#define T_HOPCAP   0x80B54Du  /* words by type*2: hop penalty threshold      */

static uint8_t rd8(const smk_rom *rom, uint32_t snes)
{
    uint32_t pc = smk_snes_to_pc(rom, snes);
    return pc < rom->size ? rom->data[pc] : 0;
}
static uint16_t rd16(const smk_rom *rom, uint32_t snes)
{
    return (uint16_t)(rd8(rom, snes) | (rd8(rom, snes + 1) << 8));
}
static uint16_t ptr81(const smk_rom *rom, uint32_t tbl, int idx)
{
    return rd16(rom, tbl + (uint32_t)idx * 2u);
}

/* $81F01F..$81F0C4: the per-player physics block ($0710 / $0768), built at
 * race setup from the character's tables, plus $B4 from $81F026. */
bool smk_player_setup(const smk_rom *rom, int character, int engine_class,
                      smk_player *p)
{
    if (character < 0 || character >= 8 || engine_class < 0 || engine_class > 2)
        return false;
    memset(p, 0, sizeof *p);
    p->character = character;
    p->engine_class = engine_class;

    /* $81F026: base top speed by character, then the class adjustment
     * ($2C == 0 selects it): 50cc -$80, 100cc +0, 150cc +$A0. */
    int top = (int16_t)rd16(rom, T_TOP + (uint32_t)character * 2u);
    if (engine_class == 0) top -= 0x80;
    else if (engine_class == 2) top += 0xA0;
    p->base_top = (int16_t)top;

    /* $81F04B: 16 acceleration bytes << 4, x1.5 at 150cc ($81EFFF) */
    uint32_t src = 0x810000u | ptr81(rom, T_ACCEL_P, character);
    for (int i = 0; i < 16; i++) {
        int v = rd8(rom, src + (uint32_t)i) << 4;
        if (engine_class == 2) v += v >> 1;
        p->accel[i] = (uint16_t)v;
    }
    /* $81F06A: caps - types 0..9 are $FFFF (no cap), 10..15 from 6 bytes
     * << 4, +$30 at 150cc ($81F011) */
    src = 0x810000u | ptr81(rom, T_CAP_P, character);
    for (int t = 0; t < 16; t++) {
        if (t < 10) { p->cap[t] = -1; continue; }
        int v = rd8(rom, src + (uint32_t)(t - 10)) << 4;
        if (engine_class == 2) v += 0x30;
        p->cap[t] = (int16_t)v;
    }
    /* $81F092: three steering rows of [max.w, reversal.b, ramp.b, decay.b] */
    src = 0x810000u | ptr81(rom, T_STEER_P, character);
    for (int r = 0; r < 3; r++) {
        p->steer[r][0] = rd16(rom, src);
        p->steer[r][1] = rd8(rom, src + 2);
        p->steer[r][2] = rd8(rom, src + 3);
        p->steer[r][3] = rd8(rom, src + 4);
        src += 5;
    }

    for (int r = 0; r < 8; r++)
        for (int i = 0; i < 8; i++)
            p->drift[r][i] = rd16(rom, T_DRIFT + (uint32_t)(r * 16 + i * 2));
    for (int i = 0; i < 16; i++) p->row_base[i] = (int16_t)rd16(rom, T_ROWBASE + (uint32_t)i * 2u);
    for (int i = 0; i < 8; i++)  p->row_char[i] = (int16_t)rd16(rom, T_ROWCHAR + (uint32_t)i * 2u);
    for (int i = 0; i < 9; i++)  p->lowturn[i]  = rd16(rom, T_LOWTURN + (uint32_t)i * 2u);
    for (int i = 0; i < 8; i++) {
        p->damp[i]    = (int16_t)rd16(rom, T_DAMP    + (uint32_t)i * 2u);
        p->overcap[i] = (int16_t)rd16(rom, T_OVERCAP + (uint32_t)i * 2u);
        p->brake[i]   = (int16_t)rd16(rom, T_BRAKE   + (uint32_t)i * 2u);
        p->coast[i]   = (int16_t)rd16(rom, T_COAST   + (uint32_t)i * 2u);
        p->overtgt[i] = (int16_t)rd16(rom, T_OVERTGT + (uint32_t)i * 2u);
    }
    for (int t = 10; t < 16; t++)
        p->hopcap[t] = (int16_t)rd16(rom, T_HOPCAP + (uint32_t)t * 2u);

    p->coins = 0;
    smk_player_reset(p, 0);
    return true;
}

/* $80B46B/$80B47C/$80B489: a mushroom.  Refused in the spin states
 * ($809E0B: $0A-$10 and $1A); otherwise 32 frames of boost. */
bool smk_player_boost(smk_player *p)
{
    int st = p->state;
    if ((st >= 0x0A && st <= 0x10) || st == 0x1A) return false;
    p->vlag = 0;                 /* stz $A8: the slide's velocity lag is dropped */
    p->state = 0x1C;             /* $A6 = $1C: settle (the pose offset decays)   */
    p->fc = 0x20;
    p->flags |= 0x0080;
    p->drive = 0x10;
    return true;
}

void smk_player_reset(smk_player *p, uint16_t heading)
{
    p->heading = p->vel_angle = p->pose = heading;
    p->turn = p->vlag = p->plag = p->spin = 0;
    p->state = 0;
    p->drive = 0;
    p->jump_state = 0;
    p->flags = 0;
    p->fc = p->ca = 0;
    p->accel32 = 0;
    p->target = p->base_top;
}

/* ---- the DSP-1's sine ------------------------------------------------- */
/* Command $04 on the DSP-1 does not compute a floating sine: it
 * interpolates a 256-entry 1.15 table with a slope table (the algorithm
 * the DSP-1 emulators use, tables generated here - no chip ROM data),
 * negative angles by symmetry, results clamped to $7FFF, and the radius
 * product floored.  Fitted against every velocity sample of the demo
 * race (tools/labs/mame/demo_race.csv): 2317/2381 frames bit-exact, the
 * rest +-1 - a double sin/cos matches only 22%.  LABELLED: the residual
 * +-1 needs the uPD7725 microcode itself. */
static int16_t SIN_T[256], COS_T[256], MUL_T[256];
static bool sin_ready;
static void sin_init(void)
{
    if (sin_ready) return;
    for (int i = 0; i < 256; i++) {
        double a = 2.0 * M_PI * i / 256.0;
        long sv = lround(32768.0 * sin(a)), cv = lround(32768.0 * cos(a));
        SIN_T[i] = (int16_t)(sv > 32767 ? 32767 : sv);
        COS_T[i] = (int16_t)(cv > 32767 ? 32767 : cv);
        MUL_T[i] = (int16_t)lround(i * M_PI);
    }
    sin_ready = true;
}
static int16_t dsp_sin(int16_t a)
{
    if (a < 0) { if (a == -32768) return 0; return (int16_t)-dsp_sin((int16_t)-a); }
    int s = SIN_T[a >> 8] + ((MUL_T[a & 0xFF] * COS_T[a >> 8]) >> 15);
    return (int16_t)(s > 32767 ? 32767 : s);
}
static int16_t dsp_cos(int16_t a)
{
    if (a < 0) { if (a == -32768) return -32768; a = (int16_t)-a; }
    int s = COS_T[a >> 8] - ((MUL_T[a & 0xFF] * SIN_T[a >> 8]) >> 15);
    return (int16_t)(s < -32768 ? -32767 : s);
}
void smk_dsp_sincos(uint16_t angle, int16_t radius, int16_t *sx, int16_t *cy)
{
    sin_init();
    *sx = (int16_t)(((int32_t)radius * dsp_sin((int16_t)angle)) >> 15);
    *cy = (int16_t)(((int32_t)radius * dsp_cos((int16_t)angle)) >> 15);
}

/* ---- helpers --------------------------------------------------------- */
static inline int16_t s16(int v) { return (int16_t)(uint16_t)v; }
static inline int abs16(int v) { return v < 0 ? -v : v; }

/* $80AC13: the pose-offset thresholds that flip the drift sprite bits */
static void pose_bits(smk_player *p, int mag)
{
    if (mag >= 0x1800)      p->flags |= 0x0024;
    else if (mag >= 0x0C00) p->flags = (p->flags & ~0x0020u) | 0x0004u;
    else                    p->flags &= ~0x0024u;
}

/* $80AAB4: both offsets decay toward zero at the row's rates.  The ROM only
 * stores $A8 when the step does not reach zero, so a small residual stays
 * behind (the -64 / -96 seen in every capture) - kept, because it is what
 * the game does and it feeds the next slide's first frames. */
static void decay(smk_player *p, const uint16_t *row)
{
    int r6 = row[6], r4 = row[4];
    if (p->plag < 0) { p->plag = s16(p->plag + r6); if (p->plag > 0) p->plag = 0; }
    else             { p->plag = s16(p->plag - r6); if (p->plag < 0) p->plag = 0; }
    bool done;
    if (p->vlag < 0) {
        done = (p->vlag + r4) >= 0;
        if (!done) p->vlag = s16(p->vlag + r4);
    } else {
        done = (p->vlag - r4) < 0;
        if (!done) p->vlag = s16(p->vlag - r4);
    }
    if (done) {
        if (p->flags & 0x40) {                 /* the long-hold reward */
            p->flags &= ~0x40u;
            p->state = 0x12;
            p->fc = 0x30;
        } else
            p->state = 0;
    }
}

/* ---- the frame ------------------------------------------------------- */
void smk_player_step(smk_player *p, smk_kart *k, const smk_track *t,
                     uint16_t held, uint16_t pressed)
{
    /* 0. $80879D - the position integration runs BEFORE the kart loop
     *    recomputes the velocity: pos(N+1) = pos(N) + v(N).  Verified by
     *    the demo replay - integrating with the new velocity crept ~0.08
     *    px per frame away from the game with identical velocities. */
    smk_kart_move(k, t);

    /* 1. $80A4D0 - speed and velocity.  The velocity angle is $A2, the
     *    heading plus the slide's velocity lag, from LAST frame's update. */
    k->accel      = (int16_t)(p->accel32 >> 16);
    k->accel_frac = (uint16_t)(p->accel32 & 0xFFFF);
    smk_kart_accelerate(k);
    if (k->bounce_cool == 0) {
        int16_t sx, cy;
        smk_dsp_sincos(p->vel_angle, k->speed, &sx, &cy);   /* DSP-1 cmd $04 */
        k->vx = sx;
        k->vy = (int16_t)-cy;                                /* eor/inc: -cos */
    }
    int spd = k->speed;

    /* Stages 2 and 3 read the pad word $C4 that the control stage composed
     * at the end of the PREVIOUS frame - so a hop lands the frame after
     * the press, exactly as in the game. */
    const uint16_t c4p = p->pad;

    /* 2. $80B1BE - the jump machine */
    uint8_t surf = smk_track_surface(t, smk_kart_px(k->x), smk_kart_px(k->y));
    /* $80B3B7: the surface TYPE $B0 is taken only from driveable classes
     * ($40 and up: `cmp #$40 / bcs`).  $20-$3F are the wall/hazard
     * handlers and $00-$1F the object classes (item box $14, coin $16,
     * $1A a no-op), none of which touch $B0 - so a stamped object tile
     * under the kart keeps the road's type.  (Verified by the demo
     * replay: the game applies no bite for a $1A frame.) */
    if (surf >= 0x40) p->type = smk_surface_type(surf);
    if (k->airborne) {
        /* $80B1D5: gravity $1A, z += zvel << 8, land on the sign of $1F */
        smk_kart_gravity(k);
        if (!k->airborne) {
            p->flags &= ~0x8000u;
            if (p->drive != 0x10) p->drive = 0;
        }
    } else {
        /* $80B49D / $80B53D: a FRESH L or R press hops, unless the other
         * shoulder is held.  On a capped surface at or over its hop
         * threshold the hop costs $40 of speed. */
        bool hop = ((c4p & 0x0004) && !(c4p & 0x0020))     /* R edge, L free */
                || ((c4p & 0x0008) && !(c4p & 0x0010));    /* L edge, R free */
        if (hop) {
            if (p->type >= 10 && spd >= p->hopcap[p->type]) {
                spd -= 0x40;
                k->speed = (int16_t)spd;
            }
            smk_kart_launch(k, 0x00E0);       /* $80B77B: $E0 */
            p->flags |= 0x8000;
            p->jump_state = 2;
        }
    }
    if (!k->airborne) {
        p->jump_state = 0;
        /* the wall/object knockback window (kart.c sets it on impact):
         * count it down here - the old step did it inside smk_kart_face,
         * which this path no longer calls, and a window that never closed
         * kept the reflected velocity forever: infinite bouncing */
        if (k->bounce_cool > 0) k->bounce_cool--;
    }

    /* 3. $80A892 - heading, velocity angle, pose */
    {
        int d;
        if (spd >= 0x80 || (p->flags & 0x8000)) {
            d = p->turn >> 3;                     /* arithmetic, cmp/ror x3 */
        } else {
            int y;
            if (p->jump_state == 8) y = 8;
            else { p->turn = 0; y = (spd >> 4) & 7; }
            if (c4p & 0x0200)       d = -(int)p->lowturn[y];
            else if (c4p & 0x0100)  d =  (int)p->lowturn[y];
            else                    d = 0;
        }
        p->heading   = (uint16_t)(p->heading + d);
        p->vel_angle = (uint16_t)(p->heading + p->vlag);
        p->pose      = (uint16_t)(p->heading - p->plag);
    }

    /* 4. $80A3B7 -> $80A3CC - control */
    /* the pad word: bits 0-1 Right/Left edges, 2-3 R/L edges, then held */
    uint16_t c4 = (uint16_t)(((pressed >> 8) & 3) | ((pressed >> 2) & 0xC) | held);
    p->pad = c4;
    int coins = p->coins > 10 ? 10 : p->coins;
    p->target = (int16_t)(p->base_top + coins * 8);   /* $D6 = $B4 + 8*coins */

    int s08;
    if (c4 & 0x4030) {                 /* Y, L or R held: the power-slide row */
        p->steer_row = 2;              /* $DE = $50: block + $50 = the third row */
        p->row = 7;                    /* $28 = $70 */
        s08 = c4 & 3;
    } else if (p->flags & 0x0002) {
        p->steer_row = 0; p->row = 0; s08 = c4 & 3;
    } else {
        p->steer_row = 0;              /* $DE = $40 */
        s08 = p->row_base[p->type];    /* left in $08 - see $80AB94 users */
        int r = s08;
        if (r) { r += p->row_char[p->character]; if (r < 0) r = 0; }
        p->row = r >> 4;
    }
    const uint16_t *row = p->drift[p->row];

    /* $80AA18 - the slide machine */
    if (!(p->flags & 1)) p->flags &= ~0x002Cu;
    if ((p->state == 0x12 || p->state == 0x14) && p->row == 7)
        p->state = 0;                  /* $80AA4B: re-arm, and run it now */
    switch (p->state) {
    case 0: {                          /* $80AA52: armed - can a slide start? */
        p->spin = 0;
        int a = spd;
        bool dec = false;
        if (p->engine_class == 2) a += 0x120;      /* $0030 == 4: 150cc */
        else if (spd < 0x100) dec = true;
        if (!dec) {
            a -= p->base_top;
            if (a >= 0) p->state = 2;
            else if ((a & 0xFFFF) < row[0]) dec = true;
            else if (abs16(p->turn) >= 0x300) p->state = 2;
            else dec = true;
        }
        if (dec) decay(p, row);
        break;
    }
    case 2: case 4: case 6: case 8: {  /* $80AAFA: sliding */
        if (p->row == 0 || spd < 0x100) { p->state = 0x1C; break; }
        int steer = 0;
        if (c4 & 0x8000) steer = c4 & 0x300;
        else if ((c4 & 0x30) && spd >= 0x1C0) steer = c4 & 0x300;
        if (steer & 0x200) {           /* Left: $80AB76 */
            if (p->plag < 0) p->plag = s16(p->plag + row[6]);
            else { pose_bits(p, p->plag);
                   p->plag = s16(p->plag >= (int)row[7] ? p->plag - row[6] : p->plag + row[6]); }
            int a;
            if (p->vlag < 0) {         /* $80AB64 */
                p->spin = s16(p->spin + s08);
                a = p->vlag + row[5];
            } else {                   /* $80ABCE: the spin accumulator */
                p->spin = s16(p->spin - row[1]);
                if (p->spin < 0 && (uint16_t)p->spin < 0x8600) p->state = 0x10;
                a = p->vlag + row[3];
            }
            p->vlag = (int16_t)(a < 0 ? -(abs16(a) < (int)row[2] ? abs16(a) : (int)row[2])
                                      : (a < (int)row[2] ? a : (int)row[2]));
        } else if (steer & 0x100) {    /* Right: $80AB36 */
            if (p->plag >= 0) p->plag = s16(p->plag - row[6]);
            else { pose_bits(p, -p->plag);
                   p->plag = s16(-p->plag >= (int)row[7] ? p->plag + row[6] : p->plag - row[6]); }
            int a;
            if (p->vlag >= 0) {        /* $80AB59 */
                p->spin = s16(p->spin - s08);
                a = p->vlag - row[5];
            } else {                   /* $80ABB7 */
                p->spin = s16(p->spin + row[1]);
                if (p->spin >= 0x7A00) p->state = 0x0E;
                a = p->vlag - row[3];
            }
            p->vlag = (int16_t)(a < 0 ? -(abs16(a) < (int)row[2] ? abs16(a) : (int)row[2])
                                      : (a < (int)row[2] ? a : (int)row[2]));
        } else {                       /* $80AB2F: released - drain and decay */
            int step = row[1] + 0xE0 + 1;
            p->spin = s16(p->spin < 0 ? p->spin + step : p->spin - step);
            decay(p, row);
        }
        break;
    }
    case 0x0E:                         /* $80A904: spinning out (from a right slide) */
    case 0x10: {                       /* $80A927: spinning out (from a left slide) */
        p->flags |= 0x0008;
        if (p->flags & 2) { p->state = 0x1C; break; }
        int step = p->state == 0x0E ? -0x480 : 0x480;
        unsigned before = (uint16_t)p->plag;
        p->plag = s16(p->plag + step);
        if (spd < 0x180) {
            bool wrapped = p->state == 0x0E ? before < 0x480 : before + 0x480 >= 0x10000;
            if (wrapped) p->state = 0x1C;
        }
        break;
    }
    case 0x12: case 0x14:              /* $80AA4B: the reward holds unless row 7 */
        break;
    case 0x1C: {                       /* $80AA8D: settle with row 0's rates */
        decay(p, p->drift[0]);
        if (p->vlag == 0) p->state = 0;
        break;
    }
    default:
        p->state = 0x1C;
        break;
    }

    /* $80A80F - the turn rate $B2 */
    {
        const uint16_t *st = p->steer[p->steer_row];
        int mx = st[0], rev = st[1], ramp = st[2], dcy = st[3];
        if ((p->flags & 0x200) && !(p->flags & 0x400)) {
            /* held */
        } else if (c4 & 0x200) {                     /* Left */
            if (p->turn == 0) p->turn = s16(-ramp);
            else if (p->turn > 0) p->turn = s16(p->turn - rev);
            else { int m = -p->turn; p->turn = s16(m >= mx ? -mx : -(m + ramp)); }
        } else if (c4 & 0x100) {                     /* Right */
            if (p->turn < 0) p->turn = s16(p->turn + rev);
            else if (p->turn >= mx) p->turn = s16(mx);
            else p->turn = s16(p->turn + ramp);
        } else {
            if (p->turn >= 0) { p->turn = s16(p->turn - dcy); if (p->turn < 0) p->turn = 0; }
            else              { p->turn = s16(p->turn + dcy); if (p->turn > 0) p->turn = 0; }
        }
    }

    /* $80A553 -> $80A5E3 ($AC == $10) - the mushroom boost: +$32 per frame
     * up to $7E0 for $FC frames, then $80A5FC ends it.  (The AI's table
     * routes $10 to $80B015, which adds a sector test; the player's $A53B
     * table does not - the demo boosts straight through a row-2 sector.) */
    if (p->drive == 0x10) {
        if (--p->fc != 0) {
            int ee;
            if (spd <= 0x7E0) ee = 0x32;
            else { k->speed = 0x7E0; spd = 0x7E0; ee = 0; }
            p->accel32 = ((int32_t)ee << 16) | (p->accel32 & 0xFFFF);
        } else {
            p->flags &= ~0x00C0u;
            p->drive = 0;
        }
    }
    /* $80A553 -> $80A69D ($AC == 0) - drive */
    else if (p->drive == 0) {
        /* the long-hold counter: shoulder + steer held 128 frames arms the
         * post-slide reward ($E2 bit 6) */
        if ((c4 & 0x30) && (c4 & 0x300)) {
            if (++p->ca >= 0x80) p->flags |= 0x40;
        } else
            p->ca = 0;

        int ee = 0;                    /* $EE, whole units per frame */
        bool table = false;            /* the A<<8 path (stz $EE / sta $ED) */
        int A = 0;
        int st = p->state;
        if (st <= 8) {                 /* $80A6F7 */
            if (c4 & 0x4000) {         /* Y: brake */
                ee = p->brake[(spd >> 8) & 7];
            } else if (!(c4 & 0x8000)) {   /* B released: coast */
                ee = p->coast[(spd >> 8) & 7];
            } else {
                int cap = p->cap[p->type];
                if (cap >= 0 && spd > cap) {
                    ee = p->overcap[(spd >> 8) & 7];         /* off-road bite */
                } else if (spd >= p->target) {
                    int over = spd - p->target;
                    if (over > 0x1FF) over = 0x1FF;
                    ee = p->overtgt[(over >> 6) & 7];
                } else {
                    table = true;
                }
            }
        } else if (st == 0x0A || st == 0x0C) {
            ee = -8;
        } else if (st == 0x0E || st == 0x10 || st == 0x1A) {
            ee = -16;                  /* $80A64F: the spin's deceleration */
        } else if (st == 0x12 || st == 0x14) {   /* $80A5C5: the reward */
            if (--p->fc == 0) { p->state = 0x1C; ee = 0; }
            else {
                int cap = p->target + 0xC0;
                if (cap >= spd) ee = 2;
                else { k->speed = (int16_t)cap; spd = cap; ee = 0; }
            }
        } else if (st == 0x18) {
            ee = 0;
        } else {                       /* $1C -> $80A77F: the plain accel path */
            table = true;
        }

        if (table) {
            /* $80A77F: damp the turn rate by speed (DSP-1 multiply,
             * (k * $B2) >> 15), then accel from the character's table */
            int kf;
            if (spd >= 0x3FF) kf = p->damp[7];
            else if (spd < 0x300) kf = 0;
            else kf = p->damp[((spd - 0x300) >> 5) & 7];
            p->turn = s16(p->turn + ((kf * p->turn) >> 15));
            int s = spd > 0x3FF ? 0x3FF : (spd < 0 ? 0 : spd);
            A = (p->flags & 1) ? 0xC0 : (int)p->accel[s >> 6];
            /* stz $EE / sta $ED: accel32 = A << 8 with a stale low byte */
            p->accel32 = ((int32_t)(A >> 8) << 16)
                       | (uint32_t)(((A & 0xFF) << 8) | (p->accel32 & 0xFF));
        } else {
            /* sta $EE only: the low word $EC keeps whatever it held */
            p->accel32 = ((int32_t)ee << 16) | (p->accel32 & 0xFFFF);
        }
    }

    k->angle = p->heading;   /* the camera follows the heading ($808632) */
}
