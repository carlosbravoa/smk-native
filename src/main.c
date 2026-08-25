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

/* Gamepad.
 *
 * Mapped to the SNES pad the game itself reads ($4218/$4219): B
 * accelerates, Y brakes, the SHOULDERS hop - which is how you drift in
 * SMK, by hopping into a held turn - and the d-pad steers.  On an
 * Xbox-style controller SDL's "A" is the bottom face button, so A is
 * the natural B; the triggers double as accelerate/brake because that
 * is what hands expect on a modern pad, and the left stick doubles for
 * the d-pad through a deadzone (the game's steering is digital, so the
 * stick is thresholded, not scaled). */
static SDL_GameController *pad;

static void pad_open(int idx)
{
    if (pad || !SDL_IsGameController(idx)) return;
    pad = SDL_GameControllerOpen(idx);
    if (pad)
        printf("gamepad: %s\n", SDL_GameControllerName(pad));
}

static void pad_close(SDL_JoystickID which)
{
    if (!pad) return;
    SDL_Joystick *j = SDL_GameControllerGetJoystick(pad);
    if (j && SDL_JoystickInstanceID(j) == which) {
        SDL_GameControllerClose(pad);
        pad = NULL;
        printf("gamepad: disconnected\n");
    }
}

static void pump(input_state *in)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) in->quit = true;
        if (e.type == SDL_CONTROLLERDEVICEADDED) pad_open(e.cdevice.which);
        if (e.type == SDL_CONTROLLERDEVICEREMOVED) pad_close(e.cdevice.which);
        if (e.type == SDL_CONTROLLERBUTTONDOWN) {
            switch (e.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: in->hop = true; break;
            case SDL_CONTROLLER_BUTTON_RIGHTSTICK:    in->toggle_filter = true; break;
            case SDL_CONTROLLER_BUTTON_BACK:          in->prev_track = true; break;
            case SDL_CONTROLLER_BUTTON_START:         in->next_track = true; break;
            default: break;
            }
        }
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

    if (pad) {
        const int DEAD = 9000;      /* stick deadzone, SDL units */
        const int TRIG = 12000;     /* trigger press threshold   */
        int lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
        int lt = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        int rt = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        #define BTN(b) SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_##b)
        in->up    |= BTN(A) || rt > TRIG;                 /* SNES B: accel */
        in->down  |= BTN(X) || BTN(B) || lt > TRIG;       /* SNES Y: brake */
        in->left  |= BTN(DPAD_LEFT)  || lx < -DEAD;
        in->right |= BTN(DPAD_RIGHT) || lx >  DEAD;
        in->hop_held |= BTN(LEFTSHOULDER) || BTN(RIGHTSHOULDER);
        in->shift |= BTN(Y);
        #undef BTN
    }
}

static int player_slip_deg;
static int player_slip_units;   /* signed, $10000 = full turn */
static int player_airborne;
static int hud_lap, hud_rank;
static long hud_race_frames;             /* frames since the lights */
static int  hud_countdown;               /* 3,2,1 while the lights run  */
/* The start sequence.  SMK holds the karts for a countdown, then runs;
 * our timing is the ROM's own 3-2-1-GO cadence in frames (60/step) -
 * LABELLED: the exact ROM start-frame count is not decoded yet, the
 * cadence is the observable one. */
enum { RACE_COUNTDOWN, RACE_RUN };
static int race_state = RACE_COUNTDOWN;
static int race_count;                   /* frames spent counting down  */
#define RACE_COUNT_FRAMES 180
static smk_hud hud_art;                  /* the game's own HUD sprites */

/* Draw one HUD tile at 8x8 * scale, palette $C0, index 0 transparent. */
static void hud_tile(uint32_t *fb, int rw, int rh, int x, int y, int tile,
                     const uint32_t *palette, int sc)
{
    if (!hud_art.ok || tile < 0 || tile >= SMK_HUD_TILES) return;
    const uint8_t *px = hud_art.px[tile];
    for (int ty = 0; ty < 8 * sc; ty++) {
        int sy = y + ty;
        if (sy < 0 || sy >= rh) continue;
        for (int tx = 0; tx < 8 * sc; tx++) {
            int sx = x + tx;
            if (sx < 0 || sx >= rw) continue;
            uint8_t v = px[(ty / sc) * 8 + (tx / sc)];
            if (v) fb[sy * rw + sx] = palette[(SMK_HUD_PAL + v) & 0xFF];
        }
    }
}

/* The race clock, in the game's own art: M ' SS " HH.
 *
 * SMK counts FRAMES (the timer advances once per rendered frame, which
 * is the console's 60 Hz) and formats minutes/seconds/hundredths for
 * display; hundredths are frames * 100 / 60.  The separator uses the
 * ROM's own tile $A2. */
static void draw_clock(uint32_t *fb, int rw, int rh, const uint32_t *palette,
                       long frames)
{
    if (!hud_art.ok || frames < 0) return;
    int sc = rw >= 640 ? 3 : 2;
    long total_cs = frames * 100 / 60;          /* hundredths */
    int cs = (int)(total_cs % 100);
    long secs = total_cs / 100;
    int ss = (int)(secs % 60);
    int mm = (int)(secs / 60); if (mm > 9) mm = 9;
    int x = 8, y = 8, adv = 8 * sc;
    hud_tile(fb, rw, rh, x, y, smk_hud_digit(mm), palette, sc);
    hud_tile(fb, rw, rh, x + adv, y, 0xA2 - SMK_HUD_TILE0, palette, sc);
    hud_tile(fb, rw, rh, x + adv * 2, y, smk_hud_digit(ss / 10), palette, sc);
    hud_tile(fb, rw, rh, x + adv * 3, y, smk_hud_digit(ss % 10), palette, sc);
    hud_tile(fb, rw, rh, x + adv * 4, y, 0xA2 - SMK_HUD_TILE0, palette, sc);
    hud_tile(fb, rw, rh, x + adv * 5, y, smk_hud_digit(cs / 10), palette, sc);
    hud_tile(fb, rw, rh, x + adv * 6, y, smk_hud_digit(cs % 10), palette, sc);
}

/* "LAP n/N" in the game's own art, top-right like the original. */
static void draw_hud(uint32_t *fb, int rw, int rh, const uint32_t *palette,
                     int lap, int laps, int rank)
{
    if (!hud_art.ok) return;
    int sc = rw >= 640 ? 3 : 2;
    int x = rw - 8 * sc * 5 - 8, y = 8;
    /* the LAP word: tiles $B0/$B1 (the strip the ROM draws beside the
     * digit), then the lap number */
    hud_tile(fb, rw, rh, x, y, 0xB0 - SMK_HUD_TILE0, palette, sc);
    hud_tile(fb, rw, rh, x + 8 * sc, y, 0xB1 - SMK_HUD_TILE0, palette, sc);
    int d = lap < 1 ? 1 : (lap > 9 ? 9 : lap);
    hud_tile(fb, rw, rh, x + 8 * sc * 3, y, smk_hud_digit(d), palette, sc);
    /* position, under it */
    int r = rank < 1 ? 1 : (rank > 8 ? 8 : rank);
    hud_tile(fb, rw, rh, x + 8 * sc * 3, y + 9 * sc, smk_hud_digit(r),
             palette, sc);
    (void)laps;
}
static int player_height_px;


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
    if (hud_lap > 0) {          /* lap and position: "L-P" in the corner */
        int lx = rw - 30 * sc, ly = 8;
        hud_number(fb, rw, rh, lx, ly, hud_lap > 9 ? 9 : hud_lap, 1,
                   0xFFFFFFFF, sc);
        hud_glyph(fb, rw, rh, lx + 6 * sc, ly, 15, 0xFF808088, sc); /* F as dash */
        hud_number(fb, rw, rh, lx + 12 * sc, ly, hud_rank, 1,
                   0xFFFFD040, sc);
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
/* MEASURED (NOTES 088), not felt: braking in SMK is WEAK - from 589 the
 * game takes 85 frames to reach 99, about 5.8 units/frame, and simply
 * coasting loses 5.2/frame.  Braking is barely stronger than lifting off,
 * which is why you slow a kart by releasing rather than by braking.  Our
 * old 32/frame brake was 5.5x too strong (playtest).  These are applied
 * as dec<<8 and integrated, so the value is units/frame * 256. */
#define FEEL_BRAKE   (6 * 256)
#define FEEL_DRAG    (5 * 256)
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
    if (k->speed < target) {
        /* The ROM's own acceleration table ($80B043) - our curve matches
         * the game's frame for frame up to about half speed.  Above that
         * the game TAPERS as it approaches the target while we did not,
         * so we arrived early and then snapped against a hard clamp: top
         * speed felt free instead of earned (playtest).
         *
         * The taper is a FIT to the measured approach (NOTES 088:
         * 12, 9.2, 7.6, 4, 3, 2, 2, 1.8 units/frame at speeds 355..702),
         * not a decode - the ROM's exact near-target law is open. */
        int32_t a = (int32_t)smk_physics_accel(phys, k->speed) << 8;
        if (target > 0) {
            float head = (float)(target - k->speed) / (float)target * 1.6f;
            if (head < 0.0f) head = 0.0f;
            if (head < 1.0f) a = (int32_t)((float)a * head);
        }
        accel = a;
    }
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

    /* no hard clamp: the taper above brings the speed in asymptotically,
     * the way the game does.  Snapping to the target was the other half
     * of "acceleration is super fast" (playtest). */
    if (k->speed > top) k->speed = (int16_t)top;   /* class ceiling only */

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
    /* below walking pace there is no meaningful velocity direction -
     * atan2 of a near-zero vector against the heading is garbage (the
     * "sideways at rest" bug) */
    int spd_abs = k->speed < 0 ? -k->speed : k->speed;
    if (spd_abs < 40) slip_now = 0.0f;
    player_slip_deg = (int)(fabsf(slip_now) * 180.0f / (float)M_PI);
    player_slip_units = (int)(slip_now * 65536.0f / (2.0f * (float)M_PI));

    /* steering authority falls off as the kart slows, as it must */
    int auth = (k->speed < 0 ? -k->speed : k->speed);
    if (auth > top) auth = top;
    int turn = top ? FEEL_TURN * auth / top : 0;
    /* Breakaway with HYSTERESIS: enter past the measured threshold,
     * leave only once the slip has clearly recovered.  A single
     * threshold flapped the turn rate (420 <-> 25) every frame at the
     * boundary, and the camera - which follows the heading - juddered
     * with it (playtest). */
    static bool plowing;
    if (slip_u0 > 4000.0f) plowing = true;
    else if (slip_u0 < 2800.0f) plowing = false;
    if (plowing && !in->hop_held)
        turn = turn * 6 / 100;               /* measured plow: ~-20 vs -307 */
    if (in->left)  k->angle -= (uint16_t)turn;
    if (in->right) k->angle += (uint16_t)turn;

    /* Hop: the decoded launch ($80B69D - zvel $0080, needs speed).  A hop
     * into a held turn starts a power slide. */
    if (in->hop && !k->airborne)
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
         * Grip is CLASS-INDEPENDENT - now measured across BOTH grip
         * batteries (12 classes): every class shows the same steady
         * slip (~200-330) and convergence, and one ABSOLUTE lateral
         * limit (~250k demo-scale) explains exactly which classes break
         * away: $4E/$48/$4A (caps .89-.97, fast enough to cross the
         * limit - VL ice among them) do, $56/$58/$5A/$5C (slow caps)
         * cannot.  "Ice feel" is EMERGENT from cap vs limit, not a grip
         * multiplier.  Our limit scales with class top (labelled feel
         * adaptation, NOTES 069) which keeps that relationship at every
         * engine class. */
        float va = atan2f((float)k->vx, -(float)k->vy);
        float ha = (float)k->angle * (float)(2.0 * M_PI) / 65536.0f;
        float slip = va - ha;
        while (slip >  (float)M_PI) slip -= 2.0f * (float)M_PI;
        while (slip < -(float)M_PI) slip += 2.0f * (float)M_PI;
        float slip_u = fabsf(slip) * 65536.0f / (2.0f * (float)M_PI);

        float class_grip = 1.0f;         /* uniform - measured, see above */

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
            g = 0.045f;                       /* held slide: FIT to the
                                                 lab's drift slip curve
                                                 (1024@6f, 5696@20f,
                                                 8640@40f - NOTES 080);
                                                 0.10 capped slip at
                                                 ~4200, below the
                                                 sideways poses */
        else if ((lateral > limit || plowing)
                 && (in->left || in->right))
            g = 0.0f;                         /* plow: see the ROTATION
                                                 below - a negative g
                                                 shrank the velocity
                                                 vector, so the kart lost
                                                 speed in steps and then
                                                 regained grip: the
                                                 stutter cycle (playtest).
                                                 The ROM keeps speed
                                                 through a plow (measured
                                                 791 -> 801). */
        else
            g = 0.50f * class_grip;           /* measured convergence     */
        float nvx = (float)k->vx + (float)(tvx - k->vx) * g;
        float nvy = (float)k->vy + (float)(tvy - k->vy) * g;
        if (plowing && (in->left || in->right)) {
            /* MEASURED slide (NOTES 089, a 150-frame hop-drift):
             *
             *   f5 11.2 deg   f10 22.8   f20 43.0   f30 62.3
             *   f45 75.0      f60 83.8   then it recovers
             *
             * The growth DECAYS as the slide opens up - per-frame steps
             * of 423, 366, 352, 154, 106 - i.e. the slip approaches a
             * ceiling near 17000 units (93 deg) at about 0.03 of the
             * remaining gap per frame.  A CONSTANT 130/frame instead
             * grew it without bound, so the kart swung past 90 degrees
             * and ended up travelling backwards - "magically drifting
             * opposite to where you are heading", and the side-on
             * sprite that goes with it.
             *
             * Speed in the same capture holds (846..854) and then falls
             * to about 0.70 of pace and stays there. */
            const float SLIP_MAX  = 17000.0f;   /* ~93 deg, measured    */
            const float SLIP_RATE = 0.03f;      /* of the gap, per frame */
            float mag = sqrtf((float)k->vx * k->vx + (float)k->vy * k->vy);
            float cur = fabsf(slip_u0);
            float step = (SLIP_MAX - cur) * SLIP_RATE;
            if (step < 0.0f) step = 0.0f;
            float rate = step * (2.0f * (float)M_PI) / 65536.0f;
            float dir  = slip_now >= 0.0f ? 1.0f : -1.0f;
            float va2  = atan2f((float)k->vx, -(float)k->vy) + dir * rate;
            nvx = sinf(va2) * mag;
            nvy = -cosf(va2) * mag;
            /* the measured speed sag through a long slide */
            int sag = top * 70 / 100;
            if (k->speed > sag) k->speed -= 3;
        }
        k->vx = (int16_t)nvx;
        k->vy = (int16_t)nvy;
    }
    smk_kart_move(k, trk);       /* the ROM's position += velocity << 8 */
    if (course_for_step) smk_collide_objects(k, course_for_step);
    player_height_px = smk_kart_height_px(k);
    player_airborne = k->airborne;
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
     * like the game does ($84F1A4).  Sprite obstacles come from the
     * decoded entity list (NOTES 078) and draw as billboards - the
     * pixels are still placeholders (green pipe) until the entity
     * sprite art is located. */
    if (course) {
        for (int i = 0; i < course->nent; i++) {
            float px, py, sc;
            if (!smk_project(cam, (float)course->ent[i].x,
                             (float)course->ent[i].y, rw, rh, &px, &py, &sc))
                continue;
            int bw = (int)(10.0f * sc) + 1, bh = (int)(16.0f * sc) + 1;
            for (int dy = 0; dy < bh; dy++) {
                int yy = (int)py - dy;
                if (yy < 0 || yy >= rh) continue;
                for (int dx = -bw / 2; dx <= bw / 2; dx++) {
                    int xx = (int)px + dx;
                    if (xx < 0 || xx >= rw) continue;
                    int edge = (dx < -bw / 2 + 1 || dx > bw / 2 - 1);
                    int lip = (dy > bh - 3);
                    fb[yy * rw + xx] = lip ? 0xFF77E077
                                    : edge ? 0xFF1E6B1E : 0xFF2E9B2E;
                }
            }
        }
    }

    if (show_grid && karts->frames && racers) {
        static smk_sprites other[SMK_CHARACTERS];
        static bool loaded[SMK_CHARACTERS];
        for (int k = 1; k < SMK_CHARACTERS; k++) {
            float px, py, sc;
            float gx = (float)smk_kart_px(racers[k].k.x);
            float gy = (float)smk_kart_px(racers[k].k.y);
            if (!smk_project(cam, gx, gy, rw, rh, &px, &py, &sc)) continue;
            /* MEASURED scaling (NOTES 076): the law is BINARY.  Near
             * range: the FULL 32x32 art at constant canvas (1/8 screen
             * width) - no shrinking at all.  Far range: a ~16px sprite
             * the game composes at runtime.  No intermediate steps, and
             * NO distance cull - karts render past depth 470.  The
             * switch was bracketed to (72, 96]; 84 until pinned.  The
             * sheet's rows 1-2 (27/24px art) are NOT depth tiers - the
             * old 96/160/224/320 stepping was wrong. */
            float a2 = (float)cam_heading * (float)(2.0 * M_PI) / 65536.0f;
            float depth = (gx - cam->x) * sinf(a2)
                        + (gy - cam->y) * -cosf(a2);
            /* MEASURED sizing (NOTES 076), now valid because the sprite
             * projection is the game's own flat law: the canvas stays
             * CONSTANT (1/8 screen width) at every depth, the ART steps
             * full -> mini at depth ~84, and there is no distance cull. */
            float dep_eye = depth + SMK_CAM_TRAIL;
            if (dep_eye < 12.0f) continue;
            int scale = (int)((float)(rw / 256) * SMK_CAM_TRAIL / dep_eye + 0.5f);
            if (scale < 1) scale = 1;
            const smk_driver *d2 = &SMK_DRIVERS[k];
            if (!loaded[k]) loaded[k] = smk_sprites_load(rom, d2->sheet, &other[k]);
            if (!loaded[k]) continue;
            /* same measured pose ladder as the player (NOTES 080):
             * mirrored straight < $400, 47 half-lean < $1000, then the
             * rotation frames */
            int rel = (int16_t)(uint16_t)(racers[k].k.angle - cam_heading);
            int ar = rel < 0 ? -rel : rel;
            bool hf = false, mirror = false;
            int f = 0;
            if (ar < 0x0400) mirror = true;    /* aligned: mirrored pose */
            else {
                /* the measured rotation rule handles every other band
                 * (AI karts do not hop, so no 47 lean here) */
                uint16_t r16 = (uint16_t)rel;
                f = smk_sprite_for_heading(SMK_SPR_TIER0, r16, &hf);
            }
            /* height lifts the sprite on screen, scaled like everything else */
            py -= (float)smk_kart_height_px(&racers[k].k) * sc;
            /* Size follows the SAME projection as everything else,
             * anchored on the ONE unambiguous measurement: the player's
             * kart is 32 SNES px at the trail distance (NOTES 084).  So
             * a kart at depth d draws 32 * TRAIL/d.  (The SNES itself
             * cannot scale sprites and quantises this to a few art
             * sizes; ours is continuous - a deliberate, labelled
             * divergence that keeps karts road-proportional.) */
            (void)depth;
            if (mirror)
                smk_draw_sprite_mirror(&other[k], 0, trk->palette, d2->pal,
                                       (int)px, (int)py, scale,
                                       fb, rw, rh, rw);
            else
                smk_draw_sprite(&other[k], f, trk->palette,
                                d2->pal, (int)px, (int)py, scale, hf,
                                fb, rw, rh, rw);
        }
    }
    if (show_kart && karts->frames) {
        int scale = rw / 256;                 /* the SNES 32px proportion */
        if (scale < 1) scale = 1;
        /* the hop lifts the sprite; the shadow stays on the ground */
        int lift = player_height_px * scale;
        int prow = (int)(SMK_PLAYER_LINE * (float)rh / 112.0f);
        if (frame == 1000)                    /* the mirrored straight pose */
            smk_draw_sprite_mirror(karts, 0, trk->palette, drv->pal,
                                   rw / 2, prow - lift, scale,
                                   fb, rw, rh, rw);
        else {
            bool hf = frame < 0;
            smk_draw_sprite(karts, hf ? -frame : frame, trk->palette,
                            drv->pal, rw / 2, prow - lift, scale,
                            hf, fb, rw, rh, rw);
        }
    }
    draw_hud(fb, rw, rh, trk->palette, hud_lap, 5, hud_rank);
    draw_clock(fb, rw, rh, trk->palette, hud_race_frames);
    if (hud_countdown > 0) {              /* 3-2-1 in the game's digits */
        int sc = rw >= 640 ? 6 : 4;
        hud_tile(fb, rw, rh, rw / 2 - 4 * sc, rh / 3,
                 smk_hud_digit(hud_countdown), trk->palette, sc);
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
    /* MEASURED pose rule (framelab6 + the NOTES 041 rotation bands -
     * which the lab's drift samples confirm for the player: slip $1640
     * shows frame 2, $21C0 frame 4, both in their ORIGINAL bands):
     *
     *   |rel| < $0400  mirrored straight (frame 0's left half)
     *         < $1000  frame 1 - or frame 47 while hopping/drifting
     *                  (the lab's hop+steer phase showed 47 there)
     *         < $1800  frame 2
     *         < $2000  frame 3
     *         < $2800  frame 4 ... (the AI rule continues)
     *
     * rel = heading minus the lagging camera; our camera is rigid so
     * held steering synthesises a lag toward $0A00 (inside frame 1's
     * band, reached in ~8 frames as the lab shows); slides contribute
     * MINUS the slip angle, combined as the larger magnitude. */
    float want = (in->left ? -1.0f : 0.0f) + (in->right ? 1.0f : 0.0f);
    if (want != 0.0f)
        *lean += ((want * 2560.0f) - *lean) * 0.22f;   /* -> $0A00 in ~8f */
    else
        *lean *= 0.78f;
    int a1 = (int)*lean, a2 = -player_slip_units;
    int rel;
    if ((a1 >= 0) == (a2 >= 0))
        rel = (a1 < 0 ? -a1 : a1) >= (a2 < 0 ? -a2 : a2) ? a1 : a2;
    else
        rel = a1 + a2;
    int a = rel < 0 ? -rel : rel;
    if (a < 0x0400) return 1000;                       /* mirrored straight */
    if (a < 0x1000) {
        if (in->hop_held || player_airborne)
            return rel > 0 ? -47 : 47;                 /* 47 base = left    */
        return rel < 0 ? -1 : 1;                       /* 1 base = right    */
    }
    int f2 = a < 0x1800 ? 2
           : a < 0x2000 ? 3
           : a < 0x2800 ? 4
           : a < 0x3000 ? 5
           : a < 0x3800 ? 6 : 7;
    return rel < 0 ? -f2 : f2;
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
           "  o p             override the course theme\n"
           "\n"
           "controls (keyboard / gamepad, mapped to the SNES pad):\n"
           "  accelerate    Up or W      / A button or right trigger\n"
           "  brake         Down or S    / X or B button, left trigger\n"
           "  steer         Left Right   / d-pad or left stick\n"
           "  hop and drift Space        / either shoulder button\n"
           "  next track    ]            / Start\n"
           "  prev track    [            / Back\n"
           "  filter        F            / right stick click\n"
           "  quit          Esc\n"
           , argv0);
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
    /* The camera shape is no longer tunable: it is the ROM's own DSP-1
     * geometry (SMK_PROJ_*, NOTES 083/084).  The old --height-cam /
     * --horizon / --fov knobs tuned a projection that no longer exists,
     * so they are removed rather than left lying around as dead controls. */

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
    smk_hud_load(&rom, &hud_art);
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
                       };
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
            hud_lap = 1; hud_rank = 1;
            static smk_racer shot_racers[SMK_CHARACTERS];
            for (int i = 0; i < SMK_CHARACTERS; i++)
                smk_racer_start(&shot_racers[i], &crs, i);
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

    /* The pad is optional: a machine with no controller (or no udev
     * permissions) must still run, so this failing is not fatal. */
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0) {
        for (int i = 0; i < SDL_NumJoysticks(); i++) pad_open(i);
        if (!pad) printf("gamepad: none connected (keyboard active)\n");
    } else {
        fprintf(stderr, "gamepad: unavailable (%s)\n", SDL_GetError());
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

    smk_camera cam = { 0 };
    course_for_step = &crs;
    static smk_racer racers[SMK_CHARACTERS];
    for (int i = 0; i < SMK_CHARACTERS; i++)
        smk_racer_start(&racers[i], &crs, i);
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
                        smk_racer_start(&racers[i], &crs, i);
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
            if (race_state == RACE_COUNTDOWN) {
                /* the lights: no throttle, no steering, kart held */
                if (++race_count >= RACE_COUNT_FRAMES) race_state = RACE_RUN;
                hud_countdown = 3 - race_count / 60;
                if (hud_countdown < 1) hud_countdown = 1;
                in.up = in.down = in.left = in.right = false;
                in.hop_held = false;
            }
            if (race_state == RACE_RUN) { hud_race_frames++; hud_countdown = 0; }
            step_kart(&kart, &trk, &phys, &in);
            /* edges are cleared AFTER the tick that consumes them.  Clearing
             * first meant step_kart never saw in.hop, so a hop press did
             * nothing at all - the "no jump" report, twice. */
            input_edges_clear(&in);
            camera_from_kart(&cam, &kart);
            me->k = kart;

            if (race_state == RACE_RUN)
                for (int i = 1; i < SMK_CHARACTERS; i++)
                    smk_racer_step(&racers[i], &trk, &crs, &phys);

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
            hud_lap = me->lap + 1;
            hud_rank = smk_race_rank(racers, 0, &crs);
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
