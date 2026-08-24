/* SDL2 host for the Super Mario Kart reimplementation. */
#include "smk.h"

#ifndef SMK_BUILD
#define SMK_BUILD "dev"
#endif

#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* The SNES NTSC frame rate.  Super Mario Kart's main loop runs exactly one
 * step per vblank, so this is the simulation tick, and every duration in the
 * game is a count of these.  Getting it wrong makes everything feel off. */
#define TICK_HZ   60.0988f
#define TICK_DT   (1.0f / TICK_HZ)

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    bool up, down, left, right, shift;
    bool hop_held;
    bool quit;
    /* sticky edges: set by events, cleared only when a tick consumes them */
    bool next_track, prev_track, next_pal, prev_pal, toggle_filter;
    bool hop;
} input_state;

static void input_edges_clear(input_state *in)
{
    in->next_track = in->prev_track = false;
    in->next_pal = in->prev_pal = in->toggle_filter = false;
    in->hop = false;
}

static void pump(input_state *in)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) in->quit = true;
        /* Fold in key events as well as polling: a tap shorter than one
         * iteration is invisible to SDL_GetKeyboardState alone. */
        if (e.type == SDL_KEYDOWN && e.key.repeat == 0) {
            switch (e.key.keysym.sym) {
            case SDLK_ESCAPE: in->quit = true; break;
            case SDLK_RIGHTBRACKET: in->next_track = true; break;
            case SDLK_LEFTBRACKET:  in->prev_track = true; break;
            case SDLK_p: in->next_pal = true; break;
            case SDLK_o: in->prev_pal = true; break;
            case SDLK_f: in->toggle_filter = true; break;
            case SDLK_SPACE: in->hop = true; break;
            default: break;
            }
        }
    }
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    in->up    = k[SDL_SCANCODE_UP]    || k[SDL_SCANCODE_W];
    in->down  = k[SDL_SCANCODE_DOWN]  || k[SDL_SCANCODE_S];
    in->left  = k[SDL_SCANCODE_LEFT]  || k[SDL_SCANCODE_A];
    in->right = k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D];
    in->shift = k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT];
    in->hop_held = k[SDL_SCANCODE_SPACE];
}

static const smk_course *course_for_step;
static int player_slip_deg;
static int player_height_px;

static void collide_objects(smk_kart *k, const smk_course *crs)
{
    /* NOTES 075: the per-track object list ($85:D000) holds GROUND
     * features only - item boxes, coins, oil - all stamped into the
     * tilemap and none solid.  The live entities (pipes, moles, Lakitu)
     * come from a separate spawn system (the $1800 object blocks) that is
     * not decoded yet, so right now there is nothing to collide with.
     *
     * The MEASURED pipe response, kept for when that system lands
     * (crash lab, NOTES 072): velocity REFLECTS about the contact
     * normal, both components halve, speed scales 308/581, and a
     * 10-frame ballistic window follows (bounce_cool).  */
    (void)k; (void)crs;
}

/* ------------------------------------------------------------------ */
/* Debug HUD: a speedometer drawn straight into the framebuffer.
 * 4x6 digit font, plus the letters needed for its labels. */
static const uint8_t FONT4x6[16][6] = {
    {0x6,0x9,0x9,0x9,0x9,0x6},{0x2,0x6,0x2,0x2,0x2,0x7},   /* 0 1 */
    {0x6,0x9,0x1,0x2,0x4,0xF},{0x6,0x9,0x2,0x1,0x9,0x6},   /* 2 3 */
    {0x1,0x3,0x5,0x9,0xF,0x1},{0xF,0x8,0xE,0x1,0x9,0x6},   /* 4 5 */
    {0x6,0x8,0xE,0x9,0x9,0x6},{0xF,0x1,0x2,0x2,0x4,0x4},   /* 6 7 */
    {0x6,0x9,0x6,0x9,0x9,0x6},{0x6,0x9,0x9,0x7,0x1,0x6},   /* 8 9 */
    {0x6,0x9,0xF,0x9,0x9,0x9},{0xE,0x9,0xE,0x9,0x9,0xE},   /* A B */
    {0x6,0x9,0x8,0x8,0x9,0x6},{0xE,0x9,0x9,0x9,0x9,0xE},   /* C D */
    {0xF,0x8,0xE,0x8,0x8,0xF},{0xF,0x8,0xE,0x8,0x8,0x8},   /* E F */
};

static void hud_glyph(uint32_t *fb, int rw, int rh, int x, int y,
                      int g, uint32_t col, int sc)
{
    if (g < 0 || g > 15) return;
    for (int r = 0; r < 6; r++)
        for (int c2 = 0; c2 < 4; c2++)
            if (FONT4x6[g][r] & (8 >> c2))
                for (int dy = 0; dy < sc; dy++)
                    for (int dx = 0; dx < sc; dx++) {
                        int px = x + c2 * sc + dx, py = y + r * sc + dy;
                        if (px >= 0 && px < rw && py >= 0 && py < rh)
                            fb[py * rw + px] = col;
                    }
}

static void hud_number(uint32_t *fb, int rw, int rh, int x, int y,
                       int v, int digits, uint32_t col, int sc)
{
    for (int i = digits - 1; i >= 0; i--) {
        hud_glyph(fb, rw, rh, x + i * 5 * sc, y, v % 10, col, sc);
        v /= 10;
    }
}

static void hud_hex2(uint32_t *fb, int rw, int rh, int x, int y,
                     int v, uint32_t col, int sc)
{
    hud_glyph(fb, rw, rh, x, y, (v >> 4) & 15, col, sc);
    hud_glyph(fb, rw, rh, x + 5 * sc, y, v & 15, col, sc);
}

static void draw_speedo(uint32_t *fb, int rw, int rh,
                        const smk_kart *k, uint8_t surf, int top)
{
    int sc = rw >= 640 ? 2 : 1;
    int x = 8, y = rh - 10 * sc - 8;
    int frac = smk_surface_cap_frac(surf);
    int cap = frac >= 1000 ? top : (top * frac) / 1000;
    bool capped = k->speed >= cap - 8 && frac < 1000;
    uint32_t col = capped ? 0xFFFFA030 : 0xFFFFFFFF;

    hud_number(fb, rw, rh, x, y, k->speed < 0 ? 0 : k->speed, 4, col, sc);
    hud_hex2(fb, rw, rh, x + 24 * sc, y, surf, 0xFF90C8FF, sc);
    {   /* slip angle in degrees - sliding is visible as a number */
        float va = atan2f((float)k->vx, -(float)k->vy);
        float ha = (float)k->angle * (float)(2.0 * M_PI) / 65536.0f;
        float d = va - ha;
        while (d >  (float)M_PI) d -= 2.0f * (float)M_PI;
        while (d < -(float)M_PI) d += 2.0f * (float)M_PI;
        int deg = (int)(fabsf(d) * 180.0f / (float)M_PI);
        uint32_t sc2 = deg > 20 ? 0xFFFF5050 : deg > 5 ? 0xFFFFA030
                                             : 0xFF808088;
        hud_number(fb, rw, rh, x + 38 * sc, y, deg > 99 ? 99 : deg, 2, sc2, sc);
    }

    /* bar: speed vs this class's top, cap marked */
    int bx = x, by = y + 7 * sc, bw = 60 * sc, bh = 3 * sc;
    for (int i = 0; i < bw; i++) {
        int filled = (k->speed > 0) && (i * top < k->speed * bw);
        uint32_t c2 = filled ? col : 0xFF404048;
        for (int j = 0; j < bh; j++)
            if (by + j < rh && bx + i < rw)
                fb[(by + j) * rw + bx + i] = c2;
    }
    int capx = bx + (cap * bw) / (top ? top : 1);
    for (int j = -1; j < bh + 1; j++)
        if (by + j >= 0 && by + j < rh && capx < rw)
            fb[(by + j) * rw + capx] = 0xFFFF5050;
}

/* ------------------------------------------------------------------ */
/* Opponents: drive the game's own racing line.
 *
 * The DATA is the ROM's - sector map, waypoints, acceleration tables - and
 * the steering LAW matches the decoded shape: the AI aims at the waypoint
 * of the sector ahead ($80B0B1: waypoint minus position into atan2) and the
 * heading slews toward that target, snapping when close ($80AFBE).
 * PLACEHOLDER values, marked: the slew rate, the target-speed entry per
 * kart, and rubber-banding (none).  Lap counting below is ours too: the
 * ROM's crossing test is not decoded, so we count a lap when a kart on the
 * finish strip has come around through the back half of the course.
 */
typedef struct {
    smk_kart k;
    int      sector;        /* last on-course sector                    */
    int      lap;
    int      progress_max;  /* $F8,x: max of (lap<<8)|sector, monotonic */
    int      slow_frames;   /* stuck-at-a-wall recovery counter         */
    int      escape;        /* frames left of hold-heading wall escape  */
    int      was_fast;      /* escape only after the kart has driven    */
    int      last_px, last_py, still;   /* position-stagnation detector  */
    int      esc_len;       /* escalating escape duration               */
    int      no_prog;       /* frames since monotonic progress          */
    int      rescue_max;    /* rescue timer's own progress watermark    */
    int      lap_cool;      /* one lap event per strip transit          */
} smk_racer;

static void racer_start(smk_racer *r, const smk_course *crs, int slot)
{
    float x, y;
    uint16_t heading;
    memset(r, 0, sizeof *r);
    smk_course_start(crs, slot, &x, &y, &heading);
    r->k.x = (int32_t)(x * SMK_POS_ONE);
    r->k.y = (int32_t)(y * SMK_POS_ONE);
    r->k.angle = heading;
    r->sector = crs->sectors - 1;         /* the grid sits in the last sector */
}

static uint16_t heading_to(const smk_kart *k, int tx, int ty)
{
    float dx = (float)(tx - smk_kart_px(k->x));
    float dy = (float)(ty - smk_kart_px(k->y));
    /* game convention: 0 = -Y, clockwise */
    return (uint16_t)(atan2f(dx, -dy) * (float)SMK_ANGLE_TURN
                      / (2.0f * (float)M_PI));
}

#define AI_SNAP      0x0200      /* $80AFBE snaps inside this            */

static void racer_step(smk_racer *r, const smk_track *trk,
                       const smk_course *crs, const smk_physics *phys)
{
    uint8_t cell = smk_course_cell(crs, smk_kart_px(r->k.x), smk_kart_px(r->k.y));
    int sec = cell & SMK_SECT_OFF;
    /* DECODED ($808962): keep the old sector when off-course ($7F), and
     * while airborne reject sectors whose waypoint attribute has bit 7 set
     * - the anti-shortcut rule for jump zones. */
    if (sec != SMK_SECT_OFF && sec < crs->sectors
        && !(r->k.airborne && (crs->wattr[sec] & 0x80))) {
        /* DECODED ($8089B6/$8089ED): the lap lives in the high byte of the
         * kart's progress word - crossing the line forward does
         * `+$0100, and #$FF00`; crossing backward subtracts it; and $F8,x
         * keeps the maximum progress so a lap only counts when it exceeds
         * everything seen before.  We keep lap and sector as fields and
         * apply the same wrap and guard. */
        /* the crossing only counts ON the strip ($808994 is called from
         * the strip-accept path), forward guarded by max progress.  The
         * strip holds paint of BOTH ends of the loop, so the sector can
         * oscillate across one transit; without a cooldown that fired
         * +1 then an unguarded -1 and left the counter locked (NOTES 055).
         * One lap event per transit. */
        if (r->lap_cool > 0) r->lap_cool--;
        if ((cell & SMK_SECT_FINISH) && r->lap_cool == 0) {
            if (r->sector != sec) r->esc_len = 0;
        /* progress for the rescue timer = monotonic max only, or the two
         * stuck loops that oscillate between adjacent sectors reset it */
        {
            int prog2 = (r->lap << 8) | sec;
            if (prog2 > r->rescue_max) { r->rescue_max = prog2; r->no_prog = 0; }
        }
            if (r->sector >= crs->sectors - 2 && sec <= 1) {
                int prog = ((r->lap + 1) << 8) | sec;
                if (prog > r->progress_max) {
                    r->lap++;
                    r->progress_max = prog;
                    r->lap_cool = 90;
                }
            } else if (sec >= crs->sectors - 2 && r->sector <= 1) {
                r->lap--;
                r->lap_cool = 90;
            }
        }
        r->sector = sec;
    }

    /* DECODED steering ($80B0B1 / NOTES 056): on course the AI's target
     * angle is the flow field byte for its cell - atan2 to a waypoint is
     * only the OFF-COURSE recovery path in the ROM, and treating it as the
     * main rule was why our karts clipped corners into walls. */
    int fcell = ((smk_kart_px(r->k.y) >> 4) & 63) * 64
              + ((smk_kart_px(r->k.x) >> 4) & 63);
    int fsec = crs->map[fcell] & SMK_SECT_OFF;
    uint16_t want;
    if (fsec != SMK_SECT_OFF && crs->map[fcell] != 0) {
        want = (uint16_t)(crs->flow[fcell] << 8);
    } else {
        int next = r->sector + 1;
        if (next >= crs->sectors) next = 0;
        want = heading_to(&r->k, crs->wx[next], crs->wy[next]);
    }
    /* Stuck against a sticky wall (labelled AI behaviour, not a decode):
     * the ROM's karts bounce free, ours stop - so scan eight compass
     * directions for the most open ground, take it, and HOLD it briefly;
     * without the hold the slew dragged the kart straight back into the
     * wall before it could move (NOTES 057). */
    if (r->k.speed > 300) r->was_fast = 1;
    /* Lakitu: the game fishes a stuck or fallen kart back onto the track.
     * Ten seconds without sector progress -> set down at the sector's own
     * waypoint, facing the next one.  (The real trigger and animation are
     * not decoded; the rescue itself is the game's own behaviour.) */
    if (++r->no_prog > 600) {
        int nx2 = r->sector + 1;
        if (nx2 >= crs->sectors) nx2 = 0;
        r->k.x = (int32_t)crs->wx[r->sector] << 16;
        r->k.y = (int32_t)crs->wy[r->sector] << 16;
        r->k.angle = heading_to(&r->k, crs->wx[nx2], crs->wy[nx2]);
        r->k.speed = 0;
        r->k.vx = r->k.vy = 0;
        r->k.airborne = false;
        r->no_prog = 0;
        r->esc_len = 0;
        r->escape = 0;
    }
    /* a kart pinned nearly square against a wall keeps its speed (the
     * proportional graze loss is ~0) while its position only crawls
     * sub-pixel - so stagnation, not low speed, is the reliable trigger */
    {
        int px = smk_kart_px(r->k.x), py = smk_kart_px(r->k.y);
        if (px == r->last_px && py == r->last_py) r->still++;
        else { r->still = 0; r->last_px = px; r->last_py = py; }
    }
    if (r->escape > 0) {
        r->escape--;
    } else if (((r->k.speed < 100 && r->was_fast) || r->still > 40)) {
        r->slow_frames += (r->still > 40) ? 31 : 1;
        if (r->slow_frames > 30) {
            int best_d = -1, best_score = -1000;
            for (int d = 0; d < 8; d++) {
                float a = (float)d * (float)M_PI / 4.0f;
                int open = 0;
                static const int STEPS[7] = { 2, 4, 8, 16, 24, 32, 40 };
                for (int si = 0; si < 7; si++) {
                    int step = STEPS[si];
                    int sx = smk_kart_px(r->k.x) + (int)(sinf(a) * step);
                    int sy = smk_kart_px(r->k.y) - (int)(cosf(a) * step);
                    if (smk_surface_solid(smk_track_surface(trk, sx, sy)))
                        break;
                    open++;
                }
                /* prefer open ground, break ties toward the flow direction
                 * so the escape makes forward progress */
                int16_t da = (int16_t)((uint16_t)(d * 0x2000) - want);
                int align = 4 - (abs((int)da) >> 12);       /* 4..-4 */
                int score = open * 8 + align;
                if (score > best_score) { best_score = score; best_d = d; }
            }
            r->k.angle = (uint16_t)(best_d * 0x2000);
            r->k.speed = 300;
            /* escalate on consecutive triggers: deep pockets need longer
             * runs before the flow field is allowed to pull again */
            r->esc_len = r->esc_len ? (r->esc_len * 2 > 120 ? 120
                                       : r->esc_len * 2) : 25;
            r->escape = r->esc_len;
            r->slow_frames = 0;
            if (r->still > 120) {
                /* wedged in a concave notch: no heading can move it, so
                 * step the position out directly (labelled last resort) */
                float ea = (float)best_d * (float)M_PI / 4.0f;
                r->k.x += (int32_t)(sinf(ea) * 3.0f * SMK_POS_ONE);
                r->k.y -= (int32_t)(cosf(ea) * 3.0f * SMK_POS_ONE);
                r->still = 0;
            }
        }
    } else
        r->slow_frames = 0;
    int16_t diff = (int16_t)(want - r->k.angle);
    if (r->escape > 0) diff = 0;             /* hold the escape heading */
    if (diff > AI_SNAP || diff < -AI_SNAP) {
        uint16_t err = (uint16_t)(diff > 0 ? diff : -diff);
        /* DECODED ($80AFF9): turn amount from the physics blob's words 32+,
         * indexed by heading error; the demo AI uses row 8 ($C8 = 8).
         * MEASURED (NOTES 043): above ~90 degrees of error the AI turns at
         * $800 per frame - a fast turnaround, not a table step. */
        uint16_t step = err > 0x4000 ? 0x800 : smk_physics_turn(phys, err, 8);
        r->k.angle += (uint16_t)(diff > 0 ? step : -(int)step);
    } else {
        r->k.angle = want;
    }

    /* DECODED ($80B074): the target speed row is selected by the sector
     * waypoint attribute's low two bits, offset by the kart's $C8 row.
     * The attract demo runs its AI at row +4 (700-1050), which outruns the
     * player at every engine class (user report) - presumably the demo's
     * difficulty, with rubber-banding undecoded.  Race AI uses row +0, the
     * same rows the player's class selects, so 50/100/150cc scale both. */
    int target = (int16_t)phys->w[SMK_PHYS_TARGET + (crs->wattr[r->sector] & 3)];
    /* DECODED ($80A701 structure): off-road surfaces cap the speed and the
     * over-cap decel row applies.  Cap values are measured (NOTES 053). */
    {
        uint8_t sv = smk_track_surface(trk, smk_kart_px(r->k.x),
                                       smk_kart_px(r->k.y));
        /* the real AI ignores surfaces (rubber-band cheat, NOTES 057);
         * we apply a softened measured cap so the field stays honest but
         * competitive - labelled behaviour */
        int frac = smk_surface_cap_frac(sv);
        if (frac < 800) {
            int cap = (int)phys->w[SMK_PHYS_TARGET + 3] * (frac + 200) / 1000;
            if (target > cap) target = cap;
        }
    }
    int32_t accel;
    if (r->k.speed < target)
        accel = (int32_t)smk_physics_accel(phys, r->k.speed) << 8;
    else
        accel = -((int32_t)0x0400 << 8);
    r->k.accel = (int16_t)(accel >> 16);
    r->k.accel_frac = (uint16_t)(accel & 0xFFFF);
    smk_kart_accelerate(&r->k);
    if (r->k.speed > target) r->k.speed = (int16_t)target;
    smk_kart_face(&r->k);
    smk_kart_gravity(&r->k);
    smk_kart_move(&r->k, trk);
    collide_objects(&r->k, crs);
}

/* ------------------------------------------------------------------ */
/* Turning input into speed and heading.
 *
 * PLACEHOLDER (ledger S1).  The motion itself - velocity from (sin, -cos) *
 * speed, and position += velocity << 8 - is the ROM's, in src/kart.c.  What
 * is invented is only how the *player* drives those two numbers: the ROM's
 * acceleration curve, drift, hop and per-surface response are still
 * undecoded, so these constants are chosen to feel reasonable and nothing
 * more.  They are in the game's units so that decoding them later is a
 * substitution, not a rewrite.
 */
/* The acceleration curve and the target speeds are the ROM's, read from it
 * at runtime (src/physics.c).  What is still invented, and marked as such
 * in the ledger, is the *policy*: which target speed the player's input
 * selects, the braking rate, and the steering rate.  The ROM picks its
 * target from per-character stats we have not decoded, and its steering is
 * a slew toward a target angle at $FA,x ($80AFBE). */
#define FEEL_TARGET_IDX   3        /* which entry of the ROM target table  */
#define FEEL_BRAKE   (0x2000)
#define FEEL_DRAG    (0x0400)
#define FEEL_TURN    420           /* angle units per frame                */

static void step_kart(smk_kart *k, const smk_track *trk,
                      const smk_physics *phys, const input_state *in)
{
    int top = (int16_t)phys->w[SMK_PHYS_TARGET + FEEL_TARGET_IDX];
    int target = 0;
    if (in->up)   target = in->shift ? top + (top >> 2) : top;
    if (in->down) target = -top / 2;

    /* Drive the ROM's acceleration fields, not speed directly. */
    /* off-track slows the kart: per-surface cap with the ROM's over-cap
     * decel row ($80A65D) and coasting drag ($80A590).  Cap values are the
     * labelled placeholders until measured (NOTES 048/053). */
    uint8_t surf = smk_track_surface(trk, smk_kart_px(k->x), smk_kart_px(k->y));
    /* MEASURED cap (NOTES 066): fraction of road speed, scaled by this
     * engine class's own top so 50/100/150cc keep the ROM's ratios */
    int frac = smk_surface_cap_frac(surf);
    int cap = (frac < 1000) ? (top * frac) / 1000 : 0;
    if (cap && target > cap) target = cap;
    int32_t accel;
    if (k->speed < target)
        accel = (int32_t)smk_physics_accel(phys, k->speed) << 8;   /* $80B043 */
    else if (k->speed > target) {
        /* over the surface cap: the MEASURED per-class deceleration
         * (NOTES 067) - a firm drag down to the cap, as the ROM does it */
        int dec = (cap && k->speed > cap)
                  ? (int)smk_surface_decel(surf) << 8
                  : (target == 0 ? FEEL_DRAG : FEEL_BRAKE);
        accel = -(int32_t)dec << 8;
    } else
        accel = 0;
    k->accel      = (int16_t)(accel >> 16);
    k->accel_frac = (uint16_t)(accel & 0xFFFF);

    smk_kart_accelerate(k);      /* the ROM's 32-bit speed integration */

    if (k->speed > target && target > 0) { k->speed = (int16_t)target; }

    /* slip, computed up front: it gates steering authority (measured:
     * in a plow the ROM's turn rate collapses from ~307 to ~20/frame) */
    float slip_now;
    {
        float va0 = atan2f((float)k->vx, -(float)k->vy);
        float ha0 = (float)k->angle * (float)(2.0 * M_PI) / 65536.0f;
        slip_now = va0 - ha0;
        while (slip_now >  (float)M_PI) slip_now -= 2.0f * (float)M_PI;
        while (slip_now < -(float)M_PI) slip_now += 2.0f * (float)M_PI;
    }
    float slip_u0 = fabsf(slip_now) * 65536.0f / (2.0f * (float)M_PI);
    player_slip_deg = (int)(fabsf(slip_now) * 180.0f / (float)M_PI);

    /* steering authority falls off as the kart slows, as it must */
    int auth = (k->speed < 0 ? -k->speed : k->speed);
    if (auth > top) auth = top;
    int turn = top ? FEEL_TURN * auth / top : 0;
    if (slip_u0 > 4000.0f)
        turn = turn * 6 / 100;               /* measured plow: ~-20 vs -307 */
    if (in->left)  k->angle -= (uint16_t)turn;
    if (in->right) k->angle += (uint16_t)turn;

    /* Hop: the decoded launch ($80B69D - zvel $0080, needs speed).  A hop
     * into a held turn starts a power slide. */
    if (in->hop && !k->airborne && k->speed >= 0x100)
        smk_kart_launch(k, SMK_HOP_VEL);

    /* Grip.  smk_kart_face() gives the ROM's (sin,-cos)*speed - full grip.
     * The real kart's velocity LAGS its heading (the drift measurement in
     * NOTES 055 showed ~13 degrees of slip with the shoulder held), so
     * blend toward the facing direction instead of snapping: PLACEHOLDER
     * grip constants, labelled, pending the drift-state decode. */
    {
        int32_t tvx = (int32_t)(sinf((float)k->angle * (float)(2.0 * M_PI)
                                     / 65536.0f) * (float)k->speed);
        int32_t tvy = (int32_t)(-cosf((float)k->angle * (float)(2.0 * M_PI)
                                      / 65536.0f) * (float)k->speed);
        /* Per-surface grip by 16-type (NOTES 060).  The COMPOSITION is the
         * ROM's - per-theme class arrays select the type - and the ice
         * types (11/12: classes $56/$58, the ice-theme roads) are the ones
         * its decel table singles out as near-frictionless.  The grip
         * VALUES are labelled placeholders pending the $AA slip-machine
         * decode: road full grip, ice low, off-road in between. */
        /* MEASURED slip dynamics (NOTES 068, both grip batteries):
         *   - steady cornering slip is ~200-310 units at the saturated
         *     turn rate, on EVERY class - convergence ~0.5/frame;
         *   - breakaway is by LATERAL ACCELERATION (speed x turn rate):
         *     950x307 breaks away, 770x307 and 585x307 hold, so the
         *     limit sits near 250k unit^2; past it slip grows ~130/frame
         *     and steering authority collapses - a progressive plow, no
         *     threshold switch;
         *   - slip recovers at ~150/frame below the limit;
         *   - the drift state ($E2: $8000 hop -> $8004 slide -> $8024
         *     charged) is entered by hopping into a held turn: airborne
         *     grip is near zero, and landing steered holds the slide.
         * Ice (types 11/12) keeps a labelled low-grip multiplier - those
         * classes are absent from the demo theme and unmeasured. */
        float va = atan2f((float)k->vx, -(float)k->vy);
        float ha = (float)k->angle * (float)(2.0 * M_PI) / 65536.0f;
        float slip = va - ha;
        while (slip >  (float)M_PI) slip -= 2.0f * (float)M_PI;
        while (slip < -(float)M_PI) slip += 2.0f * (float)M_PI;
        float slip_u = fabsf(slip) * 65536.0f / (2.0f * (float)M_PI);

        int ty = smk_surface_type(surf);
        float class_grip = (ty == 11 || ty == 12) ? 0.35f : 1.0f;

        /* The breakaway limit was MEASURED at the demo's speed scale
         * (top ~951, limit ~250k -> breakaway at ~86% of top under full
         * lock).  Shipped as an absolute it was unreachable at 50cc -
         * playtest: "no difference" - so it scales by the class top,
         * preserving the measured ratio across 50/100/150cc. */
        float limit = (float)top * 264.0f;
        float lateral = (float)k->speed * 307.0f *
                        ((in->left || in->right) ? 1.0f : 0.3f);
        float g;
        if (k->airborne)
            g = 0.04f;                        /* hop: momentum carries    */
        else if (in->hop_held && k->speed > 300)
            g = 0.10f;                        /* held slide (drift)       */
        else if (lateral > limit || slip_u > 4000.0f)
            g = 0.02f;                        /* plow: slip grows, as
                                                 measured (+130/frame) -
                                                 0.08 equilibrated and felt
                                                 like grip (playtest) */
        else
            g = 0.50f * class_grip;           /* measured convergence     */
        k->vx = (int16_t)(k->vx + (float)(tvx - k->vx) * g);
        k->vy = (int16_t)(k->vy + (float)(tvy - k->vy) * g);
    }
    smk_kart_move(k, trk);       /* the ROM's position += velocity << 8 */
    if (course_for_step) collide_objects(k, course_for_step);
    player_height_px = smk_kart_height_px(k);
}

/* The ROM's angle is 0 = -Y increasing clockwise; the renderer wants
 * radians with 0 = +X, and (cos, sin) must equal (sin a, -cos a). */
static void camera_from_kart(smk_camera *cam, const smk_kart *k)
{
    cam->x = (float)k->x / (float)SMK_POS_ONE;
    cam->y = (float)k->y / (float)SMK_POS_ONE;
    cam->angle = (float)k->angle * (2.0f * (float)M_PI / (float)SMK_ANGLE_TURN)
                 - (float)M_PI / 2.0f;
}


/* Everything drawn on top of the ground plane.  Shared by the interactive
 * loop and --shot so the two cannot drift apart - they already did once. */
static void draw_scene(const smk_rom *rom, const smk_track *trk,
                       const smk_sprites *karts, const smk_driver *drv,
                       const smk_camera *cam, uint32_t *fb, int rw, int rh,
                       int show_grid, int show_kart, int frame,
                       uint16_t cam_heading, const smk_racer *racers,
                       const smk_course *course)
{
    /* Track objects render through the GROUND: every object-list entry
     * (boxes, coins, oil) is stamped into the tilemap at load, exactly
     * like the game does ($84F1A4).  Sprite obstacles (pipes, moles)
     * belong to the undecoded $1800 entity system - nothing extra to
     * draw here yet (NOTES 075). */
    (void)course;

    if (show_grid && karts->frames && racers) {
        static smk_sprites other[SMK_CHARACTERS];
        static bool loaded[SMK_CHARACTERS];
        for (int k = 1; k < SMK_CHARACTERS; k++) {
            float px, py, sc;
            float gx = (float)smk_kart_px(racers[k].k.x);
            float gy = (float)smk_kart_px(racers[k].k.y);
            if (!smk_project(cam, gx, gy, rw, rh, &px, &py, &sc)) continue;
            /* MEASURED scaling (NOTES 072): the original never scales the
             * sprite continuously - the OAM canvas stays 32x32 (1/8 of
             * screen width) across the whole near/mid range, apparent size
             * stepping through the ART TIERS inside that canvas, with one
             * switch to 16x16 far out and a cull beyond.  Depth thresholds
             * for the tier steps are estimates pending richer upload data
             * (labelled); the constant-canvas behaviour is the measured
             * part. */
            float a2 = (float)cam_heading * (float)(2.0 * M_PI) / 65536.0f;
            float depth = (gx - cam->x) * sinf(a2)
                        + (gy - cam->y) * -cosf(a2);
            if (depth > 320.0f) continue;                 /* cull        */
            int scale = rw / 256;
            if (scale < 1) scale = 1;
            if (depth > 224.0f) scale = (scale + 1) / 2; /* 16px switch */
            const smk_driver *d2 = &SMK_DRIVERS[k];
            if (!loaded[k]) loaded[k] = smk_sprites_load(rom, d2->sheet, &other[k]);
            if (!loaded[k]) continue;
            int tier = depth < 96.0f  ? SMK_SPR_TIER0
                     : depth < 160.0f ? SMK_SPR_TIER1 : SMK_SPR_TIER2;
            bool hf = false;
            uint16_t rel = (uint16_t)(racers[k].k.angle - cam_heading);
            int f = smk_sprite_for_heading(tier, rel, &hf);
            /* height lifts the sprite on screen, scaled like everything else */
            py -= (float)smk_kart_height_px(&racers[k].k) * sc;
            smk_draw_sprite(&other[k], f, trk->palette,
                            d2->pal, (int)px, (int)py, scale, hf, fb, rw, rh, rw);
        }
    }
    if (show_kart && karts->frames) {
        int scale = rw / 256;                 /* the SNES 32px proportion */
        if (scale < 1) scale = 1;
        bool hf = frame < 0;
        /* the hop lifts the sprite; the shadow stays on the ground */
        int lift = player_height_px * scale;
        smk_draw_sprite(karts, hf ? -frame : frame, trk->palette, drv->pal,
                        rw / 2, rh - rh / 12 - lift, scale, hf, fb, rw, rh, rw);
    }
}

/* The player's own view angle.
 *
 * The frame rule itself is measured (NOTES 041); what is still invented is
 * the INPUT to it for the player's kart: in the ROM the camera lags the
 * kart through a turn, and that lag is the relative heading the rule sees.
 * Our camera tracks the kart exactly, so we synthesise a small lag from the
 * steering input.  Encoded as negative-for-hflip in one int. */
static int frame_for(const input_state *in, float *lean)
{
    /* Player frames (measured + visual identification, NOTES 073):
     * frame 2 is the straight rear view (frame 1 is visibly turned - the
     * "starts turning right" report), frame 1/hflip the normal steering
     * pose, and 47/hflip the slide/oversteer pose the uploads showed. */
    float want = (in->left ? -1.0f : 0.0f) + (in->right ? 1.0f : 0.0f);
    if (want != 0.0f)
        *lean += (want - *lean) * 0.2f;
    else
        *lean *= 0.7f;
    bool sliding = in->hop_held && want != 0.0f;
    if (sliding || player_slip_deg > 12)
        return (*lean < 0 || (want < 0)) ? -47 : 47;
    if (*lean <= -0.4f) return -1;
    if (*lean >= 0.4f) return 1;
    return 2;
}

/* ------------------------------------------------------------------ */
static void usage(const char *argv0)
{
    printf("usage: %s [options]\n"
           "  --rom PATH      Super Mario Kart (USA) ROM   [rom/smk_usa.sfc]\n"
           "  --track N       0..23  (20 courses + 4 battle arenas)\n"
           "  --theme N       override the course theme    [from ROM]\n"
           "  --class N       engine class 0/1/2 (50/100/150cc)  [0]\n"
           "  --character N   0 Mario 1 Luigi 2 Bowser 3 Peach 4 DK Jr\n"
           "                  5 Yoshi 6 Koopa 7 Toad              [0]\n"
           "  --no-kart       hide the player's kart\n"
           "  --no-grid       hide the rest of the starting grid\n"
           "  --width W       window width                 [1024]\n"
           "  --height H      window height                [896]\n"
           "  --pixel N       render at 1/N resolution     [2]\n"
           "  --fullscreen\n"
           "  --frames N      run N frames then exit (benchmark)\n"
           "  --shot PATH     render one frame to a BMP and exit\n"
           "  --dump PATH     write map+tiles+palette and exit (verification)\n"
           "  --at X Y DEG    camera placement for --shot\n"
           "  --height-cam H  eye height above the plane   [15]\n"
           "  --horizon F     horizon row, 0..1            [0.36]\n"
           "  --fov F         focal length scale           [0.55]\n\n"
           "  arrows/WASD steer and accelerate, shift = boost\n"
           "  space = hop; hold it through a turn to power slide\n"
           "  [ ] change track, o p override the theme\n"
           "  f toggles linear filtering, esc quits\n", argv0);
}

int main(int argc, char **argv)
{
    const char *rom_path = "rom/smk_usa.sfc";
    int track = 0, theme = -1;   /* -1 = use the ROM's own binding */
    int engine_class = 0;        /* 0 = 50cc, 1 = 100cc, 2 = 150cc  */
    int character = 0;           /* index into SMK_DRIVERS */
    int show_kart = 1;
    int show_grid = 1;
    int win_w = 1024, win_h = 896, pixel = 2, fullscreen = 0;
    const char *dump = NULL;          /* write raw track data and exit      */
    const char *shot = NULL;          /* render one frame to a BMP and exit */
    float shot_x = 512, shot_y = 512, shot_a = 0;
    int have_at = 0;
    long max_frames = 0;              /* >0: run headless for N frames, then exit */
    float cam_height = 15.0f, cam_horizon = 0.36f, cam_fov = 0.55f;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        #define ARG(name, var) if (!strcmp(a, name) && i + 1 < argc) { var = atoi(argv[++i]); continue; }
        if (!strcmp(a, "--rom") && i + 1 < argc) { rom_path = argv[++i]; continue; }
        ARG("--track", track) ARG("--theme", theme) ARG("--class", engine_class)
        ARG("--character", character)
        if (!strcmp(a, "--no-kart")) { show_kart = 0; continue; }
        if (!strcmp(a, "--no-grid")) { show_grid = 0; continue; }
        ARG("--width", win_w) ARG("--height", win_h) ARG("--pixel", pixel)
        if (!strcmp(a, "--fullscreen")) { fullscreen = 1; continue; }
        if (!strcmp(a, "--frames") && i + 1 < argc) { max_frames = atol(argv[++i]); continue; }
        if (!strcmp(a, "--shot") && i + 1 < argc) { shot = argv[++i]; continue; }
        if (!strcmp(a, "--dump") && i + 1 < argc) { dump = argv[++i]; continue; }
        #define FARG(name, var) if (!strcmp(a, name) && i + 1 < argc) { var = (float)atof(argv[++i]); continue; }
        FARG("--height-cam", cam_height) FARG("--horizon", cam_horizon) FARG("--fov", cam_fov)
        #undef FARG
        if (!strcmp(a, "--at") && i + 3 < argc) {
            shot_x = (float)atof(argv[++i]);
            shot_y = (float)atof(argv[++i]);
            shot_a = (float)atof(argv[++i]) * (float)M_PI / 180.0f;
            have_at = 1;
            continue;
        }
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(argv[0]); return 0; }
        fprintf(stderr, "unknown option '%s'\n", a);
        usage(argv[0]);
        return 2;
        #undef ARG
    }
    if (pixel < 1) pixel = 1;

    char err[512] = {0};
    smk_rom rom;
    if (!smk_rom_load(&rom, rom_path, err, sizeof err)) {
        fprintf(stderr,
            "error: %s\n\n"
            "This program needs your own copy of Super Mario Kart (USA).\n"
            "No game data is distributed with it.  Expected sha1 %s\n"
            "Pass a path with --rom.\n", err, SMK_SHA1_USA);
        return 1;
    }
    if (!rom.recognised)
        fprintf(stderr, "warning: %s\ncontinuing anyway; assets may be wrong.\n\n", err);

    if (character < 0 || character >= SMK_CHARACTERS) character = 0;
    const smk_driver *drv = &SMK_DRIVERS[character];
    static smk_sprites karts;
    if (!smk_sprites_load(&rom, drv->sheet, &karts))
        fprintf(stderr, "warning: kart sprites did not load\n");

    static smk_physics phys;
    if (!smk_physics_load(&rom, engine_class, &phys)) {
        fprintf(stderr, "error: cannot load physics tables\n");
        return 1;
    }
    static smk_course crs;
    if (!smk_course_load(&rom, track, &crs)) {
        fprintf(stderr, "error: cannot load course data for track %d\n", track);
        return 1;
    }
    static smk_track trk;
    if (!smk_track_load(&rom, track, theme, &trk, err, sizeof err)) {
        fprintf(stderr, "error: %s\n", err);
        smk_rom_free(&rom);
        return 1;
    }
    if (!have_at) smk_track_start(&trk, 0, &shot_x, &shot_y, &shot_a);
    printf("loaded \"%s\"\n", rom.title);
    printf("track %d, theme %d (from the ROM's own table), class %d\n",
           track, trk.theme, engine_class);
    printf("driver: %s (sheet $%06X, palette $%02X)\n",
           drv->name, drv->sheet, drv->pal);
    printf("course: %d sectors, racing line loaded\n", crs.sectors);
    printf("acceleration curve and target speeds read from the ROM\n");

    /* Raw asset dump, so the C pipeline can be diffed against the oracle
     * running the game's own 65816 code.  Layout: 16384 map, 12288 tiles,
     * 1024 palette (256 x uint32 little-endian). */
    if (dump) {
        FILE *f = fopen(dump, "wb");
        if (!f) { fprintf(stderr, "cannot write %s\n", dump); return 1; }
        /* the contract with tools/test.py is loader output only:
         * 192 theme tiles, and the map BEFORE object stamps */
        fwrite(trk.map, 1, sizeof trk.map, f);
        fwrite(trk.tiles, 1, (size_t)SMK_TILE_COUNT * SMK_TILE_BYTES, f);
        fwrite(trk.palette, 4, 256, f);
        fclose(f);
        printf("track %d theme %d -> %s\n", track, trk.theme, dump);
        smk_rom_free(&rom);
        return 0;
    }

    smk_track_place_objects(&rom, &trk);

    /* Headless single-frame render: no window, no event loop.  Also the
     * cheapest way to eyeball the renderer from a script. */
    if (shot) {
        int sw = win_w / pixel, sh = win_h / pixel;
        uint32_t *px = malloc((size_t)sw * (size_t)sh * sizeof *px);
        smk_camera c = { .x = shot_x, .y = shot_y, .angle = shot_a,
                         .height = cam_height, .horizon = cam_horizon,
                         .fov = cam_fov };
        smk_render_mode7(&trk, &c, px, sw, sh, sw);
        {
            input_state none;
            float lz = 0.0f;
            /* the kart struct does not exist yet on this path; derive the
             * heading from the shot camera the same way its init would */
            uint16_t heading = (uint16_t)(shot_a * (float)SMK_ANGLE_TURN
                                          / (2.0f * (float)M_PI)
                                          + SMK_ANGLE_TURN / 4);
            memset(&none, 0, sizeof none);
            static smk_racer shot_racers[SMK_CHARACTERS];
            for (int i = 0; i < SMK_CHARACTERS; i++)
                racer_start(&shot_racers[i], &crs, i);
            draw_scene(&rom, &trk, &karts, drv, &c, px, sw, sh,
                       show_grid, show_kart, frame_for(&none, &lz),
                       heading, shot_racers, &crs);
            {
                smk_kart shotk = { .speed = 583 };   /* sample readout */
                draw_speedo(px, sw, sh, &shotk,
                            smk_track_surface(&trk, (int)shot_x, (int)shot_y),
                            672);
            }
        }
        if (SDL_Init(SDL_INIT_VIDEO) != 0 && SDL_Init(0) != 0) {
            fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
            return 1;
        }
        SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormatFrom(
            px, sw, sh, 32, sw * (int)sizeof *px, SDL_PIXELFORMAT_ARGB8888);
        int rc = SDL_SaveBMP(surf, shot);
        printf("%s %dx%d -> %s\n", rc ? "failed" : "wrote", sw, sh, shot);
        SDL_FreeSurface(surf);
        free(px);
        SDL_Quit();
        smk_rom_free(&rom);
        return rc ? 1 : 0;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *win = SDL_CreateWindow("Super Mario Kart",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, win_w, win_h,
        SDL_WINDOW_RESIZABLE | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    if (!win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }

    SDL_Renderer *ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return 1; }

    int filter = 0;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    int rw = 0, rh = 0;
    SDL_Texture *tex = NULL;
    uint32_t *fb = NULL;

    smk_camera cam = { .height = cam_height, .horizon = cam_horizon,
                       .fov = cam_fov };
    course_for_step = &crs;
    static smk_racer racers[SMK_CHARACTERS];
    for (int i = 0; i < SMK_CHARACTERS; i++)
        racer_start(&racers[i], &crs, i);
    smk_racer *me = &racers[0];

    float lean = 0.0f;
    float g0x, g0y;
    uint16_t g0h;
    smk_course_start(&crs, 0, &g0x, &g0y, &g0h);
    smk_kart kart = {
        .x = (int32_t)(g0x * SMK_POS_ONE),
        .y = (int32_t)(g0y * SMK_POS_ONE),
        .angle = g0h,
    };
    camera_from_kart(&cam, &kart);

    input_state in;
    memset(&in, 0, sizeof in);

    Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 prev = SDL_GetPerformanceCounter();
    Uint64 run_t0 = prev;
    long total_frames = 0;
    float accum = 0.0f;
    int frames = 0; Uint64 fps_t0 = prev;

    while (!in.quit) {
        pump(&in);

        /* (re)create the framebuffer when the window size changes */
        int ww, wh;
        SDL_GetRendererOutputSize(ren, &ww, &wh);
        int nw = ww / pixel, nh = wh / pixel;
        if (nw < 1) nw = 1;
        if (nh < 1) nh = 1;
        if (nw != rw || nh != rh) {
            rw = nw; rh = nh;
            if (tex) SDL_DestroyTexture(tex);
            tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STREAMING, rw, rh);
            free(fb);
            fb = malloc((size_t)rw * (size_t)rh * sizeof *fb);
        }

        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)(now - prev) / (float)freq;
        prev = now;
        if (dt > 0.25f) dt = 0.25f;            /* don't spiral after a stall */
        accum += dt;

        bool stepped = false;
        while (accum >= TICK_DT) {
            accum -= TICK_DT;
            stepped = true;

            if (in.next_track || in.prev_track || in.next_pal || in.prev_pal) {
                int nt = track, nth = theme;
                if (in.next_track) { nt = (track + 1) % SMK_TRACK_COUNT; nth = -1; }
                if (in.prev_track) { nt = (track + SMK_TRACK_COUNT - 1) % SMK_TRACK_COUNT; nth = -1; }
                if (in.next_pal)   nth = (trk.theme + 1) % SMK_THEME_COUNT;
                if (in.prev_pal)   nth = (trk.theme + SMK_THEME_COUNT - 1) % SMK_THEME_COUNT;
                if (smk_track_load(&rom, nt, nth, &trk, err, sizeof err)
                    && smk_course_load(&rom, nt, &crs)) {
                    float sx, sy;
                    uint16_t sh;
                    smk_track_place_objects(&rom, &trk);
                    track = nt; theme = nth;
                    smk_course_start(&crs, 0, &sx, &sy, &sh);
                    kart = (smk_kart){ .x = (int32_t)(sx * SMK_POS_ONE),
                                       .y = (int32_t)(sy * SMK_POS_ONE),
                                       .angle = sh };
                    for (int i = 0; i < SMK_CHARACTERS; i++)
                        racer_start(&racers[i], &crs, i);
                    camera_from_kart(&cam, &kart);
                } else {
                    fprintf(stderr, "skipped: %s\n", err);
                    smk_track_load(&rom, track, theme, &trk, err, sizeof err);
                    smk_track_place_objects(&rom, &trk);
                }
            }
            if (in.toggle_filter) {
                filter = !filter;
                SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, filter ? "1" : "0");
                if (tex) { SDL_DestroyTexture(tex); tex = NULL; rw = rh = 0; }
            }
            input_edges_clear(&in);

            step_kart(&kart, &trk, &phys, &in);
            camera_from_kart(&cam, &kart);
            me->k = kart;

            for (int i = 1; i < SMK_CHARACTERS; i++)
                racer_step(&racers[i], &trk, &crs, &phys);

            /* player lap counting - the decoded rule via racer state */
            {
                uint8_t cell = smk_course_cell(&crs, smk_kart_px(kart.x),
                                               smk_kart_px(kart.y));
                int sec = cell & SMK_SECT_OFF;
                if (sec != SMK_SECT_OFF && sec < crs.sectors
                    && !(kart.airborne && (crs.wattr[sec] & 0x80))) {
                    if (me->lap_cool > 0) me->lap_cool--;
                    if ((cell & SMK_SECT_FINISH) && me->lap_cool == 0) {
                        if (me->sector >= crs.sectors - 2 && sec <= 1) {
                            int prog = ((me->lap + 1) << 8) | sec;
                            if (prog > me->progress_max) {
                                me->lap++;
                                me->progress_max = prog;
                                me->lap_cool = 90;
                            }
                        } else if (sec >= crs.sectors - 2 && me->sector <= 1) {
                            me->lap--;
                            me->lap_cool = 90;
                        }
                    }
                    me->sector = sec;
                }
            }
        }
        (void)stepped;   /* edges deliberately survive a tickless iteration */

        if (tex && fb) {
            smk_render_mode7(&trk, &cam, fb, rw, rh, rw);
            draw_scene(&rom, &trk, &karts, drv, &cam, fb, rw, rh,
                       show_grid, show_kart, frame_for(&in, &lean),
                       kart.angle, racers, &crs);
            draw_speedo(fb, rw, rh, &kart,
                        smk_track_surface(&trk, smk_kart_px(kart.x),
                                          smk_kart_px(kart.y)),
                        (int16_t)phys.w[SMK_PHYS_TARGET + FEEL_TARGET_IDX]);
            SDL_UpdateTexture(tex, NULL, fb, rw * (int)sizeof *fb);
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex, NULL, NULL);
            SDL_RenderPresent(ren);
        }

        total_frames++;
        if (max_frames && total_frames >= max_frames) in.quit = true;

        if (++frames >= 60) {
            Uint64 t1 = SDL_GetPerformanceCounter();
            double secs = (double)(t1 - fps_t0) / (double)freq;
            char title[192];
            snprintf(title, sizeof title,
                     "SMK [" SMK_BUILD "]  track %d  lap %d  sector %d/%d  -  "
                     "surf $%02X type %d cap %d  -  %dx%d  %.0f fps",
                     track, me->lap + 1, me->sector, crs.sectors,
                     smk_track_surface(&trk, smk_kart_px(kart.x),
                                       smk_kart_px(kart.y)),
                     smk_surface_type(smk_track_surface(&trk,
                         smk_kart_px(kart.x), smk_kart_px(kart.y))),
                     smk_surface_cap(smk_track_surface(&trk,
                         smk_kart_px(kart.x), smk_kart_px(kart.y))),
                     rw, rh, frames / secs);
            SDL_SetWindowTitle(win, title);
            frames = 0; fps_t0 = t1;
        }
    }

    if (max_frames) {
        double secs = (double)(SDL_GetPerformanceCounter() - run_t0) / (double)freq;
        printf("%ld frames at %dx%d in %.2fs = %.1f fps (%.2f ms/frame)\n",
               total_frames, rw, rh, secs, total_frames / secs,
               1000.0 * secs / (double)total_frames);
    }

    free(fb);
    if (tex) SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    smk_rom_free(&rom);
    return 0;
}
