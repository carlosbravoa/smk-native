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
static bool pad_off;          /* --no-pad */
static int  pad_lx;           /* last stick reading, for the HUD */

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

    if (pad && !pad_off) {
        const int DEAD = 9000;      /* stick deadzone, SDL units */
        const int TRIG = 12000;     /* trigger press threshold   */
        int lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
        int lt = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        int rt = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        #define BTN(b) SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_##b)
        in->up    |= BTN(A) || rt > TRIG;                 /* SNES B: accel */
        in->down  |= BTN(X) || BTN(B) || lt > TRIG;       /* SNES Y: brake */
        /* A stick is only believed once it has been seen at rest.  A
         * pad that reports a stuck or miscalibrated axis would otherwise
         * steer for ever with nothing touching it, and the player has no
         * way to tell that from a bug in the driving code. */
        static bool stick_ok;
        if (lx > -DEAD && lx < DEAD) stick_ok = true;
        in->left  |= BTN(DPAD_LEFT)  || (stick_ok && lx < -DEAD);
        in->right |= BTN(DPAD_RIGHT) || (stick_ok && lx >  DEAD);
        pad_lx = lx;
        in->hop_held |= BTN(LEFTSHOULDER) || BTN(RIGHTSHOULDER);
        in->shift |= BTN(Y);
        #undef BTN
    }
}

static int racer_draw_mask = 0xFE;      /* which racer slots draw_scene draws */
static int player_sector;               /* $C0: the last sector reached      */
static smk_horizon horizon;             /* the scenery above the track        */
static smk_effects fx;                  /* tyre smoke / dust (NOTES 109)      */
static smk_effect_state fx_state = { -1, 0, 0, 0 };
static int fx_frame_idx;                /* the kart sprite frame the puffs follow */
static bool fx_mirror;
static unsigned fx_ticks;
static int player_slip_deg;
static int player_slip_units;   /* signed, $10000 = full turn */
static int player_airborne;
static int hud_lap, hud_rank;
static long hud_race_frames;             /* frames since the lights */
static int  hud_countdown;               /* 3,2,1 while the lights run  */
static int  hud_input;                   /* L/R/accel bits, for the HUD */
/* The start sequence.  SMK holds the karts for a countdown, then runs;
 * our timing is the ROM's own 3-2-1-GO cadence in frames (60/step) -
 * LABELLED: the exact ROM start-frame count is not decoded yet, the
 * cadence is the observable one. */
enum { RACE_COUNTDOWN, RACE_RUN };
static int race_state = RACE_COUNTDOWN;
static int race_count;                   /* frames spent counting down  */
#define RACE_COUNT_FRAMES 180
static smk_hud hud_art;
static smk_objgfx obj_art;          /* pipes and other entities */                  /* the game's own HUD sprites */

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
/* Sprite priority against the plane (NOTES 128): filled by the ground
 * renderer, one byte a pixel, non-zero where the plane is opaque. */
static uint8_t *plane_mask;
static size_t plane_mask_sz;
static int player_below;                /* the player's z is under the plane */


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
/* The player's physics used to be a labelled feel model here; it is now
 * the decoded control in src/player.c (NOTES 103). */

static smk_player player;

/* One frame of the player's kart: the DECODED control (src/player.c, NOTES
 * 103).  This function only translates the SDL input into the SNES pad word
 * the ROM composes at $80A3CC and publishes the HUD readouts. */
static const smk_rom *rom_for_step;
static const char *replay_path;         /* --replay: drive the kart from the game's log */
static int replay_kart = 1000;          /* 1000 = P1 (Mario), 1100 = P2 (Toad)        */
static int replay_i;
static void step_kart(smk_kart *k, smk_track *trk,
                      const smk_physics *phys, const input_state *in)
{
    (void)phys;
    uint16_t held = 0, pressed = 0;
    if (in->up)       held |= 0x8000;        /* B: accelerate            */
    if (in->down)     held |= 0x4000;        /* Y: brake                 */
    if (in->left)     held |= 0x0200;
    if (in->right)    held |= 0x0100;
    if (in->hop_held) held |= 0x0020;        /* L (R is the same button) */
    if (in->hop)      pressed |= 0x0020;     /* a fresh press hops       */
    /* the rescue target: the ROM takes the kart's waypoint ($80B373 reads
     * $0900/$0A00 by $C0); ours is the course's waypoint for the sector
     * under the kart, with the flow field's heading */
    /* Lakitu's target ($80B373): the kart's own waypoint $C0 - the last
     * sector it legitimately reached, NOT the cell it fell on - and the
     * heading is the flow-field direction AT that waypoint. */
    /* $80B373 is called when the fall is ARMED and every frame of the
     * wade - but NOT while Lakitu is carrying the kart ($A0 = $0C/$0E),
     * where $CC/$CE/$D0 stand still.  Refreshing them there made the
     * target move with the kart, so it was never put down (NOTES 124). */
    if (course_for_step && player.hazard != 6
        && player.hazard != 0x0C && player.hazard != 0x0E) {
        int sec = player_sector;
        if (sec >= 0 && sec < course_for_step->sectors) {
            int wx = course_for_step->wx[sec], wy = course_for_step->wy[sec];
            player.resc_x = wx;
            player.resc_y = wy;
            /* $80B393 reads $7F3FFF,x as a WORD: the high byte is the
             * direction field at the waypoint's cell, the low byte the
             * cell before it. */
            int fcell = ((wy >> 4) & 63) * 64 + ((wx >> 4) & 63);
            player.resc_h = (uint16_t)((course_for_step->flow[fcell] << 8)
                                       | course_for_step->flow[(fcell - 1) & 0xFFF]);
        }
    }
    /* $84DBD5 runs every frame: the lap segment the player's waypoint
     * falls in decides which obstacles are on the track (NOTES 127) */
    if (course_for_step) smk_course_spawn(course_for_step, player_sector, false);
    bool grounded = k->z == 0;                   /* $1F,x before this frame's jump update */
    smk_player_step(&player, k, trk, held, pressed);
    if (course_for_step) smk_collide_objects(k, course_for_step);
    /* the collector ($81B73B) serves ONE player per frame, alternating:
     * every P1 pickup in the demo lands on an odd frame, every P2 pickup
     * on an even one, with the cell the kart is on after that frame's
     * move (NOTES 110).  A coin crossed on the other parity is missed if
     * the kart has left its cell by the next frame - as in the game. */
    {
        unsigned parity = replay_path ? (unsigned)replay_i : fx_ticks;
        unsigned mine = (replay_path && replay_kart == 1100) ? 0u : 1u;
        if (rom_for_step && (parity & 1u) == mine)
            smk_pickup_step(rom_for_step, trk, &player, k, grounded);
    }

    /* the ground effect object ($80CF7B..$80D4A3): what the surface under
     * the kart and the slide/spin state ask for this frame */
    {
        uint8_t surf = smk_track_surface(trk, smk_kart_px(k->x), smk_kart_px(k->y));
        int kind = smk_effects_pick(surf, !k->airborne && k->z == 0,
                                    (player.flags & 0x0008) != 0,
                                    (player.flags & 0x0020) != 0, k->speed);
        smk_effects_step(&fx_state, kind, fx_frame_idx);
        fx_ticks++;
    }

    /* The sprite's angle relative to the camera: pose ($2A) minus the
     * azimuth the ROM feeds the camera (heading + $C0).  In a left slide
     * the pose offset is positive, which the measured pose rule (NOTES
     * 080) reads as a positive slip. */
    int rel = (int16_t)(uint16_t)(player.plag + SMK_CAM_LEAD);
    player_slip_units = rel;
    player_slip_deg = (rel < 0 ? -rel : rel) * 360 / 65536;
    player_height_px = smk_kart_height_px(k);
    player_airborne = k->airborne;
}

/* The ROM's angle is 0 = -Y increasing clockwise; the renderer wants
 * radians with 0 = +X, and (cos, sin) must equal (sin a, -cos a). */
static void camera_from_kart(smk_camera *cam, const smk_kart *k)
{
    cam->x = (float)k->x / (float)SMK_POS_ONE;
    cam->y = (float)k->y / (float)SMK_POS_ONE;
    /* the ROM's camera azimuth is the heading plus a constant $C0 lead
     * ($808632, measured on the live race: cam - $A4 == 192 every frame) */
    uint16_t az = (uint16_t)(k->angle + SMK_CAM_LEAD);
    cam->angle = (float)az * (2.0f * (float)M_PI / (float)SMK_ANGLE_TURN)
                 - (float)M_PI / 2.0f;
}


/* One track obstacle.  Split out of draw_scene so obstacles and karts can
 * be drawn in ONE depth-sorted pass (NOTES 128) - drawing every obstacle
 * before every kart put a pipe beside you behind a kart across the
 * track. */
static void draw_entity(const smk_track *trk, const smk_camera *cam,
                        const smk_course *course, int i,
                        uint32_t *fb, int rw, int rh)
{
        float px, py, sc;
        if (!smk_project(cam, (float)course->ent[i].x,
                         (float)course->ent[i].y, rw, rh, &px, &py, &sc))
            return;
        /* The theme's own object art (NOTES 098/099). */
        if (!obj_art.ok) return;
        /* Continuous scale, like the karts: the art is 16x40 SNES px
         * and shrinks with distance.  Flooring it at one screen pixel
         * per art pixel left far pipes full size, so their tops rose
         * above the horizon and they floated in the sky. */
        /* Pick the size tier whose art height matches what the
         * projection asks for, then draw it at the SNES proportion -
         * the hardware's own mechanism.  desired = 16 world px seen
         * at this depth, expressed in art pixels. */
        /* QUANTISED to the sheet's real tiers, as the hardware does.
         * The SNES cannot scale a sprite: it swaps to a smaller
         * drawing.  Measured off the sheet - the same descending
         * family exists in every theme:
         *
         *   theme 1  b0 12x15  b32 12x16  b34 11x14  b36 10x12
         *   theme 7  b0 12x16  b32 12x15  b34 11x13  b36 10x11
         *
         * so the true range is only 16 -> 11 art pixels.  Distant
         * objects settle at the smallest drawing rather than
         * dwindling away, and the size POPS between steps instead of
         * gliding - which is what the original does. */
        /* Only the FRONT-FACING drawings.  Rendering every
         * candidate base side by side shows bases 0/2/4/6 are
         * skewed perspective variants - the object seen at an angle
         * - and drawing one as the "largest tier" is what looked
         * like a torn sprite in playtest.  The clean ladder, by art
         * height: */
        /* Measured across both themes, the complete drawings with a
         * BODY are bases 32/34/36 (h/w about 1.2-1.3).  Bases
         * 8/10/12/14 are squat (h/w 0.6-0.8) - almost entirely rim -
         * and using base 12 as the far tier is why distant pipes
         * rendered as a lid with no length (playtest).  Beyond the
         * smallest we keep drawing the smallest, which is what the
         * hardware does; it never runs out of pipe. */
        static const struct { int base, h; } TIER[SMK_OBJ_TIERS] = {
            { SMK_OBJ_PIPE0,     16 },   /* 12x16 near  */
            { SMK_OBJ_PIPE0 + 2, 14 },   /* 11x14       */
            { SMK_OBJ_PIPE0 + 4, 12 },   /* 10x12 far   */
        };
        /* The game's own scale for this object (NOTES 129):
         *   +$06 = $4200 / depth ALONG THE VIEW AXIS ahead of the kart
         * measured at 2.1% over a driven lap.  cam->x/y IS the kart; the
         * eye trails it, so this is smk_project's zf before the trail. */
        float odx = (float)course->ent[i].x - cam->x;
        float ody = (float)course->ent[i].y - cam->y;
        /* The world does NOT wrap (NOTES 063), so neither may this.
         * Wrapping the delta put a kart or a pipe 900 px BEHIND you 124 px
         * in front of you, off the side of the track - the ghost copies the
         * user reported.  The Mode 7 plane repeats character 0 outside its
         * 1024 px, but that is the PPU filling the floor, not the world
         * being tiled (NOTES 138). */
        float zf = odx * cosf(cam->angle) + ody * sinf(cam->angle);
        /* Behind the EYE is the only thing that culls a near object -
         * smk_project has already returned false for that.  The port used
         * to drop anything nearer than $4200/$300 = 22 px along the axis,
         * reading $80C883's `sta $30,x` as parking the sprite, and pipes
         * vanished as you drove up to one: you could not stop beside it
         * (user, NOTES 130).  The size cannot run away here in any case -
         * the BANDS bound it, and band 0 is a fixed drawing however close
         * you get - so the scale is simply clamped into band 0. */
        if (zf < 1.0f) zf = 1.0f;
        int oscale = (int)(SMK_OBJ_SCALE_K / zf + 0.5f);
        if (oscale > SMK_OBJ_SCALE_HIDE) oscale = SMK_OBJ_SCALE_HIDE;
        /* $84DA18 walks $84DA3C = C0 60 30 00: the first threshold the
         * scale is ABOVE picks the drawing, and past the last one the
         * loop hits the terminator and the object is not drawn. */
        int ti;
        if (oscale > SMK_OBJ_BAND0)      ti = 0;
        else if (oscale > SMK_OBJ_BAND1) ti = 1;
        else if (oscale > SMK_OBJ_BAND2) ti = 2;
        else return;
        if (ti >= SMK_OBJ_TIERS) ti = SMK_OBJ_TIERS - 1;
        int obase = TIER[ti].base;
        /* The hardware cannot scale a sprite: each band draws at its own
         * fixed art size, so the size POPS between bands.  That is the
         * original's behaviour and it is why distant pipes had been
         * dwindling away here - they should settle at the last drawing
         * and then vanish. */
        float k = (float)TIER[ti].h / (float)SMK_OBJ_PIPE_H * (float)SMK_OBJ_MAG;
        float ppx = (float)rw / 256.0f;     /* render px per SNES px */
        int pw = (int)((float)SMK_OBJ_PIPE_W * k * ppx + 0.5f);
        int ph = (int)((float)SMK_OBJ_PIPE_H * k * ppx + 0.5f);
        if (pw < 1 || ph < 1) return;
        int x0 = (int)px - pw / 2, y0 = (int)py - ph;
        for (int dy = 0; dy < ph; dy++) {
            int yy = y0 + dy;
            if (yy < 0 || yy >= rh) continue;
            int ty = dy * SMK_OBJ_PIPE_H / ph;
            for (int dx = 0; dx < pw; dx++) {
                int xx = x0 + dx;
                if (xx < 0 || xx >= rw) continue;
                int tx = dx * SMK_OBJ_PIPE_W / pw;
                int tile = obase + (ty / 8) * SMK_OBJ_STRIDE + (tx / 8);
                if (tile >= SMK_OBJ_TILES) continue;
                uint8_t v = obj_art.px[tile][(ty % 8) * 8 + (tx % 8)];
                if (!v) continue;
                fb[yy * rw + xx] = trk->palette[(SMK_OBJ_PAL + v) & 0xFF];
            }
        }
}

/* One AI kart, split out for the same reason as draw_entity. */
static void draw_ai_kart(const smk_rom *rom, const smk_track *trk,
                         const smk_camera *cam, uint16_t cam_heading,
                         const smk_racer *racers, int k,
                         smk_sprites *other, int *loaded,
                         uint32_t *fb, int rw, int rh)
{
        float px, py, sc;
        float gx = (float)smk_kart_px(racers[k].k.x);
        float gy = (float)smk_kart_px(racers[k].k.y);
        if (!smk_project(cam, gx, gy, rw, rh, &px, &py, &sc)) return;
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
        if (dep_eye < 12.0f) return;
        int scale = (int)((float)(rw / 256) * SMK_CAM_TRAIL / dep_eye + 0.5f);
        if (scale < 1) scale = 1;
        int ch = racers[k].character;
        if (ch < 0 || ch >= SMK_CHARACTERS) ch = k;
        const smk_driver *d2 = &SMK_DRIVERS[ch];
        if (loaded[k] != ch + 1) loaded[k] = smk_sprites_load(rom, d2->sheet, &other[k]) ? ch + 1 : 0;
        if (!loaded[k]) return;
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
        /* QUANTISED to the kart sheet's own tiers, as the hardware
         * does (NOTES 102).  The sheet carries three rotation sets
         * at descending sizes - measured max art height:
         *
         *   frames  0-10   31 px      frames 11-21   28 px
         *   frames 22-32   25 px      half-size drawing far out
         *
         * plus the 16 px switch NOTES 072 saw beyond those.  The
         * tier is chosen by the height the projection asks for and
         * then drawn at the fixed SNES proportion, so kart sizes POP
         * between steps exactly like the entities. */
        static const struct { int base, h; } KTIER[4] = {
            { SMK_SPR_TIER0, 31 }, { SMK_SPR_TIER1, 28 },
            { SMK_SPR_TIER2, 25 }, { -1,            13 },
        };
        /* Sized by the game's OWN scale law, measured on the kart
         * blocks themselves: +$06 = 0x4200 / d against the distance
         * from the player, the same rule the entities follow
         * (NOTES 105).  The old anchor used the camera depth and so
         * held distant karts too large. */
        float kdx = gx - cam->x, kdy = gy - cam->y;
        float kd = sqrtf(kdx * kdx + kdy * kdy);
        if (kd < SMK_OBJ_NEAR) kd = SMK_OBJ_NEAR;
        float kwant = (float)KTIER[0].h * SMK_KART_SCALE_K / kd;
        if (kwant > (float)KTIER[0].h) kwant = (float)KTIER[0].h;
        int kt = 0;
        for (int t = 1; t < 4; t++)
            if (fabsf((float)KTIER[t].h - kwant)
                < fabsf((float)KTIER[kt].h - kwant)) kt = t;
        int kscale = rw / 256;
        if (kscale < 1) kscale = 1;
        if (KTIER[kt].base < 0) {           /* the far, half-size draw */
            /* A MIRRORED pose is frame 0's left half FOLDED (NOTES 080),
             * and smk_draw_sprite_mini does not fold - it samples all 32
             * columns 2:1, so the junk right half came with it and the
             * sprite garbled at exactly this tier and no other.  mirror2
             * folds and minifies in one go (NOTES 138). */
            if (mirror)
                smk_draw_sprite_mirror2(&other[k], 0, trk->palette, d2->pal,
                                        (int)px, (int)py, kscale, true,
                                        fb, rw, rh, rw);
            else
                smk_draw_sprite_mini(&other[k], f, trk->palette, d2->pal,
                                     (int)px, (int)py, kscale, hf,
                                     fb, rw, rh, rw);
        } else {
            /* re-pick the rotation frame inside the chosen tier */
            uint16_t r16 = (uint16_t)rel;
            bool hf2 = false;
            int f2 = mirror ? KTIER[kt].base
                            : smk_sprite_for_heading(KTIER[kt].base,
                                                     r16, &hf2);
            if (mirror)
                smk_draw_sprite_mirror(&other[k], f2, trk->palette,
                                       d2->pal, (int)px, (int)py,
                                       kscale, fb, rw, rh, rw);
            else
                smk_draw_sprite(&other[k], f2, trk->palette, d2->pal,
                                (int)px, (int)py, kscale, hf2,
                                fb, rw, rh, rw);
        }
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
    /* ONE depth-sorted pass over everything on the plane (NOTES 128).
     * Obstacles used to be drawn before every kart, so a pipe beside you
     * hid behind a kart on the far side of the track; and karts sorted
     * among themselves only.  The SNES sorts its OAM by distance for the
     * same reason - a nearer sprite has to be able to cover a farther one
     * whatever KIND it is. */
    {
        static smk_sprites other[SMK_CHARACTERS];
        static int loaded[SMK_CHARACTERS];      /* character + 1 whose sheet is in other[k] */
        struct { float dep; int kind, idx; } item[SMK_CHARACTERS + 8];
        int n = 0;
        float a2 = (float)cam_heading * (float)(2.0 * M_PI) / 65536.0f;
        if (course) {
            int nvis = course->nlive ? course->nlive : course->nent;
            for (int j = 0; j < nvis && n < (int)(sizeof item / sizeof item[0]); j++) {
                int i = course->nlive ? course->live[j] : j;
                float dx = (float)course->ent[i].x - cam->x;
                float dy = (float)course->ent[i].y - cam->y;
                item[n].dep = dx * sinf(a2) + dy * -cosf(a2);
                item[n].kind = 0; item[n].idx = i; n++;
            }
        }
        if (show_grid && karts->frames && racers) {
            for (int k = 1; k < SMK_CHARACTERS && n < (int)(sizeof item / sizeof item[0]); k++) {
                if (!(racer_draw_mask & (1 << k))) continue;
                float gx = (float)smk_kart_px(racers[k].k.x);
                float gy = (float)smk_kart_px(racers[k].k.y);
                item[n].dep = (gx - cam->x) * sinf(a2) + (gy - cam->y) * -cosf(a2);
                item[n].kind = 1; item[n].idx = k; n++;
            }
        }
        /* farthest first: insertion sort, descending by depth */
        for (int i = 1; i < n; i++) {
            typeof(item[0]) v = item[i];
            int j = i - 1;
            while (j >= 0 && item[j].dep < v.dep) { item[j + 1] = item[j]; j--; }
            item[j + 1] = v;
        }
        for (int i = 0; i < n; i++) {
            if (item[i].kind == 0)
                draw_entity(trk, cam, course, item[i].idx, fb, rw, rh);
            else
                draw_ai_kart(rom, trk, cam, cam_heading, racers, item[i].idx,
                             other, loaded, fb, rw, rh);
        }
    }
    if (show_kart && karts->frames) {
        int scale = rw / 256;                 /* the SNES 32px proportion */
        if (scale < 1) scale = 1;
        /* the hop lifts the sprite; the shadow stays on the ground */
        int lift = player_height_px * scale;
        /* Fallen through the track: the kart goes UNDER the plane, so it
         * is hidden by the track and shows only through the hole it fell
         * into - the SNES drops the sprite below BG1 (NOTES 128). */
        if (player_below && plane_mask) smk_draw_set_clip_mask(plane_mask, rw);
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
        /* the puffs sit relative to the kart sprite's top-left + (0,16)
         * and draw over the wheels (lower OAM slots than the kart) */
        smk_effects_draw(&fx, &fx_state, fx_mirror, fx_ticks,
                         rw / 2 - 16 * scale, prow - lift - 16 * scale, scale,
                         trk->palette, fb, rw, rh);
        smk_draw_set_clip_mask(NULL, 0);
    }
    draw_hud(fb, rw, rh, trk->palette, hud_lap, player.coins, hud_rank);
    draw_clock(fb, rw, rh, trk->palette, hud_race_frames);
    /* live input state, so a stuck control is visible rather than
     * looking like a physics bug */
    if (hud_input) {
        int sc2 = rw >= 640 ? 2 : 1;
        int bx = 8, by = rh - 20 * sc2 - 8;
        hud_glyph(fb, rw, rh, bx, by, hud_input & 1 ? 1 : 0,
                  hud_input & 1 ? 0xFFFF6060 : 0xFF404048, sc2);      /* L */
        hud_glyph(fb, rw, rh, bx + 6 * sc2, by, hud_input & 2 ? 1 : 0,
                  hud_input & 2 ? 0xFF60FF60 : 0xFF404048, sc2);      /* R */
        hud_glyph(fb, rw, rh, bx + 12 * sc2, by, hud_input & 4 ? 1 : 0,
                  hud_input & 4 ? 0xFF6060FF : 0xFF404048, sc2);      /* A */
        hud_number(fb, rw, rh, bx + 20 * sc2, by,
                   pad_lx < 0 ? -pad_lx : pad_lx, 5, 0xFF909098, sc2);
    }
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
    /* The bands to frame 4 are MEASURED (framelab6: slip $1640 showed
     * frame 2, $21C0 showed frame 4).  Past that the sprite is only
     * reached by a spin after a hit or a very long slide - the side-on
     * poses are never seen in ordinary driving - so the continuation is
     * the rotation rule's own spacing, LABELLED as extrapolation. */
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
           "  --no-pad        ignore gamepads (keyboard only)\n"
           "  --width W       window width                 [1024]\n"
           "  --height H      window height                [896]\n"
           "  --pixel N       render at 1/N resolution     [2]\n"
           "  --fullscreen\n"
           "  --frames N      run N frames then exit (benchmark)\n"
           "  --shot PATH     render one frame to a BMP and exit\n"
           "  --dump PATH     write map+tiles+palette and exit (verification)\n"
           "  --at X Y DEG    camera placement for --shot\n"
           "  --replay CSV    drive the kart from the game's own log\n"
           "                  (tools/labs/mame/demo_race.csv); the real kart\n"
           "                  rides along as a ghost.  --replay-kart 1000|1100\n"
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
    replay_path = NULL;
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
        if (!strcmp(a, "--no-pad")) { pad_off = true; continue; }
        ARG("--width", win_w) ARG("--height", win_h) ARG("--pixel", pixel)
        if (!strcmp(a, "--fullscreen")) { fullscreen = 1; continue; }
        if (!strcmp(a, "--frames") && i + 1 < argc) { max_frames = atol(argv[++i]); continue; }
        if (!strcmp(a, "--shot") && i + 1 < argc) { shot = argv[++i]; continue; }
        if (!strcmp(a, "--replay") && i + 1 < argc) { replay_path = argv[++i]; continue; }
        ARG("--replay-kart", replay_kart)
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

    static smk_demolog replay;
    if (replay_path) {
        if (!smk_demolog_load(replay_path, replay_kart, &replay)) {
            fprintf(stderr, "error: cannot read the replay log %s (kart %d)\n",
                    replay_path, replay_kart);
            return 1;
        }
        track = replay.track; theme = -1;
        character = replay.character;
        engine_class = replay.engine_class;
        printf("replay: track %d, character %d, class %d, %d frames\n",
               track, character, engine_class, replay.n);
    }
    if (character < 0 || character >= SMK_CHARACTERS) character = 0;
    const smk_driver *drv = &SMK_DRIVERS[character];
    static smk_sprites karts;
    if (!smk_sprites_load(&rom, drv->sheet, &karts))
        fprintf(stderr, "warning: kart sprites did not load\n");

    static smk_physics phys;
    if (!smk_player_setup(&rom, character, engine_class, &player)) {
        fprintf(stderr, "error: cannot load the player physics tables\n");
        return 1;
    }
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
    smk_blocks_bind(&trk);
    smk_objgfx_load(&rom, trk.theme, &obj_art);   /* the theme's objects */
    if (!smk_horizon_load(&rom, trk.theme, &horizon))
        fprintf(stderr, "warning: horizon not loaded\n");
    if (!smk_effects_load(&rom, &fx))
        fprintf(stderr, "warning: ground effects not loaded\n");

    /* Headless single-frame render: no window, no event loop.  Also the
     * cheapest way to eyeball the renderer from a script. */
    if (shot) {
        int sw = win_w / pixel, sh = win_h / pixel;
        uint32_t *px = malloc((size_t)sw * (size_t)sh * sizeof *px);
        smk_camera c = { .x = shot_x, .y = shot_y, .angle = shot_a,
                       };
        smk_horizon_load(&rom, trk.theme, &horizon);
        smk_render_set_horizon(&horizon, (uint16_t)(shot_a * (float)SMK_ANGLE_TURN
                                                    / (2.0f * (float)M_PI)
                                                    + SMK_ANGLE_TURN / 4));
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
    rom_for_step = &rom;
    /* starting coins: the ROM's table at $81E3DA by the kart's $E6 field,
     * entry 0 = 2 (LABELLED: $E6 is not modelled; the demo starts with 5) */
    player.coins = 2;
    static smk_racer racers[SMK_CHARACTERS];
    int grid[8];
    smk_grid_order(&rom, character, 0, false, grid);
    for (int i = 0; i < SMK_CHARACTERS; i++) {
        smk_racer_start(&racers[i], &crs, i);
        racers[i].character = grid[i];
    }
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
    smk_player_reset(&player, g0h);
    replay_i = 0;
    if (replay_path) {
        replay_i = replay.start;
        smk_demolog_sync(&replay, replay_i, &player, &kart);
        race_state = RACE_RUN;             /* the log starts at the lights */
        racer_draw_mask = 0x02;            /* only the ghost, grid or not */
        show_grid = 1;
    }
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
                    smk_blocks_bind(&trk);
                    smk_objgfx_load(&rom, trk.theme, &obj_art);
                    smk_horizon_load(&rom, trk.theme, &horizon);
                    track = nt; theme = nth;
                    smk_course_start(&crs, 0, &sx, &sy, &sh);
                    kart = (smk_kart){ .x = (int32_t)(sx * SMK_POS_ONE),
                                       .y = (int32_t)(sy * SMK_POS_ONE),
                                       .angle = sh };
                    smk_player_reset(&player, sh);
                    for (int i = 0; i < SMK_CHARACTERS; i++) {
                        smk_racer_start(&racers[i], &crs, i);
                        racers[i].character = grid[i];
                    }
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
            if (replay_path) {
                /* the recorded pad word replaces the player's input, and
                 * the game's own kart rides along as a ghost (slot 1) */
                in.up = in.down = in.left = in.right = in.hop_held = in.hop = false;
                if (replay_i + 1 < replay.n) {
                    const smk_demo_frame *r = &replay.f[++replay_i];
                    in.up = (r->c4 & 0x8000) != 0;
                    in.down = (r->c4 & 0x4000) != 0;
                    in.left = (r->c4 & 0x0200) != 0;
                    in.right = (r->c4 & 0x0100) != 0;
                    in.hop_held = (r->c4 & 0x0030) != 0;
                    in.hop = (r->c4 & 0x000C) != 0;
                    /* coins are collected by the port itself now; the
                     * item use is an input the log only shows by its
                     * effect: the boost state appearing */
                    if (r->drive == 0x10 && replay.f[replay_i - 1].drive != 0x10)
                        smk_player_boost(&player);
                    racers[1].k.x = r->x; racers[1].k.y = r->y;
                    racers[1].k.angle = r->pose;
                    racers[1].k.z = (int32_t)r->z << 8;
                    racers[1].k.airborne = (r->flags & 0x8000) != 0;
                }
            }
            smk_blocks_step();
            step_kart(&kart, &trk, &phys, &in);
            if (replay_path && getenv("SMK_REPLAY_TRACE") && replay_i < replay.n) {
                const smk_demo_frame *r = &replay.f[replay_i];
                double dx = (kart.x - r->x) / 65536.0, dy = (kart.y - r->y) / 65536.0;
                fprintf(stderr, "replay f%d fx %d coins %d/%d pad %04X off %.2f px spd %d/%d head %u/%u st %02X/%02X v %d,%d/%d,%d vlag %d/%d turn %d/%d pos %.3f,%.3f\n",
                        replay_i, fx_state.kind, player.coins, r->coins, r->c4, sqrt(dx * dx + dy * dy), kart.speed, r->speed,
                        player.heading, r->a4, player.state, r->state, kart.vx, kart.vy, r->vx, r->vy,
                        player.vlag, r->vlag, player.turn, r->turn, kart.x / 65536.0, kart.y / 65536.0);
            }
            /* edges are cleared AFTER the tick that consumes them.  Clearing
             * first meant step_kart never saw in.hop, so a hop press did
             * nothing at all - the "no jump" report, twice. */
            input_edges_clear(&in);
            camera_from_kart(&cam, &kart);
            me->k = kart;

            if (race_state == RACE_RUN && !replay_path)
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
                    player_sector = sec;
                }
            }
        }
        (void)stepped;   /* edges deliberately survive a tickless iteration */

        if (tex && fb) {
            smk_render_set_horizon(&horizon, kart.angle);
            if (plane_mask_sz < (size_t)rw * (size_t)rh) {
                free(plane_mask);
                plane_mask_sz = (size_t)rw * (size_t)rh;
                plane_mask = malloc(plane_mask_sz);
            }
            smk_render_set_plane_mask(plane_mask, rw);
            player_below = kart.z < 0;
            smk_render_mode7(&trk, &cam, fb, rw, rh, rw);
            hud_input = (in.left ? 1 : 0) | (in.right ? 2 : 0)
                      | (in.up ? 4 : 0);
            hud_lap = me->lap + 1;
            hud_rank = smk_race_rank(racers, 0, &crs);
            int pframe = frame_for(&in, &lean);
            {
                int af = pframe < 0 ? -pframe : pframe;
                /* the game's $BC frame index: 0 straight, 1..7 the rotation
                 * bands; the drift-onset sheet frame 47 counts as band 1
                 * (LABELLED: its $BC index is not measured) */
                fx_frame_idx = (pframe == 1000) ? 0 : (af == 47 ? 1 : af);
                fx_mirror = pframe != 1000 && pframe < 0;
            }
            draw_scene(&rom, &trk, &karts, drv, &cam, fb, rw, rh,
                       show_grid, show_kart, pframe,
                       kart.angle, racers, &crs);
            draw_speedo(fb, rw, rh, &kart,
                        smk_track_surface(&trk, smk_kart_px(kart.x),
                                          smk_kart_px(kart.y)),
                        player.target);
            /* SMK_REPLAY_SHOT=frame:path - save the rendered frame of a replay */
            if (replay_path && getenv("SMK_REPLAY_SHOT")) {
                static int shot_frame = -2; static char shot_path[512];
                if (shot_frame == -2) {
                    shot_frame = -1;
                    const char *e = getenv("SMK_REPLAY_SHOT");
                    const char *c = strchr(e, ':');
                    if (c) { shot_frame = atoi(e); snprintf(shot_path, sizeof shot_path, "%s", c + 1); }
                }
                if (shot_frame >= 0 && replay_i >= shot_frame) {
                    FILE *pf = fopen(shot_path, "wb");
                    if (pf) {
                        fprintf(pf, "P6\n%d %d\n255\n", rw, rh);
                        for (int i = 0; i < rw * rh; i++) {
                            uint32_t c = fb[i];
                            fputc((c >> 16) & 255, pf); fputc((c >> 8) & 255, pf); fputc(c & 255, pf);
                        }
                        fclose(pf);
                    }
                    in.quit = true;
                }
            }
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
            if (replay_path && replay_i < replay.n) {
                double dx = (kart.x - replay.f[replay_i].x) / 65536.0;
                double dy = (kart.y - replay.f[replay_i].y) / 65536.0;
                size_t len = strlen(title);
                snprintf(title + len, sizeof title - len,
                         "  -  replay f%d/%d  off by %.1f px",
                         replay_i, replay.n, sqrt(dx * dx + dy * dy));
            }
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
