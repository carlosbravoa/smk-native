/* Bananas and shells - the player's thrown items.
 *
 * Numbers from the oracle, not from the ROM's script VM: $80:F243 is a
 * chain of per-frame handlers run through jmp ($0000,x) and reading it
 * would have cost the day.  What was measured instead is in smk.h and
 * docs/ITEMS.md §5, with the two things still OURS labelled there.
 */
#include "smk.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

static int free_slot(smk_proj *list, int n)
{
    for (int i = 0; i < n; i++) if (list[i].kind == SMK_PROJ_NONE) return i;
    return -1;
}

static void set_velocity(smk_proj *p)
{
    int16_t sx, cy;
    smk_dsp_sincos(p->heading, p->speed, &sx, &cy);
    p->vx = sx;
    p->vy = (int16_t)-cy;
}

void smk_proj_throw(smk_proj *list, int n, int kind, const smk_kart *k,
                    uint16_t heading, int owner, int target, bool backward,
                    bool ahead)
{
    int i = free_slot(list, n);
    if (i < 0) return;                           /* $80:F190: no block free */
    smk_proj *p = &list[i];
    memset(p, 0, sizeof *p);
    p->kind = kind;
    p->owner = owner;
    p->target = target;
    p->safe = SMK_PROJ_OWNER_SAFE;
    p->heading = (uint16_t)(heading + (backward ? 0x8000 : 0));
    p->x = k->x; p->y = k->y; p->z = 0;
    switch (kind) {
    case SMK_PROJ_BANANA:
        if (ahead) {
            /* thrown: flies ahead and lands - flight time OURS until the
             * chain2 measurement lands */
            p->kind = SMK_PROJ_BANANA_AIR;
            /* OURS until a forward throw is recorded: fast and high enough
             * to be SEEN leaving - at the kart's speed plus a shell's it
             * hid under the kart and landed under it (the user: "does not
             * work") */
            p->speed = (int16_t)(k->speed + SMK_PROJ_SPEED_ADD * 2);
            p->zv = 0x0180;
            set_velocity(p);
        } else {
            /* MEASURED: eight pixels behind the kart, and it stays */
            int16_t sx, cy;
            smk_dsp_sincos(heading, 8 * 256, &sx, &cy);
            p->x -= (int32_t)sx << (SMK_POS_SHIFT - 8);
            p->y += (int32_t)cy << (SMK_POS_SHIFT - 8);
        }
        break;
    case SMK_PROJ_GREEN:
    case SMK_PROJ_RED:
        /* MEASURED: the kart's speed + $300, along its heading.  A green
         * shell dropped behind (button + DOWN) is STATIC, eight pixels back
         * like the banana - the user: "I can leave it behind me, static,
         * exactly same as banana" */
        if (backward && kind == SMK_PROJ_GREEN) {
            int16_t sx, cy;
            smk_dsp_sincos(heading, 8 * 256, &sx, &cy);
            p->x -= (int32_t)sx << (SMK_POS_SHIFT - 8);
            p->y += (int32_t)cy << (SMK_POS_SHIFT - 8);
            p->speed = 0;
            break;
        }
        p->speed = (int16_t)(backward ? SMK_PROJ_SPEED_ADD
                                      : k->speed + SMK_PROJ_SPEED_ADD);
        p->delay = SMK_PROJ_RED_DELAY;
        set_velocity(p);
        break;
    default:
        p->kind = SMK_PROJ_NONE;
        break;
    }
}

static uint16_t bearing(int32_t fx, int32_t fy, int32_t tx, int32_t ty)
{
    float dx = (float)(tx - fx), dy = (float)(ty - fy);
    /* heading 0 = north (-y), turning clockwise: the kart convention */
    float a = atan2f(dx, -dy);
    return (uint16_t)(int)(a * (65536.0f / (2.0f * (float)M_PI)));
}

/* $81:9F0A: steer $2A toward the bearing - snap inside the band, else
 * $0400 a frame toward it */
static void home(smk_proj *p, uint16_t want)
{
    int diff = (int16_t)(uint16_t)(want - p->heading);
    if (diff > -SMK_PROJ_RED_BAND && diff < SMK_PROJ_RED_BAND) p->heading = want;
    else p->heading = (uint16_t)(p->heading + (diff < 0 ? -SMK_PROJ_RED_TURN
                                                        :  SMK_PROJ_RED_TURN));
}

/* The characters' weapons (NOTES 190; the user: "every cpu character has
 * their own single power. while mario and Luigi become invincible, the
 * rest have one item that can drop behind, static, or throw ahead.
 * shells for koopa troopa, banana for dk jr, a shrinking mushroom for
 * toad and princess, an egg for yoshi and a fire ball for bowser, that is
 * the only non static item").  SMK_DRIVERS order. */
int smk_ai_weapon_of(int character)
{
    switch (character) {
    case 0: case 1: return SMK_AI_WEAPON_STAR;      /* Mario, Luigi  */
    case 2:         return SMK_PROJ_FIREBALL;       /* Bowser        */
    case 3: case 7: return SMK_PROJ_MUSHROOM;       /* Peach, Toad   */
    case 4:         return SMK_PROJ_BANANA;         /* DK Jr         */
    case 5:         return SMK_PROJ_EGG;            /* Yoshi         */
    case 6:         return SMK_PROJ_GREEN;          /* Koopa         */
    default:        return SMK_AI_WEAPON_NONE;
    }
}

/* MEASURED (tools/labs/mame/objdump.lua on the `attack` session, NOTES
 * 190): the object goes live at the kart's own position and moves at the
 * kart's exact velocity for 58 frames, then stops where it is.  The port
 * keeps it eight pixels behind the kart for those frames (the banana's
 * own offset) and lets go.  Bowser's fireball is thrown ahead instead
 * (OURS: a green shell's launch, no bounce life measured). */
void smk_proj_ai_drop(smk_proj *list, int n, int kind, const smk_kart *k, int owner)
{
    int i = free_slot(list, n);
    if (i < 0) return;
    smk_proj *p = &list[i];
    memset(p, 0, sizeof *p);
    p->kind = kind; p->owner = owner; p->target = -1;
    p->heading = k->angle;
    p->x = k->x; p->y = k->y;
    /* the fireball too: Bowser "puts or throws them but they stay in
     * place and then have a zigzag but only to the left and right" (the
     * user) - carried and let go like the rest, no speed */
    p->carry = SMK_AI_CARRY;
    p->safe = SMK_AI_CARRY + SMK_PROJ_OWNER_SAFE;
}

void smk_proj_ai_throw(smk_proj *list, int n, int kind, const smk_kart *k, int owner)
{
    smk_proj_ai_drop(list, n, kind, k, owner);
    for (int i = 0; i < n; i++)
        if (list[i].kind == kind && list[i].owner == owner && list[i].carry == SMK_AI_CARRY) {
            list[i].carry = SMK_AI_THROW_CARRY;
            list[i].throw_after = true;
            return;
        }
}

/* the release of a forward attack: the port's own forward throw of that
 * kind, from where the object is, along the owner's heading.  MEASURED
 * (NOTES 279): the thrown object leaves at ~6 px a frame and flies ~48
 * frames with a ~10 px peak before parking; the port's thrown-banana
 * arc is OURS and stands until fitted. */
static void ai_release(smk_proj *p, const smk_kart *ok)
{
    smk_kart k = *ok;
    k.x = p->x; k.y = p->y;
    k.speed = 0x0600 - SMK_PROJ_SPEED_ADD * 2;     /* smk_proj_throw adds 2*$300: $600 flat */
    int kind = p->kind, owner = p->owner;
    memset(p, 0, sizeof *p);
    p->kind = kind; p->owner = owner; p->target = -1;
    p->safe = SMK_PROJ_OWNER_SAFE;
    p->heading = ok->angle;
    p->x = k.x; p->y = k.y; p->z = 0;
    switch (kind) {
    case SMK_PROJ_BANANA:
    case SMK_PROJ_MUSHROOM:
    case SMK_PROJ_EGG:
        p->kind = SMK_PROJ_BANANA_AIR;          /* the thrown-banana handler ($F6DB) */
        p->speed = 0x0600;
        p->zv = 0x0180;
        break;
    default:                                    /* the shell family flies flat */
        p->speed = 0x0600;
        break;
    }
    set_velocity(p);
    if (kind != SMK_PROJ_BANANA && kind != SMK_PROJ_MUSHROOM && kind != SMK_PROJ_EGG)
        p->kind = kind;
}

void smk_proj_step(smk_proj *list, int n, const smk_track *trk,
                   const smk_kart *const *karts, int nkarts)
{
    for (int i = 0; i < n; i++) {
        smk_proj *p = &list[i];
        if (p->kind == SMK_PROJ_NONE) continue;
        p->t++;
        if (p->dying) {
            /* $80:F85D: a hop, then gone when it lands */
            p->z += (int32_t)p->zv << 8;
            p->zv = (int16_t)(p->zv - 26);
            if (p->z <= 0) p->kind = SMK_PROJ_NONE;
            continue;
        }
        if (p->carry > 0) {                              /* riding behind its kart */
            p->carry--;
            if (p->owner >= 0 && p->owner < nkarts && karts[p->owner]) {
                const smk_kart *ok = karts[p->owner];
                int16_t sx, cy;
                smk_dsp_sincos(ok->angle, 8 * 256, &sx, &cy);
                p->x = ok->x - ((int32_t)sx << (SMK_POS_SHIFT - 8));
                p->y = ok->y + ((int32_t)cy << (SMK_POS_SHIFT - 8));
                if (p->carry == 0 && p->throw_after) ai_release(p, ok);
            }
            continue;
        }
        if (p->kind == SMK_PROJ_FIREBALL) {
            /* in place, weaving left and right of where it was put:
             * OURS, the width and the period unmeasured */
            int16_t sx, cy;
            smk_dsp_sincos((uint16_t)(p->heading + 0x4000), 256, &sx, &cy);
            float d = (float)SMK_FIRE_WEAVE_AMP * sinf((float)(p->t % SMK_FIRE_WEAVE_T) / (float)SMK_FIRE_WEAVE_T * 6.2831853f);
            p->wx = (int32_t)((float)sx * d) << (SMK_POS_SHIFT - 8);
            p->wy = -((int32_t)((float)cy * d) << (SMK_POS_SHIFT - 8));
            continue;
        }
        if (p->kind == SMK_PROJ_BANANA || p->kind == SMK_PROJ_MUSHROOM
            || p->kind == SMK_PROJ_EGG) continue;         /* $80:F745: sits */
        if (p->kind == SMK_PROJ_GREEN && p->speed == 0) continue;   /* dropped: static */

        if (p->kind == SMK_PROJ_RED) {
            if (p->delay > 0) p->delay--;
            if (p->target >= 0 && p->target < nkarts && karts[p->target]) {
                const smk_kart *tk = karts[p->target];
                home(p, bearing(p->x, p->y, tk->x, tk->y));
                set_velocity(p);
            }
        }
        if (p->kind == SMK_PROJ_BANANA_AIR) {
            p->z += (int32_t)p->zv << 8;
            p->zv = (int16_t)(p->zv - 26);
            if (p->z <= 0 && p->t > 2) {                     /* landed */
                p->z = 0; p->zv = 0; p->vx = p->vy = 0; p->speed = 0;
                p->kind = SMK_PROJ_BANANA;
                continue;
            }
        }

        /* $80:FE07: the integrator, then the wall */
        int32_t nx = p->x + ((int32_t)p->vx << (SMK_POS_SHIFT - 8));
        int32_t ny = p->y + ((int32_t)p->vy << (SMK_POS_SHIFT - 8));
        int px = smk_kart_px(nx), py = smk_kart_px(ny);
        int cx = smk_kart_px(p->x), cy = smk_kart_px(p->y);
        uint8_t here = smk_track_surface(trk, px, py);
        if (here & 0x80) {
            /* MEASURED: a RED shell dies on a wall - $42 = $8000 and the
             * hop, at a class-$80 cell - where a green bounces off it */
            if (p->kind == SMK_PROJ_RED) { p->dying = true; p->zv = SMK_PROJ_DIE_HOP; p->vx = p->vy = 0; continue; }
            /* a wall: reflect the component that ran into it, at 7/8 */
            bool bx = (smk_track_surface(trk, px, cy) & 0x80) != 0;
            bool by = (smk_track_surface(trk, cx, py) & 0x80) != 0;
            if (!bx && !by) bx = by = true;               /* a corner */
            if (bx) p->vx = (int16_t)(-p->vx * SMK_PROJ_BOUNCE_NUM / 8);
            if (by) p->vy = (int16_t)(-p->vy * SMK_PROJ_BOUNCE_NUM / 8);
            p->heading = bearing(0, 0, p->vx, -p->vy);
            if (++p->bounces >= SMK_PROJ_MAX_BOUNCE) { p->dying = true; p->zv = SMK_PROJ_DIE_HOP; }
            p->bounced = 1;             /* the caller makes the sound */
            continue;
        }
        if ((here & 0x20) && p->kind != SMK_PROJ_BANANA_AIR) {
            /* $80:F8C0: a hazard class under it - it hops out */
            p->dying = true; p->zv = SMK_PROJ_DIE_HOP;
            continue;
        }
        p->x = nx; p->y = ny;
    }

    /* TWO OF THEM MEETING.  The user: "two attack items colliding cancel
     * each other (animation is the same as implemented in other actions:
     * they get flipped down and 'fall')" - which is the death this file
     * already has, $80:F85D's hop.  A banana lying on the road is not an
     * attack, so it takes no part; anything moving does.  OURS: the ROM's
     * own object-against-object test is not decoded, so the box is the
     * same SMK_PROJ_HIT_R every other contact uses. */
    for (int i = 0; i < n; i++) {
        smk_proj *a = &list[i];
        if (a->kind == SMK_PROJ_NONE || a->dying || a->carry > 0) continue;
        if (a->kind == SMK_PROJ_BANANA) continue;          /* sitting there */
        for (int j = i + 1; j < n; j++) {
            smk_proj *b = &list[j];
            if (b->kind == SMK_PROJ_NONE || b->dying || b->carry > 0) continue;
            if (b->kind == SMK_PROJ_BANANA) continue;
            int dx = smk_kart_px(a->x + a->wx) - smk_kart_px(b->x + b->wx);
            int dy = smk_kart_px(a->y + a->wy) - smk_kart_px(b->y + b->wy);
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx >= SMK_PROJ_HIT_R || dy >= SMK_PROJ_HIT_R) continue;
            a->dying = b->dying = true;
            a->zv = b->zv = SMK_PROJ_DIE_HOP;
            a->vx = a->vy = b->vx = b->vy = 0;
            break;
        }
    }
}

int smk_proj_hit(smk_proj *list, int n, const smk_kart *k, int kart_index)
{
    for (int i = 0; i < n; i++) {
        smk_proj *p = &list[i];
        if (p->kind == SMK_PROJ_NONE || p->dying) continue;
        if (p->kind == SMK_PROJ_BANANA_AIR) continue;      /* in the air */
        if (p->owner == kart_index && (p->carry > 0 || p->t < p->safe)) continue;
        int dx = smk_kart_px(p->x + p->wx) - smk_kart_px(k->x);
        int dy = smk_kart_px(p->y + p->wy) - smk_kart_px(k->y);
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx >= SMK_PROJ_HIT_R || dy >= SMK_PROJ_HIT_R) continue;
        if (k->z > SMK_BUMP_Z_MAX * 25029) continue;       /* over it */
        int kind = p->kind;
        if (getenv("SMK_ITEM_TRACE"))
            printf("  proj HIT: kind %d owner %d victim %d t %d carry %d safe %d frame %ld\n",
                   kind, p->owner, kart_index, p->t, p->carry, p->safe, smk_race_frame);
        if (kind == SMK_PROJ_BANANA || kind == SMK_PROJ_MUSHROOM || kind == SMK_PROJ_EGG) p->kind = SMK_PROJ_NONE;
        else { p->dying = true; p->zv = SMK_PROJ_DIE_HOP; p->vx = p->vy = 0; }
        return kind;
    }
    return SMK_PROJ_NONE;
}


/* ---- the art (smk.h) ---------------------------------------------------- */
static void tile8(const uint8_t *src, uint8_t *dst64)
{
    memset(dst64, 0, 64);
    for (int pair = 0; pair < 2; pair++) {
        const uint8_t *q = src + pair * 16;
        for (int y = 0; y < 8; y++) {
            uint8_t lo = q[y * 2], hi = q[y * 2 + 1];
            for (int x = 0; x < 8; x++) {
                int bit = 7 - x;
                int v = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
                dst64[y * 8 + x] |= (uint8_t)(v << (pair * 2));
            }
        }
    }
}

bool smk_projart_load(const smk_rom *rom, smk_projart *out)
{
    /* $C4:0594, decompressed by the game itself ($81:E58D) - six ladders
     * of twenty tiles; the largest tier of each is its first four tiles,
     * TL TR BL BR (measured: a thrown green shell drew VRAM $660..$671
     * from sheet tiles 20 21 22 23 in that order, NOTES 273). */
    static uint8_t buf[0x2000];
    memset(out, 0, sizeof *out);
    long n = smk_decompress_into(rom->data, rom->size, smk_snes_to_pc(rom, SMK_PROJART_SRC),
                                 buf, sizeof buf, 0, NULL);
    if (n < 120 * 32) return false;
    for (int k = 0; k < SMK_PROJART_N; k++)
        for (int q = 0; q < 4; q++) {
            uint8_t t[64];
            tile8(buf + (k * 20 + q) * 32, t);
            int ox = (q & 1) * 8, oy = (q >> 1) * 8;
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++)
                    out->px[k][(oy + y) * 16 + ox + x] = t[y * 8 + x];
        }
    out->ok = true;
    return true;
}
