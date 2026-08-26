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
    p->item_held = false;          /* the item is spent */
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
    p->hazard = 0; p->resc_t = 0;
    p->item_held = false;
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

/* $80B7BB: launch at an angle - DSP-1 sin/cos with the speed as radius:
 * $26 = speed * sin(angle) becomes the vertical velocity, the forward
 * speed becomes speed * cos(angle), and the kart is airborne. */
static void launch(smk_player *p, smk_kart *k, uint16_t angle)
{
    int16_t sx, cy;
    smk_dsp_sincos(angle, k->speed, &sx, &cy);
    k->zvel = sx;
    k->airborne = true;
    p->flags |= 0x8000;
    k->speed = cy;
}
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
    smk_kart_move_ex(k, t, false);

    /* 1. $80A4D0 - speed and velocity.  The velocity angle is $A2, the
     *    heading plus the slide's velocity lag, from LAST frame's update. */
    k->accel      = (int16_t)(p->accel32 >> 16);
    k->accel_frac = (uint16_t)(p->accel32 & 0xFFFF);
    /* The bounce window does not just hold the VELOCITY, it holds the
     * SPEED (NOTES 130).  Measured in the game: after the hit $EA sat at
     * 418 for all eight frames and only moved once control came back.
     * Accelerating through the window is what made bouncing free - you
     * came off the wall already back up to speed. */
    if (k->bounce_cool == 0) {
        /* $80A55B, drive state $16: once the window lets go, the kart is
         * still travelling sideways to where it points, and THAT costs
         * speed.  $EE comes from the table at $80A590 indexed by the
         * velocity lag - `(min(|$A8|,$4000) >> 10) * 2` - so a square hit
         * takes the last entry, -85 a frame, and a glancing one barely
         * anything.  Measured in the game: 419 -> 334 -> 250 -> 165
         * before the throttle bit again (NOTES 132). */
        /* $80A0EB: a slip under 45 degrees is a GRAZE - $A6 = $1C, $AC
         * stays 0 and the throttle is never interrupted.  Only a hit more
         * than 45 degrees off arms drive state $16 and its deceleration.
         * Measured in the user's run: a 1103-unit slip at frame 1045 kept
         * $EE = +12 right through (NOTES 133). */
        /* $80A0EB exempts a slip under 45 degrees - a graze keeps its
         * throttle - and the user's run shows the game doing exactly that
         * at frame 1044.  Applying it here makes the port WORSE (82.0% ->
         * 73.4% within 1 px, heading errors 37 -> 1718), with the slip
         * taken the game's way or ours.  So something upstream still
         * differs and the exemption is left out, logged (NOTES 134). */
        if (k->crash_frames > 0) {
            static const int16_t CRASH[8] =        /* $80A590 */
                { -4, -8, -16, -24, -36, -56, -64, -85 };
            int mag = k->crash_lag < 0 ? -k->crash_lag : k->crash_lag;
            if (mag >= 0x4000) mag = 0x3F00;       /* $80A573 */
            int idx = ((mag >> 8) >> 2) & 0x1E;    /* $80A57B: xba/lsr/lsr/and */
            p->accel32 = (int32_t)CRASH[idx >> 1] << 16;
            k->accel = CRASH[idx >> 1];
            k->accel_frac = 0;
            k->crash_frames--;
            /* $80A9FD/$80AA05 walk the lag toward zero $40 a frame */
            if (k->crash_lag > 0) k->crash_lag = (int16_t)(k->crash_lag - 0x40);
            else if (k->crash_lag < 0) k->crash_lag = (int16_t)(k->crash_lag + 0x40);
        }
        smk_kart_accelerate(k);
    }
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
    /* $80FA5A: four or more pixels up, the kart reads plain road ($40) */
    if ((k->z >> 16) >= 4) surf = 0x40;
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
            if (p->drive != 0x10) p->drive = 0;        /* $80B216 */
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
    /* ---- the hazard states ($A0 = 6/8), before anything else ---------- */
    if (p->hazard == 8) {                  /* $80B24D: in the water */
        if (p->ca == 0) { p->hazard = 6; p->resc_t = 0; k->speed = 0; }
        else {
            p->ca--;
            /* below $78 the ROM raises the splash flags ($10 bit 8, $D4
             * bit 10); the effect object is not wired to them yet */
            uint8_t here = smk_track_surface(t, smk_kart_px(k->x), smk_kart_px(k->y));
            if (here == 0x24) { p->hazard = 6; p->resc_t = 0; k->speed = 0; }
            else if (here != 0x22 && here < 0x80) {      /* $80B286: out */
                p->hazard = 0;
                p->flags |= 0x0010;
                k->speed = 0x0100;
                launch(p, k, 0x3E00);
                p->jump_state = 2; p->drive = 2;
            }
        }
        if (p->hazard == 8) {
            /* $80A5AD (drive state 8): B held under $7C accelerates by 1,
             * anything else decelerates by 1 - the measured wade, speed
             * settling at 123/124 (tools/labs/mame sink captures) */
            int ee = ((p->pad & 0x8000) && k->speed < 0x7C) ? 1 : -1;
            p->accel32 = ((int32_t)ee << 16) | (p->accel32 & 0xFFFF);
            k->accel = (int16_t)(p->accel32 >> 16);
            k->accel_frac = (uint16_t)(p->accel32 & 0xFFFF);
            smk_kart_accelerate(k);
            int16_t sx2, cy2;
            smk_dsp_sincos(p->vel_angle, k->speed, &sx2, &cy2);
            k->vx = sx2; k->vy = (int16_t)-cy2;
            smk_kart_move_ex(k, t, false);
            k->angle = p->heading;
            return;
        }
    }
    /* $80B3DD/$80A0C7 - the rest of a wall's cost - is DECODED but NOT
     * ported (NOTES 131).  It re-derives the velocity angle from the
     * bounce, writes the difference straight into the slide's lag $A8 and
     * drops the kart into drive state $16.  Writing $A8 from outside the
     * slide machine puts it far past the clamps the drift rows apply, and
     * in play that was worse than not having it at all: a head-on bounce
     * left the kart dead, and a bounce taken while sliding turned into a
     * ball ricocheting at 1500+.  The measured part - the window holding
     * the SPEED - is in smk_player_step and gives the cost on its own. */
    if (k->bounce_hit) k->bounce_hit = 0;

    if (p->hazard == 6 || p->hazard == 0x0C || p->hazard == 0x0E) {
        /* Lakitu's rescue, transcribed from the ROM's own three states
         * ($A0 = 6 -> $0C -> $0E), NOTES 124.  The earlier port merged
         * them and re-read the target every frame, so the kart chased a
         * waypoint that moved with it and was never put down.
         *
         *   6    $80B5B7 armed it: $B373 LATCHED the target ($CC/$CE from
         *        the kart's waypoint $C0, $D0 from the direction field at
         *        that waypoint) and set $CA; the kart falls while $CA runs.
         *   $0C  $80B2B6: turn toward $D0 ($B346, $140 a frame, snapping
         *        inside $200), then walk $18 (integer x) 2 px toward $CC -
         *        and RETURN.  Only once x matches does y walk toward $CE.
         *   $0E  $80B32E: turn again; ONLY when $B346 returns carry set -
         *        the heading has arrived - does $1F come down by $80 a
         *        frame.  When it borrows: stz $1F, stz $A0, stz $AC.
         */
        k->speed = 0; k->speed_frac = 0; p->accel32 = 0;
        k->vx = k->vy = 0; p->turn = 0;
        p->vlag = p->plag = 0; p->state = 0;
        k->airborne = false;

        /* $80B346: turn toward $D0, carry set once it has arrived */
        bool faced = false;
        {
            int d = (int16_t)(uint16_t)(p->resc_h - p->heading);
            if (d == 0) faced = true;
            else if (d > 0x0200 || d < -0x0200)
                p->heading = (uint16_t)(p->heading + (d > 0 ? 0x0140 : -0x0140));
            else p->heading = p->resc_h;       /* $80B35C/$80B360: snap */
            p->vel_angle = p->pose = p->heading;
            k->angle = p->heading;
        }

        if (p->hazard == 6) {                             /* still falling */
            if (p->resc_t < 60) { p->resc_t++; k->z -= (int32_t)0x0180 << 8; }
            else { k->z = (int32_t)0x3000 << 8; p->hazard = 0x0C; }
            return;
        }
        if (p->hazard == 0x0C) {                          /* Lakitu carries it */
            int32_t tx = (int32_t)p->resc_x << 16, ty = (int32_t)p->resc_y << 16;
            int32_t step = 2 << 16;                       /* $80B2D2: 2 px */
            if (k->x != tx) {                             /* x FIRST, then return */
                if (k->x < tx) k->x += (tx - k->x < step) ? tx - k->x : step;
                else           k->x -= (k->x - tx < step) ? k->x - tx : step;
                return;
            }
            if (k->y != ty) {
                if (k->y < ty) k->y += (ty - k->y < step) ? ty - k->y : step;
                else           k->y -= (k->y - ty < step) ? k->y - ty : step;
                return;
            }
            p->hazard = 0x0E;                             /* $80B328 */
            return;
        }
        /* $0E: down $80 a frame, but only once the heading has arrived */
        if (!faced) return;                               /* $80B332: bcc */
        k->z -= (int32_t)0x0080 << 8;
        if (k->z <= 0) {
            k->z = 0; p->hazard = 0; p->drive = 0; p->jump_state = 0;
            p->resc_t = 0;
        }
        return;
    }

    if (!k->airborne && p->drive != 0x10 && (surf & 0xFE) >= 0x20 && (surf & 0xFE) < 0x40
        && p->jump_state == 0) {
        /* $80B3F1 -> the hazard table at $80B39B, classes $20-$3E */
        switch (surf & 0x0E) {
        case 0x02:                          /* $22: water ($80B56D) */
            if (k->speed >= 0x200) {        /* $80B606: skim off it */
                k->speed = (int16_t)(k->speed - (k->speed >= 0x400 ? 0x2C0 : 0xA0));
                p->flags |= 0x0010;
                launch(p, k, 0x0800);
                p->jump_state = 2; p->drive = 2;
            } else {                        /* $80B5EC: fall in */
                p->flags &= 0x4002;
                k->z = 0; k->zvel = 0; k->airborne = false;
                k->speed = 0; k->speed_frac = 0; p->accel32 = 0;
                p->turn = 0; p->vlag = p->plag = 0; p->state = 0;
                p->ca = 0x0102;
                p->hazard = 8; p->drive = 8; p->jump_state = 8;
            }
            break;
        case 0x00:                          /* $20: the void - Ghost Valley,
                                             * Rainbow Road (NOTES 119) */
        case 0x08:                          /* $28: Rainbow Road's edge - the
                                             * same fall, measured (NOTES 120) */
        case 0x04:                          /* $24: lava / the pit ($80B643) */
        case 0x06:                          /* $26: the deep drop ($80B626) */
            k->speed = 0; k->speed_frac = 0; p->accel32 = 0;
            p->hazard = 6; p->resc_t = 0;
            p->drive = (surf & 0x0E) == 0x04 ? 6 : 0x0A;   /* $20 measured as $04 */
            p->jump_state = p->drive;
            break;
        case 0x0A:                          /* $2A: the bump ($80B67C) */
            if (k->speed < 0x2E0) k->speed = 0x2E0;
            k->z = (int32_t)0x0280 << 8;
            p->jump_state = 2; p->drive = 2;
            break;
        case 0x0C: {                        /* $2C: launch, speed kept */
            int16_t keep = k->speed;
            launch(p, k, 0x0D00);
            k->speed = keep;
            p->jump_state = 2; p->drive = 2;
            break;
        }
        default: break;                     /* $20 wall, $28, $2E: elsewhere */
        }
    }
    if (!k->airborne && p->drive != 0x10 && (surf & 0xFE) < 0x20 && p->jump_state == 0) {
        /* $80B3B7 -> the object-class table at $80B3A5 (classes $00-$1E)
         * and the hazard table at $80B39B ($20-$3E) run right after the
         * hop check, on the class of the cell under the kart. */
        switch (surf & 0x1E) {
        case 0x10:                          /* $80B67C: ramp */
            if (k->speed < 0x2E0) k->speed = 0x2E0;   /* $80B7AF */
            launch(p, k, 0x0E00);
            k->z = (int32_t)0x0280 << 8;               /* $1F = $280 */
            p->jump_state = 2; p->drive = 2;
            break;
        case 0x12: case 0x1C: {             /* $80B666: the mud jump */
            int16_t keep = k->speed;
            launch(p, k, 0x0D00);
            k->speed = keep;                           /* pla / sta $EA */
            p->jump_state = 2; p->drive = 2;
            break;
        }
        case 0x16: {                        /* $80B47B: a boost pad */
            int st = p->state;
            if (!((st >= 0x0A && st <= 0x10) || st == 0x1A)) {   /* $809E0B */
                p->fc = 0x20; p->flags |= 0x0080; p->drive = 0x10;
            }
            break;
        }
        case 0x18:                          /* $80B426: oil - spin out at speed */
            if (!(p->flags & 2) && k->speed >= 0x300) {
                p->state = p->plag < 0 ? 0x0E : 0x10;
                p->vlag = 0;
            }
            p->flags &= ~0x1800u;
            break;
        case 0x1E:                          /* $80B69D: a bump */
            if (k->speed >= 0x100) {
                k->zvel = 0x0080; k->z = (int32_t)0x0100;   /* $1E = $100 */
                k->airborne = true; p->flags |= 0x8000;
                p->jump_state = 2;
            }
            break;
        default: break;                     /* $14 box, $1A coin: the collector */
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
    if ((c4 & 0x0080) && !(p->pad_prev & 0x0080)) p->item_held = false;   /* A: the item is used (no item system yet) */
    p->pad_prev = c4;
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
    else if (p->drive == 2) {              /* $80A647: airborne off a ramp - no thrust */
        p->accel32 = p->accel32 & 0xFFFF;
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
