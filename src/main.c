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
    /* the shell: menu navigation and the item button, all edge-triggered */
    bool nav_up, nav_down, nav_left, nav_right, confirm, back;
    bool item;
} input_state;

static void input_edges_clear(input_state *in)
{
    in->next_track = in->prev_track = false;
    in->next_pal = in->prev_pal = in->toggle_filter = false;
    in->hop = false;
    in->nav_up = in->nav_down = in->nav_left = in->nav_right = false;
    in->confirm = in->back = in->item = false;
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
            case SDL_CONTROLLER_BUTTON_BACK:          in->prev_track = true;
                                                      in->back = true; break;
            case SDL_CONTROLLER_BUTTON_START:         in->next_track = true;
                                                      in->confirm = true; break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:       in->nav_up = true; break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     in->nav_down = true; break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     in->nav_left = true; break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    in->nav_right = true; break;
            case SDL_CONTROLLER_BUTTON_A:             in->confirm = true; break;
            /* B is the item button in a race - it must NOT also abandon
             * it, so `back` is Select/Esc only.  The menus accept B as a
             * back too, but they read `item` for that themselves. */
            case SDL_CONTROLLER_BUTTON_B:             in->item = true; break;
            default: break;
            }
        }
        /* Fold in key events as well as polling: a tap shorter than one
         * iteration is invisible to SDL_GetKeyboardState alone. */
        if (e.type == SDL_KEYDOWN && e.key.repeat == 0) {
            switch (e.key.keysym.sym) {
            case SDLK_ESCAPE: in->back = true; break;
            case SDLK_RIGHTBRACKET: in->next_track = true; break;
            case SDLK_LEFTBRACKET:  in->prev_track = true; break;
            case SDLK_p: in->next_pal = true; break;
            case SDLK_o: in->prev_pal = true; break;
            case SDLK_f: in->toggle_filter = true; break;
            case SDLK_SPACE: in->hop = true; break;
            case SDLK_RETURN: case SDLK_KP_ENTER: in->confirm = true; break;
            case SDLK_z: case SDLK_LCTRL: in->item = true; break;
            default: break;
            }
        }
        /* Menu navigation REPEATS while a key is held, so a long list is
         * not tapped through one press at a time.  Steering reads the
         * held state below, so these edges are the menu's alone. */
        if (e.type == SDL_KEYDOWN) {
            switch (e.key.keysym.sym) {
            case SDLK_UP:    case SDLK_w: in->nav_up = true; break;
            case SDLK_DOWN:  case SDLK_s: in->nav_down = true; break;
            case SDLK_LEFT:  case SDLK_a: in->nav_left = true; break;
            case SDLK_RIGHT: case SDLK_d: in->nav_right = true; break;
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
        in->down  |= BTN(X) || lt > TRIG;                 /* SNES Y: brake */
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
/* During the celebration the player's kart is drawn like everybody else -
 * PROJECTED - instead of pinned to SMK_PLAYER_LINE.  The fixed row is
 * right for normal play (your kart is always at the bottom of a SMK
 * screen) and wrong the moment the camera moves, which left the driver
 * jammed against the bottom edge of the first version of this. */
#define SMK_POSE_LEAN 1001      /* the block unfolded; negative = hflipped */
static int celebrating;
/* How far the celebration camera has turned, in ROM angle units.  The
 * SPRITES are projected from a cam_heading passed separately to
 * draw_scene, so rotating cam.angle alone left the ground turning while
 * every kart was projected on the old basis - and the player's own kart
 * simply disappeared. */
static uint16_t finish_yaw;
static int force_steer;           /* SMK_FORCE_STEER: -1 left, +1 right */
static int celebrating_pose;      /* SMK_WIN_POSE: force the arms-up frame */
#define smk_win_pose celebrating_pose
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
static int  hud_countdown;               /* Lakitu's frame, from the arm */
static int  lap_sign_t = -1;             /* his lap sign, from the crossing */
static int  rescue_t;                    /* frames into the $0E drop      */
static int  hud_input;                   /* L/R/accel bits, for the HUD */
/* The start sequence.  SMK holds the karts for a countdown, then runs;
 * our timing is the ROM's own 3-2-1-GO cadence in frames (60/step) -
 * LABELLED: the exact ROM start-frame count is not decoded yet, the
 * cadence is the observable one. */
/* RACE_FINISH is OURS, and deliberately not the ROM's.  The user, asking
 * for it: "the race doesn't stop abruptly.  If you arrive top 4, camera
 * shows you from the front while the character celebrates for a few
 * seconds.  Then after that, you get times."  And on how faithful to be:
 * "faithful is for driving experience, not for hud, menus, and things
 * that can be better without constraints."  So the timing and the camera
 * move below are designed, not measured - the ART and the times are the
 * ROM's.  Ledgered as S27.
 *
 * The simulation KEEPS RUNNING through it, which is the point: the other
 * seven karts have not finished when you do, and their times are what the
 * results screen is for. */
enum { RACE_COUNTDOWN, RACE_RUN, RACE_FINISH };
#define SMK_FINISH_TURN   80     /* frames to swing the camera round     */
#define SMK_FINISH_HOLD  210     /* and how long the celebration lasts   */
#define SMK_FINISH_DIST 34.0f    /* how far ahead of the kart it ends  */
static int race_state = RACE_COUNTDOWN;
static int race_count;                   /* frames spent counting down  */
/* MEASURED (NOTES 145): $809FE1 loads $0146 with $FEB0 = -336 and
 * $80A1F8 increments it once a frame, releasing the karts on the frame it
 * reaches zero.  So the countdown is 336 frames, not the 180 that was
 * invented here.  Confirmed in the user's four-start recording, where
 * $0146 runs -333 at frame 4 to 0 at frame 337 and the race clock $0100
 * starts ticking immediately after. */
static smk_hud hud_art;
static smk_objgfx obj_art;          /* pipes and other entities */
static smk_shadow shadow_art;       /* the one oval every object shares */                  /* the game's own HUD sprites */

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

/* One sprite tile from the HUD stream by its VRAM number, optionally
 * H-flipped, at any palette base.  hud_tile above is this for the $C0
 * row without a flip; the start light needs both. */
static void spr_tile(uint32_t *fb, int rw, int rh, int x, int y, int tile,
                     int pal_base, bool hflip, const uint32_t *palette, int sc)
{
    const uint8_t *px = smk_hud_tile_px(&hud_art, tile);
    if (!px) return;
    for (int ty = 0; ty < 8 * sc; ty++) {
        int sy = y + ty;
        if (sy < 0 || sy >= rh) continue;
        for (int tx = 0; tx < 8 * sc; tx++) {
            int sx = x + tx;
            if (sx < 0 || sx >= rw) continue;
            int cx = tx / sc;
            uint8_t v = px[(ty / sc) * 8 + (hflip ? 7 - cx : cx)];
            if (v) fb[sy * rw + sx] = palette[(pal_base + v) & 0xFF];
        }
    }
}

/* Lakitu and his light (src/lakitu.c).  A 16x16 sprite is the VRAM tiles
 * N, N+1, N+16, N+17 - the 16-tile row stride - and every one of his
 * four quadrants is drawn H-flipped, so the flip is applied across the
 * whole 16 and the two columns swap. */
#define SMK_START_PAL   0xD0            /* sprite palette 5 */
static void draw_start_light(uint32_t *fb, int rw, int rh,
                             const uint32_t *palette, int t)
{
    smk_start st;
    smk_start_frame(t, &st);
    if (!st.on || !hud_art.ok) return;
    int sc = rw >= 640 ? 3 : 2;
    for (int q = 0; q < 4; q++) {
        int qx = (st.x + st.quad[q].dx) * sc, qy = (st.y + st.quad[q].dy) * sc;
        for (int sub = 0; sub < 4; sub++) {
            int cx = sub & 1, cy = sub >> 1;
            int tile = st.quad[q].tile + cx + cy * 16;
            spr_tile(fb, rw, rh, qx + (1 - cx) * 8 * sc, qy + cy * 8 * sc,
                     tile, SMK_START_PAL, true, palette, sc);
        }
    }
    for (int i = 0; i < 3; i++)
        spr_tile(fb, rw, rh, SMK_START_LAMP_X * sc,
                 (st.y + SMK_START_LAMP_DY + i * 8) * sc,
                 st.lamp[i], SMK_HUD_PAL, false, palette, sc);
}

/* Lakitu with the lap sign (NOTES 168).  Four sprites moving as a group;
 * the cloud pair is H-flipped, like every other drawing of him. */
static void draw_lap_sign(uint32_t *fb, int rw, int rh,
                          const uint32_t *palette, int t, int lap, int laps)
{
    smk_lapsign sg;
    smk_lapsign_frame(t, lap, laps, &sg);
    if (!sg.on || !hud_art.ok) return;
    int sc = rw >= 640 ? 3 : 2;
    /* An 8x8 list, because the sign is not four 16x16 blocks: the
     * numeral is ONE column wide and drawing it as half of a 16x16 put
     * two digits on the plate (NOTES 168b). */
    struct { int dx, dy, tile; bool flip; } part[10];
    int n = 0;
    if (sg.final_lap) {
        for (int c = 0; c < 4; c++)
            for (int r = 0; r < 2; r++)
                part[n++] = (typeof(part[0])){ c * 8, r * 8,
                    SMK_LAPSIGN_FINAL_L + c + r * 16, false };
    } else {
        for (int r = 0; r < 2; r++) {
            part[n++] = (typeof(part[0])){ 0,  r * 8, sg.plate + r * 16, false };
            part[n++] = (typeof(part[0])){ 8,  r * 8, sg.plate + 1 + r * 16, false };
            part[n++] = (typeof(part[0])){ 8,  r * 8, SMK_LAPSIGN_BAR + r * 16, false };
            part[n++] = (typeof(part[0])){ 16, r * 8, sg.digit + r * 16, false };
        }
    }
    /* his cloud stays a pair of H-flipped 16x16s */
    for (int q = 0; q < 2; q++)
        for (int sub = 0; sub < 4; sub++) {
            int cx = sub & 1, cy = sub >> 1;
            int base = q ? SMK_LAPSIGN_CLOUD_R : SMK_LAPSIGN_CLOUD_L;
            int qx = (sg.x + (q ? 17 : 1)) * sc, qy = (sg.y + 16) * sc;
            spr_tile(fb, rw, rh, qx + (1 - cx) * 8 * sc, qy + cy * 8 * sc,
                     base + cx + cy * 16, SMK_START_PAL, true, palette, sc);
        }
    for (int i = 0; i < n; i++)
        spr_tile(fb, rw, rh, (sg.x + part[i].dx) * sc,
                 (sg.y + part[i].dy) * sc, part[i].tile,
                 SMK_START_PAL, false, palette, sc);
}

/* Lakitu lowering a fished-out kart back onto the road (NOTES 168a).
 * Only during the DROP phase - through the carry his sprites are parked
 * off the side of the screen, which is the game not drawing him. */
static void draw_rescue_lakitu(uint32_t *fb, int rw, int rh,
                               const uint32_t *palette, int t)
{
    if (!hud_art.ok) return;
    int sc = rw >= 640 ? 3 : 2;
    int y = smk_rescue_y(t);
    static const struct { int dx, dy, tile; } PART[5] = {
        {  0,  0, SMK_RESCUE_TL }, { 16,  0, SMK_RESCUE_TR },
        {  0, 16, SMK_RESCUE_BL }, { 16, 16, SMK_RESCUE_BR },
        { 16, 16, SMK_RESCUE_EXTRA },
    };
    for (int i = 0; i < 5; i++) {
        int qx = (SMK_RESCUE_X + PART[i].dx) * sc, qy = (y + PART[i].dy) * sc;
        for (int sub = 0; sub < 4; sub++) {
            int cx = sub & 1, cy = sub >> 1;
            spr_tile(fb, rw, rh, qx + (1 - cx) * 8 * sc, qy + cy * 8 * sc,
                     PART[i].tile + cx + cy * 16, SMK_START_PAL, true,
                     palette, sc);
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

/* ---- the game shell ------------------------------------------------
 * The race used to be the whole program; it is now one screen of a
 * small state machine (src/menu.c).  These were locals of main(); they
 * move out here so the shell can rebuild a race without threading a
 * dozen pointers through. */
static smk_track    trk;
static smk_course   crs;
static smk_physics  phys;
static smk_sprites  karts;
static smk_racer    racers[SMK_CHARACTERS];
static const smk_driver *drv;
static smk_kart     kart;
static int          grid[8];

static smk_font     menu_font;
static smk_records  records;
static smk_ui       ui;
static smk_ui_result result;
static bool shell;                  /* the menu drives the game        */
static int  race_mode = SMK_MODE_GP;   /* the game's own $2C           */
static long lap_start_frames;       /* clock at the last line crossing */
static bool tt_mushroom;            /* the one time-trial mushroom     */
static bool race_over;
static int  finish_t;                    /* frames into the celebration */
static bool race_reported;
static bool obj_marks;   /* --obj-marks: show each object's ground point */
static smk_autopilot autopilot;
static int  crossings;              /* finish-line crossings this race */

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
    /* Not while Lakitu has it.  smk_collide_objects pushes the kart's
     * POSITION out of a track object, and it does that whatever the
     * kart's height - so during the rescue, where the kart is being
     * carried at z = $3000 and walked to its waypoint two pixels a frame,
     * an object sitting near that waypoint shoves it back out as fast as
     * the walk moves it in.  Neither side wins and the kart is never put
     * down: on Bowser Castle 1 that is a PERMANENT hang, still carrying
     * after eleven thousand frames (NOTES 149a).
     *
     * The wall path already respects height ($80FA5A, NOTES 136).  This
     * is the narrow version of the same rule: a kart in Lakitu's hands is
     * not on the track, so the track cannot touch it. */
    if (course_for_step && !player.hazard) smk_collide_objects(k, course_for_step);
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

/* ---- the race, built and rebuilt by the shell -----------------------
 *
 * Time trial has NO coins and NO item boxes: the running game's tilemap
 * carries the theme's erase tile where a GP has them (NOTES 113, measured
 * off the attract loop's own $2C = 4 demo).  The stamp is the one the coin
 * collector writes, $81:8BBD indexed by theme - the same table pickup.c
 * uses when a coin is taken. */
static void strip_pickups(const smk_rom *rom, smk_track *t)
{
    uint32_t pc = smk_snes_to_pc(rom, 0x818BBDu + (uint32_t)t->theme);
    uint8_t erase = pc < rom->size ? rom->data[pc] : 0;
    for (int i = 0; i < SMK_MAP_BYTES; i++) {
        uint8_t cls = t->surface[t->map[i]];
        if (cls == 0x14 || cls == 0x1A) t->map[i] = erase;
    }
}

static bool load_race(const smk_rom *rom, int track, int theme, int character,
                      int engine_class, int mode)
{
    char err[256];
    if (character < 0 || character >= SMK_CHARACTERS) character = 0;
    if (!smk_track_load(rom, track, theme, &trk, err, sizeof err)) {
        fprintf(stderr, "track %d: %s\n", track, err);
        return false;
    }
    if (!smk_course_load(rom, track, &crs)) {
        fprintf(stderr, "track %d: no course data\n", track);
        return false;
    }
    smk_track_place_objects(rom, &trk);
    if (mode == SMK_MODE_TT) strip_pickups(rom, &trk);
    smk_blocks_bind(&trk);
    smk_objgfx_load(rom, trk.theme, &obj_art);
    smk_shadow_load(rom, &shadow_art);
    smk_horizon_load(rom, trk.theme, &horizon);

    drv = &SMK_DRIVERS[character];
    if (!smk_sprites_load(rom, drv->sheet, &karts))
        fprintf(stderr, "warning: kart sprites did not load\n");
    if (!smk_player_setup(rom, character, engine_class, &player)
        || !smk_physics_load(rom, engine_class, &phys)) {
        fprintf(stderr, "error: physics tables did not load\n");
        return false;
    }

    /* Who drives which kart, and where each kart lines up.  racers[] is
     * indexed by the game's kart BLOCK, which is what smk_grid_order
     * returns and what SMK_GRID_SLOT turns into a row: block 0 - the
     * player - is at the BACK, block 7 on the pole. */
    smk_grid_order(rom, character, 0, false, grid);
    for (int i = 0; i < SMK_CHARACTERS; i++) {
        smk_racer_start(&racers[i], &crs, SMK_GRID_SLOT(i));
        racers[i].character = grid[i];
    }
    /* A time trial is ALONE on the track and the game starts it off the
     * grid, at $818F7F's nudged front position; a race starts the player
     * in his own grid slot, which is the last one (src/course.c). */
    float gx, gy;
    uint16_t gh;
    if (mode == SMK_MODE_TT) smk_course_start_solo(&crs, &gx, &gy, &gh);
    else smk_course_start(&crs, SMK_GRID_SLOT(0), &gx, &gy, &gh);
    kart = (smk_kart){ .x = (int32_t)(gx * SMK_POS_ONE),
                       .y = (int32_t)(gy * SMK_POS_ONE), .angle = gh };
    smk_player_reset(&player, gh);
    /* Starting coins.  A GP kart gets the ROM's table entry ($81E3DA);
     * a time trial starts on ZERO and stays there - measured in the
     * repo's own time-trial log, where P1's $0E00 is 0 on frame 0. */
    player.coins = (mode == SMK_MODE_TT) ? 0 : 2;

    /* A time trial is run ALONE - the attract loop's own $2C = 4 demo has
     * DK on the track by himself (NOTES 113) - so the other seven slots
     * neither step nor draw. */
    racer_draw_mask = (mode == SMK_MODE_TT) ? 0x00 : 0xFE;

    race_mode = mode;
    race_state = RACE_COUNTDOWN;
    race_count = 0;
    hud_race_frames = 0;
    lap_start_frames = 0;
    crossings = 0;
    race_over = false;
    finish_t = 0;
    /* the results screen names the track from the UI's own field, which
     * only the shell sets - so a --race or --timetrial run showed a blank */
    ui.track = track;
    smk_race_frame = 0;
    race_reported = false;
    lap_sign_t = -1;
    smk_autopilot_init(&autopilot);
    /* one mushroom in a time trial, and nothing else (the user's rule for
     * this shell; the ROM's own grant is not decoded - ledger S19) */
    tt_mushroom = (mode == SMK_MODE_TT);
    player.item_held = tt_mushroom;
    memset(&result, 0, sizeof result);
    result.best_slot = -1;
    course_for_step = &crs;
    player_sector = 0;
    fx_state.kind = -1;
    return true;
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


/* A rendered frame to a PPM, for looking at something instead of
 * reasoning about it.  The shot hooks all wrote this out longhand. */
static void save_ppm(const char *path, const uint32_t *fb, int w, int h)
{
    FILE *pf = fopen(path, "wb");
    if (!pf) return;
    fprintf(pf, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        uint32_t c = fb[i];
        fputc((c >> 16) & 255, pf); fputc((c >> 8) & 255, pf); fputc(c & 255, pf);
    }
    fclose(pf);
}

/* Let the unfinished karts finish, so the table has real times.
 *
 * When the player crosses, most of the field has not.  The original waits
 * for them; we run them on without drawing, which costs a few milliseconds
 * and means every row of the results is a time that kart actually drove
 * rather than an estimate.  Capped, because a kart that is genuinely stuck
 * must not hang the results screen - those are shown as DNF.
 */
static void settle_field(smk_racer *rs, const smk_track *t,
                         const smk_course *c, const smk_physics *ph, long now)
{
    const long CAP = 60 * 90;            /* 90 s of race time, then give up */
    for (long f = 0; f < CAP; f++) {
        int left = 0;
        for (int i = 1; i < SMK_CHARACTERS; i++)
            if (rs[i].finish_frame < 0) left++;
        if (!left) break;
        smk_race_frame = now + f;
        smk_ai_rubber(rs, SMK_CHARACTERS, c, ph->engine_class);
        for (int i = 1; i < SMK_CHARACTERS; i++)
            if (rs[i].finish_frame < 0) smk_racer_step(&rs[i], t, c, ph);
    }
}

/* The finishing order and everyone's total, for the results screen. */
static void build_result_table(smk_ui_result *res, smk_racer *rs, long player_total)
{
    rs[0].finish_frame = player_total;         /* the clock the HUD showed */
    int order[SMK_CHARACTERS];
    for (int i = 0; i < SMK_CHARACTERS; i++) order[i] = i;
    /* finished karts by time, then the DNFs; a plain insertion sort, which
     * for eight entries is the clearest thing that can be written */
    for (int i = 1; i < SMK_CHARACTERS; i++) {
        int v = order[i], j = i - 1;
        for (; j >= 0; j--) {
            long a = rs[order[j]].finish_frame, b = rs[v].finish_frame;
            int worse = (a < 0 && b >= 0) || (a >= 0 && b >= 0 && a > b);
            if (!worse) break;
            order[j + 1] = order[j];
        }
        order[j + 1] = v;
    }
    for (int p = 0; p < SMK_CHARACTERS; p++) {
        int i = order[p];
        rs[i].place = p + 1;
        res->field[p].character = rs[i].character;
        res->field[p].total     = rs[i].finish_frame;
        res->field[p].player    = (i == 0);
    }
    res->entries  = SMK_CHARACTERS;
    res->position = rs[0].place;
}

/* The celebration camera: swing round to look the kart in the face.
 *
 * OURS (S27).  The chase camera sits behind the kart at heading + $C0;
 * this eases that round to heading + $C0 + half a turn over
 * SMK_FINISH_TURN frames, so the view swings to the front and holds
 * there while the driver celebrates.  Eased rather than linear (a
 * smoothstep) so it settles instead of stopping dead, and it also drops
 * closer to the ground, which is what makes it read as a camera move
 * rather than the track spinning.
 *
 * The kart keeps being simulated underneath - the other seven have not
 * finished yet, and their times are the whole point of what comes next. */
static void finish_camera(smk_camera *cam, const smk_kart *k, int t)
{
    float u = (float)t / (float)SMK_FINISH_TURN;
    if (u > 1.0f) u = 1.0f;
    u = u * u * (3.0f - 2.0f * u);                 /* smoothstep */

    /* MEASURED, from the user's own recorded race (NOTES 179).
     *
     * The camera azimuth $94 trails the kart's heading $A4 by exactly 192
     * for the whole race - and after the last crossing that difference
     * climbs 192 -> 9555 -> 19421 -> 29735 -> 32836 and settles around
     * 32800, which is $8000: HALF A TURN.  The game really does swing
     * round to look the driver in the face, over about 80 frames.
     *
     * This log had already built it that way, then talked itself out of
     * it because the arms-up sprite looked like a rear view.  The
     * recording settles it: the camera goes to the front. */
    float h  = (float)k->angle * (2.0f * (float)M_PI / (float)SMK_ANGLE_TURN);
    float fx = sinf(h), fy = -cosf(h);             /* the kart's forward */
    float d  = SMK_FINISH_DIST * u;
    cam->x = (float)k->x / (float)SMK_POS_ONE + fx * d;
    cam->y = (float)k->y / (float)SMK_POS_ONE + fy * d;
    cam->angle += u * (float)M_PI;
    finish_yaw = (uint16_t)(u * 32768.0f);         /* the same, for the sprites */
}

/* One art pixel of an object's drawing.
 *
 * Bands 1 and 2 are a single 16x16 drawing and index the sheet directly.
 * Band 0 is the 32x32 metasprite (NOTES 157): four 16x16 sprites, the
 * right column being the left one mirrored, top row from base
 * SMK_OBJ_NEAR_TOP and bottom row from SMK_OBJ_NEAR_BOT. */
static uint8_t obj_texel(int base, int aw, int ax, int ay)
{
    int sbase = base;
    if (aw == SMK_OBJ_NEAR_W) {
        sbase = (ay < 16) ? SMK_OBJ_NEAR_TOP : SMK_OBJ_NEAR_BOT;
        ay &= 15;
        if (ax >= 16) ax = 31 - ax;      /* the mirrored right column */
    }
    int tl = sbase + (ay / 8) * SMK_OBJ_STRIDE + (ax / 8);
    if (tl < 0 || tl >= SMK_OBJ_TILES) return 0;
    return obj_art.px[tl][(ay % 8) * 8 + (ax % 8)];
}


/* The shadow, from the ROM's own 32x8 ellipse.
 *
 * The game draws it as four sprites on ALTERNATE FRAMES so it reads as
 * translucent; we draw the result of that flicker instead - a 50%
 * darkening - because at the port's frame rate a real flicker strobes.
 * `gx` is the ground point and the ellipse's BOTTOM sits on it, which is
 * where the game puts it: the kart's shadow spans rows 95..102 against a
 * ground line of 101.9 (measured in OAM). */
static void draw_shadow(uint32_t *fb, int rw, int rh,
                        float gx, float gy, float sc)
{
    if (!shadow_art.ok) return;
    int w = (int)(SMK_SHADOW_WW * sc + 0.5f);
    int h = (int)(SMK_SHADOW_WH * sc + 0.5f);
    if (w < 2 || h < 1) return;
    int x0 = (int)gx - w / 2, y0 = (int)gy - h;
    for (int dy = 0; dy < h; dy++) {
        int yy = y0 + dy;
        if (yy < 0 || yy >= rh) continue;
        int ty = dy * SMK_SHADOW_H / h;
        for (int dx = 0; dx < w; dx++) {
            int xx = x0 + dx;
            if (xx < 0 || xx >= rw) continue;
            if (!shadow_art.px[ty][dx * SMK_SHADOW_W / w]) continue;
            uint32_t c = fb[yy * rw + xx];
            fb[yy * rw + xx] = 0xFF000000u
                | ((((c >> 16) & 255) * SMK_SHADOW_DARK / 100) << 16)
                | ((((c >> 8) & 255) * SMK_SHADOW_DARK / 100) << 8)
                | (((c & 255) * SMK_SHADOW_DARK / 100));
        }
    }
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
        /* The old ladder of single 16x16 drawings - bases 32/34/36 -
         * is gone; every distance now draws the near metasprite
         * (NOTES 157/158).  Bases 0/2/4/6 were once read here as
         * "skewed perspective variants" because base 0's ink is
         * right-aligned; they are the LEFT HALVES of mirrored pairs,
         * which is the whole reason the near drawing was missed. */
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
        /* No band ladder, and no distance cull.  $84DA18 walks
         * $84DA3C = C0 60 30 00 to pick one of three drawings, and past
         * the last threshold it stops drawing at all - both because the
         * SNES cannot scale a sprite and because it runs out of sprite
         * budget.  We can scale, so the ladder buys us nothing and costs
         * us two things.
         *
         * It costs RESOLUTION, obviously: the far drawings are 10x12 where
         * the near one is 24x32.
         *
         * It also costs CONTINUITY, which is less obvious and is what the
         * user actually sees - "they flip to the good sprite too near the
         * player so it looks also like some magic happened".  The drawings
         * do not fill their blocks by the same fraction: the near
         * metasprite is 24/32 across and 32/32 down, base 34 is 11/16 and
         * 14/16, base 36 is 10/16 and 12/16.  The drawn size is that
         * fraction of a rect the projection sizes, so every band boundary
         * was a jump - about 9% wider and 14% taller at each one.  The
         * object grew in steps as you approached, on top of growing
         * smoothly, which reads exactly like the magic the user described
         * for the pipes before (NOTES 154b).
         *
         * So: the best drawing at every distance.  The size stays the
         * projection's, and now it is continuous all the way in.
         * LABELLED divergence, with S7 and the always-visible objects:
         * the ROM's ladder is a hardware limit we do not have. */
        int obase = SMK_OBJ_PIPE0;         /* unused by the near sampler */
        int aw = SMK_OBJ_NEAR_W, ah = SMK_OBJ_NEAR_H;
        /* The SIZE is the game's own scale, and the band only picks which
         * drawing supplies the detail.
         *
         * `$4200 / zf` is that scale (NOTES 129) - read as 8.8, so 256 is
         * 1:1 - and it lands exactly on the one size ever measured: NOTES
         * 139 put a real pipe at 23 x 31 SNES px against a 12 x 16
         * drawing, and 12 x 16 * ($4200 / (256 * 34)) = 23.3 x 31.1.
         *
         * This also closes S15.  The port used to draw every band at a
         * FIXED size magnified 2x, on the theory that the near art had to
         * come from somewhere we had not found.  It did not: at that
         * reference distance the correct scale simply IS 1.94.  The fixed
         * size is what made a pipe 342 px away and one 3 px away both
         * draw about 24-32 px, so a distant pipe swamped a road that was
         * only a few pixels wide on screen and looked like it stood in
         * the middle of it - "the pipe that should be on the side of the
         * road looks like it was in the middle when you are far" (user,
         * NOTES 154a).  The base pixel was right all along; the mass
         * around it was not. */
        /* Size by the SAME law as the ground and every kart: smk_project's
         * scale, which is screen pixels per world pixel at this depth.
         *
         * The object is simply SMK_OBJ_PIPE_W x SMK_OBJ_PIPE_H world
         * pixels big; nothing else is needed and nothing else can drift
         * out of step with the floor it stands on.
         *
         * The previous version used the game's own `$4200 / zf` as the
         * size, which is measured from the KART while every other scale in
         * the renderer is measured from the EYE, 61 px further back.  Over
         * 40 to 400 world px that shrinks 10x where the ground shrinks
         * 4.6x, so distant pipes came out half the size they should be and
         * then swelled as you approached - "they remain the same until you
         * are relatively close, then scale up faster than any element on
         * screen, so they look like they are getting bigger by magic, not
         * approaching" (user, NOTES 154b).  $4200/zf still picks the BAND;
         * it is a depth cue, not the drawn size. */
        int pw = (int)((float)SMK_OBJ_PIPE_W * sc + 0.5f);
        int ph = (int)((float)SMK_OBJ_PIPE_H * sc + 0.5f);
        if (pw < 1 || ph < 1) return;
        /* --obj-marks: draw where the object's BASE is, and where the road
         * is on that same row, so "is this pipe on the road" can be read
         * off the screen instead of judged by eye.  Magenta cross = the
         * projected ground point; cyan ticks = the road edges at that
         * depth. */
        if (obj_marks) {
            float l2h2 = (float)rh / 112.0f;
            float dep2 = SMK_PROJ_K / (py / l2h2 - SMK_PROJ_H);
            float f22 = dep2 - SMK_CAM_TRAIL;
            float st2 = dep2 / (SMK_PROJ_LES * (float)rw / 256.0f);
            int by = (int)py;
            for (int xx = 0; xx + 1 < rw; xx++) {
                float ux = -sinf(cam->angle) * st2, uy = cosf(cam->angle) * st2;
                float bx = cam->x + cosf(cam->angle) * f22, byw = cam->y + sinf(cam->angle) * f22;
                uint8_t sa2 = smk_track_surface(trk, (int)(bx + ux * (xx - rw / 2.0f)),
                                                     (int)(byw + uy * (xx - rw / 2.0f)));
                uint8_t sb2 = smk_track_surface(trk, (int)(bx + ux * (xx + 1 - rw / 2.0f)),
                                                     (int)(byw + uy * (xx + 1 - rw / 2.0f)));
                bool on1 = smk_surface_cap_frac(sa2) >= 1000 && !smk_surface_solid(sa2);
                bool on2 = smk_surface_cap_frac(sb2) >= 1000 && !smk_surface_solid(sb2);
                if (on1 != on2)
                    for (int t3 = -3; t3 <= 3; t3++)
                        if (by + t3 >= 0 && by + t3 < rh)
                            fb[(by + t3) * rw + xx] = 0xFF00FFFF;
            }
            for (int t3 = -4; t3 <= 4; t3++) {
                int mx = (int)px;
                if (by >= 0 && by < rh && mx + t3 >= 0 && mx + t3 < rw)
                    fb[by * rw + mx + t3] = 0xFFFF00FF;
                if (mx >= 0 && mx < rw && by + t3 >= 0 && by + t3 < rh)
                    fb[(by + t3) * rw + mx] = 0xFFFF00FF;
            }
        }
        /* SMK_ENT_TRACE=1: does the drawn base land where the object
         * actually stands?  Prints the entity's world position and the
         * surface under it, then the world point the GROUND renderer shows
         * at the pixel we drew the base on.  If those differ, the anchor
         * is wrong; if they agree, the object really is where it looks. */
        if (getenv("SMK_ENT_TRACE")) {
            float l2h = (float)rh / 112.0f;
            float dep = SMK_PROJ_K / (py / l2h - SMK_PROJ_H);
            float f2 = dep - SMK_CAM_TRAIL;
            float st = dep / (SMK_PROJ_LES * (float)rw / 256.0f);
            float gx = cam->x + cosf(cam->angle) * f2
                     - sinf(cam->angle) * st * (px - rw / 2.0f);
            float gy = cam->y + sinf(cam->angle) * f2
                     + cosf(cam->angle) * st * (px - rw / 2.0f);
            uint8_t s1 = smk_track_surface(trk, course->ent[i].x, course->ent[i].y);
            uint8_t s2 = smk_track_surface(trk, (int)gx, (int)gy);
            fprintf(stderr, "f%ld ent %d world (%d,%d) surf $%02X %s | drawn base "
                    "(%.0f,%.0f) -> ground (%.0f,%.0f) surf $%02X %s | dist %.0f"
                    " size %dx%d\n",
                    hud_race_frames, i, course->ent[i].x, course->ent[i].y, s1,
                    smk_surface_cap_frac(s1) >= 1000 ? "ROAD" : "off",
                    px, py, gx, gy, s2,
                    smk_surface_cap_frac(s2) >= 1000 ? "ROAD" : "off",
                    zf, pw, ph);
        }
        /* A mover is drawn at its own height (NOTES 152), and that height
         * is in WORLD pixels, so smk_project's own scale converts it -
         * the same law the ground and every sprite use.  Converting at
         * the kart's depth and reusing that at any distance floated far
         * Thwomps a third of the way up the screen. */
        /* The lift is a VERTICAL quantity, so it is projected the way
         * the ground is - into `line` units, which the renderer maps by
         * rh/112 - and NOT by smk_project's `sc`, which is horizontal
         * (rw/d).  Those two agree only when the window happens to be the
         * view's own 256:112; at 1350x505 they differ by 17%, and a
         * Thwomp measured 55.9 lines up where the game puts it 46.8.
         * Inverting smk_project's own line recovers the depth. */
        float depth = (SMK_PROJ_LES * (float)rw / 256.0f) / sc;
        int lift = (int)(smk_mover_world(course, i)
                         * (SMK_PROJ_LES / depth) * ((float)rh / 112.0f));
        /* Anchor the INK, not the rect.
         *
         * The three drawings do not fill their 16x16 block the same way:
         * the near one uses all 16 rows, the middle leaves one blank row
         * below and the far one leaves two.  Hanging the RECT's bottom on
         * the ground point therefore floats the far drawings above the
         * road - and one pixel of vertical error near the horizon is an
         * enormous depth error, because depth = K/(line - H) and its slope
         * is -K/(line-H)^2.  The pipe reads as being much further up the
         * track, and further up the track is nearer the vanishing point:
         * it looks like it is standing in the MIDDLE of the road.  Close
         * up, band 0 has no blank rows and the same pipe looks correctly
         * placed at the roadside - which is exactly what the user reported,
         * twice, for one pipe (NOTES 155b).
         *
         * So: find where the ink actually ends inside the block and put
         * THAT on the ground, and centre on the ink rather than the rect. */
        int ink_b = ah - 1, ink_l = 0, ink_r = aw - 1;
        {
            int lo = 999, hi = -1, bot = -1;
            for (int ay = 0; ay < ah; ay++)
                for (int ax = 0; ax < aw; ax++) {
                    if (!obj_texel(obase, aw, ax, ay)) continue;
                    if (ax < lo) lo = ax;
                    if (ax > hi) hi = ax;
                    if (ay > bot) bot = ay;
                }
            if (bot >= 0) { ink_b = bot; ink_l = lo; ink_r = hi; }
        }
        int inkcx = (ink_l + ink_r + 1) * pw / (2 * aw);
        int x0 = (int)px - inkcx;
        int y0 = (int)py - (ink_b + 1) * ph / ah - lift;

        /* The shadow, under an object that is off the ground.  It is the
         * object's SUB-BLOCK at +$40 that draws this in the game, and the
         * art is the shared oval - the same one the kart hops over, which
         * is why it looks identical under everything (user).  A pipe never
         * leaves the ground, so `lift` is zero and it never gets one. */
        if (lift > 0) draw_shadow(fb, rw, rh, px, py, sc);
        for (int dy = 0; dy < ph; dy++) {
            int yy = y0 + dy;
            if (yy < 0 || yy >= rh) continue;
            int ty = dy * ah / ph;
            for (int dx = 0; dx < pw; dx++) {
                int xx = x0 + dx;
                if (xx < 0 || xx >= rw) continue;
                int tx = dx * aw / pw;
                uint8_t v = obj_texel(obase, aw, tx, ty);
                if (!v) continue;
                fb[yy * rw + xx] =
                    trk->palette[(smk_obj_pal(trk->theme) + v) & 0xFF];
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
            /* The winner's pose is NOT reachable yet, and this is the
             * honest state of it (NOTES 180).  The user's screenshot of
             * the original shows it exactly: face on, mouth open, both
             * white gloves raised.  It lives in the packed frames 33-43,
             * which hold four small sprites each - and those frames do
             * NOT use the tile arrangement smk_sprites_load assumes for a
             * 32x32 (N, N+1, N+16, N+17), so slicing them into quadrants
             * yields heads without bodies and arms without heads.
             * Extracting it is a decode, not a crop.
             *
             * SMK_WIN_POSE=1 draws the quadrant anyway, which is how that
             * was established and is worth keeping for whoever finishes
             * it.  Default OFF: a garbled winner is worse than a plain one. */
            if (celebrating && k == smk_ai_player_block && smk_win_pose) {
                smk_draw_sprite_quad(&other[k], SMK_SPR_WIN_FRAME,
                                     SMK_SPR_WIN_QUAD, trk->palette, d2->pal,
                                     (int)px, (int)py, kscale * 2, false,
                                     fb, rw, rh, rw);
                return;
            }
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
        /* Room for every entity AND every kart.  This used to be
         * SMK_CHARACTERS + 8 = 16, and the entities are enumerated
         * first, so on any course with 16 or more of them the list was
         * full before a single opponent was added and the whole field
         * vanished (NOTES 165). */
        struct { float dep; int kind, idx; } item[SMK_DRAW_LIST];
        int n = 0;
        float a2 = (float)cam_heading * (float)(2.0 * M_PI) / 65536.0f;
        if (course) {
            int nvis = (smk_obj_show_all || !course->nlive)
                     ? course->nent : course->nlive;
            for (int j = 0; j < nvis && n < (int)(sizeof item / sizeof item[0]); j++) {
                int i = (smk_obj_show_all || !course->nlive)
                      ? j : course->live[j];
                float dx = (float)course->ent[i].x - cam->x;
                float dy = (float)course->ent[i].y - cam->y;
                item[n].dep = dx * sinf(a2) + dy * -cosf(a2);
                item[n].kind = 0; item[n].idx = i; n++;
            }
        }
        if (show_grid && karts->frames && racers) {
            /* from ZERO, not one: slot 0 is the player, and the
             * celebration draws him through this projection instead of
             * pinned to SMK_PLAYER_LINE.  Every other caller's mask
             * already clears bit 0 (0xFE racing, 0x00 trial, 0x02 ghost),
             * so the loop bound was doing the mask's job twice and made
             * the celebration's 0xFF silently mean nothing. */
            for (int k = 0; k < SMK_CHARACTERS && n < (int)(sizeof item / sizeof item[0]); k++) {
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
    if (show_kart && !celebrating && karts->frames) {
        int scale = rw / 256;                 /* the SNES 32px proportion */
        if (scale < 1) scale = 1;
        /* the hop lifts the sprite; the shadow stays on the ground */
        int lift = player_height_px * scale;
        /* The fall is a RENDERING effect here and nowhere else (NOTES
         * 142).  The game holds `$1F` at 1 for the whole countdown and
         * animates the sprite; we have no fall animation, so the kart is
         * simply drawn lower each frame.  It is NOT clipped against the
         * plane - that only ever applied because our physics used to put
         * the kart underneath it. */
        if (player.hazard == 6) lift -= player.resc_t * 2 * scale;
        /* Fallen through the track: the kart goes UNDER the plane, so it
         * is hidden by the track and shows only through the hole it fell
         * into - the SNES drops the sprite below BG1 (NOTES 128). */
        if (player_below && plane_mask) smk_draw_set_clip_mask(plane_mask, rw);
        int prow = (int)(SMK_PLAYER_LINE * (float)rh / 112.0f);
        /* The kart's own shadow, which stays on the ground while a hop
         * lifts the sprite (user).  The SAME ROM oval the objects use -
         * this is where it was measured: hopping the kart is what makes
         * the sprite appear in OAM at all. The kart sits at the eye's own
         * trail distance, so that is the scale its shadow is drawn at. */
        if (lift > 0)
            draw_shadow(fb, rw, rh, (float)(rw / 2), (float)prow,
                        SMK_PROJ_LES * (float)rw / 256.0f / SMK_CAM_TRAIL);
        if (frame == SMK_POSE_LEAN || frame == -SMK_POSE_LEAN)
            /* the SAME block as the straight pose, drawn UNFOLDED so its
             * own right half shows: that is the lean (NOTES 182) */
            smk_draw_sprite(karts, SMK_SPR_LEAN, trk->palette, drv->pal,
                            rw / 2, prow - lift, scale,
                            frame < 0, fb, rw, rh, rw);
        else if (frame == 1000)               /* the mirrored straight pose */
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
    if (!celebrating) {
        draw_hud(fb, rw, rh, trk->palette, hud_lap, player.coins, hud_rank);
        draw_clock(fb, rw, rh, trk->palette, hud_race_frames);
    }
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
    /* None of the furniture during the celebration: the point of it is a
     * clean look at the driver, and a FINAL LAP sign over a race that has
     * just been won reads as a bug. */
    if (!celebrating) {
        if (hud_countdown >= 0)           /* Lakitu, and his light */
            draw_start_light(fb, rw, rh, trk->palette, hud_countdown);
        if (lap_sign_t >= 0)              /* and again with the lap sign */
            draw_lap_sign(fb, rw, rh, trk->palette, lap_sign_t,
                          hud_lap, SMK_RACE_LAPS);
        if (player.hazard == 0x0E)        /* and once more, on a rescue */
            draw_rescue_lakitu(fb, rw, rh, trk->palette, rescue_t);
    }
}

/* The player's own view angle.
 *
 * The frame rule itself is measured (NOTES 041); what is still invented is
 * the INPUT to it for the player's kart: in the ROM the camera lags the
 * kart through a turn, and that lag is the relative heading the rule sees.
 * Our camera tracks the kart exactly, so we synthesise a small lag from the
 * steering input.  Encoded as negative-for-hflip in one int. */
/* MEASURED (NOTES 182): the standstill LEAN, which is not a turn.
 *
 * The game draws the player's kart as four 16x16 sprites, and at rest it
 * is SYMMETRIC - the right pair is the left pair with hflip set:
 *
 *   neutral   $180  $180F  $1A0  $1A0F
 *   right     $180  $182   $1A0  $1A2      the halves stop matching
 *   left      $180F $182F  $1A0F $1A2F     the same, whole sprite mirrored
 *
 * Those four tiles are one 32x32 block, so steering simply stops folding
 * the block and draws its own right half instead.  Seventeen per cent of
 * the pixels move: the cap and shoulders lean, the kart's bumper is
 * pixel-identical.  The user: "the images you are providing are not
 * leaning but turning.  different things."  They were - the port picked
 * an adjacent ROTATION frame, which pivots the whole kart. */

static int frame_for(const input_state *in, float *lean)
{
    /* Below speed 16 the ROM's own table turns nothing ($80A9B8[0] = 0,
     * NOTES 175), so there is no rotation to show and the lean is the
     * only thing that moves. */
    if (kart.speed < 16 && (in->left || in->right))
        return in->right ? SMK_POSE_LEAN : -SMK_POSE_LEAN;
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
           "  --menu          start in the menu shell (the default)\n"
           "  --track N       0..23: skip the shell and drive this course\n"
           "  --timetrial     with --track: a solo 5-lap time trial\n"
           "  --autodrive     drive itself (a test aid, not the AI: it gets\n"
           "                  round most courses, not all)\n"
           "  --fast          one simulation tick per frame (headless tests)\n"
           "  --obj-marks     mark each object's ground point (magenta) and the\n"
           "                  road edges at that depth (cyan), to check placement\n"
           "  --rom-spawn     only the ROM's two live objects, which pop in\n"
           "                  and out as you drive; the default shows them all\n"
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
           "  use mushroom  Z or Ctrl / B button   (time trial)\n"
           "  menu          arrows move, Enter selects, Esc goes back\n"
           "  quit          Esc at the title screen\n"
           "\n"
           "environment (debugging and tuning, all optional):\n"
           "  SMK_SHOT=frame:path        write one rendered race frame as a PPM\n"
           "  SMK_START_SHOT=frame:path  the same, counted from the countdown's arm\n"
           "  SMK_AUTODRIVE_TRACE=1      one line per traced frame of --autodrive\n"
           "  SMK_TRACE_WINDOW=lo[:hi]   trace every frame in that range, not every 20th\n"
           "  SMK_AP_LEAD / SMK_AP_DEAD  the autopilot\'s steering damping and deadband\n"
           "  SMK_AP_NOBRAKE / NOSLIDE / NOPROBE   turn one piece of it off, to time\n"
           "                             what that piece is worth (see src/autopilot.c)\n"
           "  SMK_REPLAY_TRACE=1         per-frame diff against a --replay log\n"
           "  SMK_REPLAY_SHOT=frame:path as SMK_SHOT, counted in replay frames\n"
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
    /* The shell (title -> mode -> driver -> course) is the default way in.
     * Anything that names a race outright - --track, --replay, --shot,
     * --dump - skips it, so every existing script and gate still drives
     * the race directly. */
    int explicit_start = 0, force_menu = 0;
    /* Test aids, and an attract mode for free: --timetrial starts a time
     * trial on --track without the shell, and --autodrive steers the
     * player along the course's own direction field ($7F:4000, the same
     * field Lakitu's rescue and the AI use) so a whole five-lap run can be
     * played headlessly. */
    int want_tt = 0, want_race = 0, autodrive = 0;
    /* --scaletest: a straight Mario Circuit road with a line of pipes down
     * the middle at known distances, so one screenshot shows how object
     * scaling compares with the ground's own perspective. */
    int scaletest = 0;
    /* --fast decouples the simulation from the wall clock: exactly one
     * tick per iteration, so a headless run of N frames is N ticks and a
     * whole five-lap trial takes seconds instead of minutes. */
    int fast = 0;
    /* The camera shape is no longer tunable: it is the ROM's own DSP-1
     * geometry (SMK_PROJ_*, NOTES 083/084).  The old --height-cam /
     * --horizon / --fov knobs tuned a projection that no longer exists,
     * so they are removed rather than left lying around as dead controls. */

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        #define ARG(name, var) if (!strcmp(a, name) && i + 1 < argc) { var = atoi(argv[++i]); continue; }
        if (!strcmp(a, "--rom") && i + 1 < argc) { rom_path = argv[++i]; continue; }
        if (!strcmp(a, "--track") && i + 1 < argc) { track = atoi(argv[++i]); explicit_start = 1; continue; }
        if (!strcmp(a, "--menu")) { force_menu = 1; continue; }
        if (!strcmp(a, "--timetrial")) { want_tt = 1; explicit_start = 1; continue; }
        if (!strcmp(a, "--race")) { want_race = 1; explicit_start = 1; continue; }
        if (!strcmp(a, "--autodrive")) { autodrive = 1; continue; }
        if (!strcmp(a, "--fast")) { fast = 1; continue; }
        if (!strcmp(a, "--scaletest")) { scaletest = 1; explicit_start = 1; continue; }
        if (!strcmp(a, "--rom-spawn")) { smk_obj_show_all = false; continue; }
        if (!strcmp(a, "--obj-marks")) { obj_marks = true; continue; }
        ARG("--theme", theme) ARG("--class", engine_class)
        ARG("--character", character)
        if (!strcmp(a, "--no-kart")) { show_kart = 0; continue; }
        if (!strcmp(a, "--no-grid")) { show_grid = 0; continue; }
        if (!strcmp(a, "--no-pad")) { pad_off = true; continue; }
        ARG("--width", win_w) ARG("--height", win_h) ARG("--pixel", pixel)
        if (!strcmp(a, "--fullscreen")) { fullscreen = 1; continue; }
        if (!strcmp(a, "--frames") && i + 1 < argc) { max_frames = atol(argv[++i]); continue; }
        if (!strcmp(a, "--shot") && i + 1 < argc) { shot = argv[++i]; explicit_start = 1; continue; }
        if (!strcmp(a, "--replay") && i + 1 < argc) { replay_path = argv[++i]; explicit_start = 1; continue; }
        ARG("--replay-kart", replay_kart)
        if (!strcmp(a, "--dump") && i + 1 < argc) { dump = argv[++i]; explicit_start = 1; continue; }
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
    drv = &SMK_DRIVERS[character];
    if (!smk_sprites_load(&rom, drv->sheet, &karts))
        fprintf(stderr, "warning: kart sprites did not load\n");

    if (!smk_player_setup(&rom, character, engine_class, &player)) {
        fprintf(stderr, "error: cannot load the player physics tables\n");
        return 1;
    }
    /* $80AF0F, the catch-up distances the AI row chooser indexes. */
    { const char *e = getenv("SMK_AI_SKILL"); if (e) smk_ai_skill = atoi(e); }
    celebrating_pose = getenv("SMK_WIN_POSE") ? 1 : 0;
    /* SMK_FORCE_STEER=-1|1 holds a steering direction with no hands, so
     * the standstill LEAN can be shot and compared against the game's own
     * (NOTES 181).  It is input, not physics: it goes in where the pad
     * would, and below speed 16 the ROM's own table turns nothing. */
    { const char *e = getenv("SMK_FORCE_STEER"); if (e) force_steer = atoi(e); }
    if (!smk_ai_catchup_load(&rom))
        fprintf(stderr, "warning: AI catch-up table not loaded\n");
    if (!smk_physics_load(&rom, engine_class, &phys)) {
        fprintf(stderr, "error: cannot load physics tables\n");
        return 1;
    }
    if (!smk_course_load(&rom, track, &crs)) {
        fprintf(stderr, "error: cannot load course data for track %d\n", track);
        return 1;
    }
    smk_hud_load(&rom, &hud_art);

    if (!smk_track_load(&rom, track, theme, &trk, err, sizeof err)) {
        fprintf(stderr, "error: %s\n", err);
        smk_rom_free(&rom);
        return 1;
    }
    if (scaletest && !have_at) {
        /* stand on the road at the near end looking along it.  The
         * renderer's angle 0 looks along +X, not north - camera_from_kart
         * subtracts a quarter turn to convert the ROM heading. */
        shot_x = 120.0f; shot_y = 512.0f; shot_a = 0.0f;
        have_at = 1;
    }
    if (!have_at) {
        uint16_t sh0;
        smk_course_start_solo(&crs, &shot_x, &shot_y, &sh0);
        shot_a = 0.0f;
    }
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
    if (scaletest) {
        /* Build a ruler: a straight road running north, with pipes down
         * the middle every 40 world px.  The ground is drawn by the Mode 7
         * renderer with the true perspective, so any disagreement between
         * how the road converges and how the pipes shrink is visible in
         * one frame. */
        /* Pick the tiles the real map actually uses most, rather than the
         * first that matches a class - tile 0 is the void and using it as
         * "not found" made the whole thing blank. */
        int hist[SMK_TILE_TOTAL];
        memset(hist, 0, sizeof hist);
        for (int i2 = 0; i2 < SMK_MAP_BYTES; i2++) hist[trk.map[i2]]++;
        int road = -1, off = -1;
        for (int t2 = 1; t2 < SMK_TILE_COUNT; t2++) {
            uint8_t cls = trk.surface[t2];
            if (smk_surface_solid(cls) || hist[t2] == 0) continue;
            if (smk_surface_cap_frac(cls) >= 1000) {
                if (road < 0 || hist[t2] > hist[road]) road = t2;
            } else {
                if (off < 0 || hist[t2] > hist[off]) off = t2;
            }
        }
        if (road < 0 || off < 0) { fprintf(stderr, "scaletest: no tiles\n"); return 1; }
        for (int cy = 0; cy < SMK_MAP_DIM; cy++)
            for (int cx = 0; cx < SMK_MAP_DIM; cx++) {
                int wy = cy * 8;   /* the road runs EAST, along +X */
                trk.map[cy * SMK_MAP_DIM + cx] =
                    (uint8_t)((wy >= 448 && wy < 576) ? road : off);
            }
        crs.nent = 0;
        /* Two rows: one down the CENTRE line and one hugging the left
         * EDGE of the road.  The edge row is the direct test of "a pipe
         * that should be at the side looks like it is in the middle when
         * you are far" - if the far ones drift inward, it is visible
         * against the road's own edge in a single frame. */
        for (int d = 40; d <= 400 && crs.nent < 30; d += 40) {
            crs.ent[crs.nent].kind = 0;
            crs.ent[crs.nent].x = (uint16_t)(120 + d);
            crs.ent[crs.nent].y = 512;          /* centre line */
            crs.nent++;
            crs.ent[crs.nent].kind = 0;
            crs.ent[crs.nent].x = (uint16_t)(120 + d);
            crs.ent[crs.nent].y = 456;          /* just inside the left edge */
            crs.nent++;
        }
        crs.nlive = 0;                 /* draw them all, not a live pair */
        crs.nseg = 0;
        printf("scaletest: road tile %d (class $%02X), off tile %d (class $%02X), "
               "%d pipes every 40 px from 40 to %d ahead\n",
               road, trk.surface[road], off, trk.surface[off],
               crs.nent, 40 * (crs.nent / 2));
    }
    smk_blocks_bind(&trk);
    smk_objgfx_load(&rom, trk.theme, &obj_art);   /* the theme's objects */
    smk_shadow_load(&rom, &shadow_art);
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
                smk_racer_start(&shot_racers[i], &crs, SMK_GRID_SLOT(i));
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
    smk_grid_order(&rom, character, 0, false, grid);
    for (int i = 0; i < SMK_CHARACTERS; i++) {
        smk_racer_start(&racers[i], &crs, SMK_GRID_SLOT(i));
        racers[i].character = grid[i];
    }
    smk_racer *me = &racers[0];

    float lean = 0.0f;
    float g0x, g0y;
    uint16_t g0h;
    smk_course_start(&crs, SMK_GRID_SLOT(0), &g0x, &g0y, &g0h);
    kart = (smk_kart){
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

    /* the shell */
    shell = !explicit_start || force_menu;
    smk_ui_init(&ui);
    if (!smk_font_load(&rom, &menu_font))
        fprintf(stderr, "warning: menu font not loaded\n");
    smk_records_load(&records);
    if (shell) {
        ui.player_sel = character;
        ui.engine_class = engine_class;
        printf("lap records: %s\n", smk_records_path());
    } else {
        ui.screen = SMK_UI_RACE;
        race_mode = SMK_MODE_GP;
        if (want_tt && !replay_path) {
            load_race(&rom, track, theme, character, engine_class, SMK_MODE_TT);
            camera_from_kart(&cam, &kart);
        } else if (want_race && !replay_path) {
            /* A full eight-kart race with no shell, so --autodrive --fast
             * makes the AI measurable headlessly (SMK_ROW_TRACE). */
            load_race(&rom, track, theme, character, engine_class, SMK_MODE_GP);
            camera_from_kart(&cam, &kart);
        }
    }

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
        accum += fast ? TICK_DT : dt;

        bool stepped = false;
        while (accum >= TICK_DT) {
            accum -= TICK_DT;
            stepped = true;

            /* ---- the shell ------------------------------------- */
            if (shell && ui.screen != SMK_UI_RACE) {
                smk_ui_input nav = { in.nav_up, in.nav_down, in.nav_left,
                                     in.nav_right, in.confirm,
                                     in.back || in.item };
                if (ui.screen == SMK_UI_TITLE && in.back) in.quit = true;
                if (smk_ui_step(&ui, &rom, &nav)) {
                    /* a single race IS a Grand Prix course on its own */
                    int m = (ui.mode_sel == SMK_UI_MODE_TT)
                            ? SMK_MODE_TT : SMK_MODE_GP;
                    if (load_race(&rom, ui.track, -1, ui.player_sel,
                                  ui.engine_class, m)) {
                        track = ui.track; theme = -1;
                        character = ui.player_sel;
                        engine_class = ui.engine_class;
                        camera_from_kart(&cam, &kart);
                    } else {
                        ui.screen = SMK_UI_COURSE;
                    }
                }
                input_edges_clear(&in);
                continue;
            }
            /* Esc in a shell race abandons it; in a direct race it quits */
            if (in.back) {
                if (shell) { ui.screen = SMK_UI_COURSE; input_edges_clear(&in); continue; }
                in.quit = true;
            }
            /* the one mushroom */
            if (in.item && tt_mushroom && race_state == RACE_RUN
                && smk_player_boost(&player)) {
                tt_mushroom = false;
                player.item_held = false;
            }
            /* the debug track cycle belongs to the direct mode only */
            if (shell) in.next_track = in.prev_track = false;
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
                    smk_course_start(&crs, SMK_GRID_SLOT(0), &sx, &sy, &sh);
                    kart = (smk_kart){ .x = (int32_t)(sx * SMK_POS_ONE),
                                       .y = (int32_t)(sy * SMK_POS_ONE),
                                       .angle = sh };
                    smk_player_reset(&player, sh);
                    for (int i = 0; i < SMK_CHARACTERS; i++) {
                        smk_racer_start(&racers[i], &crs, SMK_GRID_SLOT(i));
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
            /* One clock for the whole start.  race_count is the frame
             * number $0146 counts, and it does NOT stop at the release:
             * Lakitu holds the green, cheers, and climbs back out over
             * the next hundred frames (src/lakitu.c), so it runs on to
             * SMK_START_LAST and the drawing reads it the whole way. */
            if (race_count < SMK_START_LAST) race_count++;
            hud_countdown = race_count;
            if (race_state == RACE_COUNTDOWN) {
                /* The lights.  The kart is held, but the throttle is NOT
                 * ignored - it builds the rev, and where the rev sits when
                 * the lights go out decides whether you get the turbo
                 * launch, nothing, or a wheelspin (NOTES 143). */
                /* SMK_START_HOLD=t - hold the throttle from countdown
                 * frame t, so the three launches (over-rev, turbo, plain)
                 * can be shot without a pair of hands. */
                bool thr = in.up;
                {
                    static int hold = -2;
                    if (hold == -2) {
                        const char *e = getenv("SMK_START_HOLD");
                        hold = e ? atoi(e) : -1;
                    }
                    if (hold >= 0 && race_count >= hold) thr = true;
                }
                smk_player_rev(&player, thr, (unsigned)race_count);
                if (race_count >= SMK_COUNT_FRAMES) {
                    race_state = RACE_RUN;
                    smk_player_launch(&player);   /* $80956A pays out here */
                }
                /* Throttle and hop are consumed here; STEERING IS NOT.
                 * The user: "When stopped (speed=0) and you press left or
                 * right, the cart doesn't turn, the player only leans
                 * their head left or right.  Nothing else.  This can be
                 * tested easily during count down."  Zeroing left/right
                 * threw that away, so our driver sat rigid through the
                 * whole countdown.
                 *
                 * Keeping them cannot turn the kart: below speed $80 the
                 * heading moves by $80A9B8[(speed>>4)&7], and that table
                 * begins 0, 16, 32, 48 - entry ZERO for speeds 0..15.
                 * Verified by holding LEFT for 90 frames at a standstill:
                 * the heading moves by exactly $0000.  All they reach is
                 * the sprite's lean (NOTES 175). */
                in.up = in.down = false;
                in.hop_held = false;
            }
            if (race_state == RACE_RUN || race_state == RACE_FINISH) {
                hud_race_frames++;
                smk_race_frame = hud_race_frames;   /* so a kart can stamp its own finish */
            }
            if (race_state == RACE_FINISH) finish_t++;
            if (lap_sign_t >= 0 && ++lap_sign_t > SMK_LAPSIGN_FRAMES)
                lap_sign_t = -1;
            /* his own clock for the drop, so the path plays at the rate
             * it was captured at rather than off the kart's height */
            rescue_t = (player.hazard == 0x0E) ? rescue_t + 1 : 0;
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
            if (autodrive && race_state == RACE_RUN && !replay_path) {
                /* The autopilot presses buttons and nothing else, so the
                 * kart it drives is subject to every rule the player's is
                 * (src/autopilot.c). */
                smk_autopilot_out ap;
                smk_autopilot_step(&autopilot, &trk, &crs, &player, &kart, &ap);
                in.up = ap.accel; in.down = ap.brake;
                in.left = ap.left; in.right = ap.right;
                in.hop = ap.hop; in.hop_held = ap.hop_held;
            }
            static long trace_lo = -1, trace_hi = -1;
            if (trace_lo < 0) {
                const char *w = getenv("SMK_TRACE_WINDOW");
                if (w) { trace_lo = atol(w); const char *c = strchr(w, ':');
                         trace_hi = c ? atol(c + 1) : trace_lo + 60; }
                else { trace_lo = 0; trace_hi = 0; }
            }
            if (autodrive && getenv("SMK_AUTODRIVE_TRACE")
                && (trace_hi > 0
                    ? (hud_race_frames >= trace_lo && hud_race_frames <= trace_hi)
                    : hud_race_frames % 20 == 0)
                && race_state == RACE_RUN)
                fprintf(stderr, "f%ld pos %d,%d spd %d/%d sec %d/%d aim %d need %04X dev %d slide %d haz %d z %d surf %02X lost %d\n",
                        hud_race_frames, smk_kart_px(kart.x), smk_kart_px(kart.y),
                        kart.speed, autopilot.dbg_limit, player_sector,
                        autopilot.sector,
                        autopilot.dbg_aim, autopilot.dbg_need,
                        autopilot.dbg_dev, autopilot.slide, player.hazard,
                        (int)(kart.z >> 8),
                        smk_track_surface(&trk, smk_kart_px(kart.x), smk_kart_px(kart.y)),
                        autopilot.lost);
            smk_blocks_step();
            /* Thwomps are parked through lap one and released when it is
             * complete - crossing 2 is the first finished lap (NOTES
             * 148/152). */
            smk_course_movers_step(&crs, crossings >= 2);
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
            if (force_steer) {
                in.left  = force_steer < 0;
                in.right = force_steer > 0;
            }
            celebrating = (race_state == RACE_FINISH);
            /* the winner coasts: hands off the controls once you cross.
             * The user, on what matters: "celebration is important,
             * continuing driving is not". */
            if (celebrating) {
                in.up = in.down = in.left = in.right = false;
                in.hop_held = in.hop = in.item = false;
            }
            if (!celebrating) finish_yaw = 0;
            racer_draw_mask = celebrating
                ? (getenv("SMK_CEL_SOLO") ? 0x01 : 0xFF)
                : (race_mode == SMK_MODE_TT ? 0x00 : 0xFE);
            if (race_state == RACE_FINISH) {
                finish_camera(&cam, &kart, finish_t);
                if (getenv("SMK_FINISH_TRACE") && finish_t % 10 == 0)
                    fprintf(stderr, "cel t=%3d kart(%d,%d) h=%04X cam(%.0f,%.0f) a=%.2f\n",
                            finish_t, smk_kart_px(kart.x), smk_kart_px(kart.y),
                            kart.angle, cam.x, cam.y, cam.angle);
            }
            me->k = kart;

            if ((race_state == RACE_RUN || race_state == RACE_FINISH)
                && !replay_path && race_mode != SMK_MODE_TT) {
                /* the rubber band, before anybody moves (NOTES 167) */
                smk_ai_rubber(racers, SMK_CHARACTERS, &crs, engine_class);
                /* SMK_ROW_TRACE: one line per frame of every AI's $C8 row
                 * and speed, in the same shape flaglog.lua logs the real
                 * game, so tools/labs/rowmix.py can put ours and the
                 * ROM's row mixture side by side (NOTES 174). */
                if (getenv("SMK_ROW_TRACE")) {
                    printf("row %ld", total_frames);
                    for (int q = 0; q < SMK_CHARACTERS; q++)
                        printf(" %d,%d,%d,%d,%d,%d", racers[q].row * 2,
                               racers[q].k.speed, racers[q].rank,
                               racers[q].branch,
                               smk_kart_px(racers[q].k.x),
                               smk_kart_px(racers[q].k.y));
                    printf("\n");
                }
                for (int i = 1; i < SMK_CHARACTERS; i++)
                    smk_racer_step(&racers[i], &trk, &crs, &phys);
                /* Kart against kart, once a frame over the whole field
                 * (NOTES 166).  racers[0] IS the player's kart - me->k is
                 * copied from it above - so the player takes part on the
                 * same terms as everyone else, and the response lands
                 * back on `kart` when the pass is done. */
                smk_kart *field[SMK_CHARACTERS];
                uint8_t wt[SMK_CHARACTERS];
                for (int i = 0; i < SMK_CHARACTERS; i++) {
                    field[i] = &racers[i].k;
                    wt[i] = SMK_KART_WEIGHT[racers[i].character
                                            % SMK_CHARACTERS];
                }
                if (getenv("SMK_NO_BUMP")) {
                    /* A/B: run the field with kart contact switched off */
                } else if (getenv("SMK_BUMP_TRACE")) {
                    static int8_t was[SMK_CHARACTERS];
                    smk_karts_collide(field, wt, SMK_CHARACTERS);
                    for (int i = 0; i < SMK_CHARACTERS; i++) {
                        if (field[i]->bump_cool == SMK_BUMP_COOL && !was[i])
                            printf("bump f%ld: kart %d (%s, weight $%02X)"
                                   " speed %d at (%d,%d)\n",
                                   hud_race_frames, i,
                                   SMK_DRIVERS[racers[i].character
                                               % SMK_CHARACTERS].name,
                                   wt[i], field[i]->speed,
                                   smk_kart_px(field[i]->x),
                                   smk_kart_px(field[i]->y));
                        was[i] = field[i]->bump_cool;
                    }
                } else {
                    smk_karts_collide(field, wt, SMK_CHARACTERS);
                }
                kart = me->k;
            }

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
                                /* he comes back with the sign (NOTES 168);
                                 * the grid crossing enters lap 1 and shows
                                 * nothing, so this starts at lap 2 */
                                if (me->lap >= 2) lap_sign_t = 0;
                                if (race_state == RACE_RUN && !race_over)
                                    race_over = smk_tt_crossing(
                                        &result, &crossings,
                                        &lap_start_frames, hud_race_frames);
                            }
                        } else if (sec >= crs.sectors - 2 && me->sector <= 1) {
                            me->lap--;
                            me->lap_cool = 90;
                            /* `crossings` is deliberately NOT decremented:
                             * the forward branch above is guarded by
                             * progress_max, so a re-crossing never counts
                             * twice, and a counter that only ever goes up
                             * cannot desync from the splits it indexes. */
                        }
                    }
                    me->sector = sec;
                    player_sector = sec;
                }
            }

            /* the race is over: report it, bank the best lap, show it */
            if (race_over && !race_reported) {
                race_reported = true;
                char tm[16];
                /* In a race the place is what you finished in, so it is
                 * taken on the crossing rather than left to the HUD's
                 * per-frame value, which keeps moving as the AI carry on. */
                result.position = (race_mode == SMK_MODE_TT)
                                  ? 0 : smk_race_rank(racers, 0, &crs);
                printf("%s: %s, %s, %s\n",
                       race_mode == SMK_MODE_TT ? "time trial" : "race",
                       smk_track_name(&rom, track), drv->name,
                       engine_class == 0 ? "50cc" : engine_class == 1 ? "100cc" : "150cc");
                if (result.position)
                    printf("  finished %d of %d\n", result.position, SMK_CHARACTERS);
                for (int i = 0; i < SMK_RACE_LAPS; i++) {
                    smk_time_text(result.lap[i], tm, sizeof tm);
                    printf("  lap %d  %s%s\n", i + 1, tm,
                           result.lap[i] == result.best_lap ? "   best" : "");
                }
                smk_time_text(result.total, tm, sizeof tm);
                printf("  total  %s\n", tm);
                if (autodrive)
                    printf("  steering: %d reversals, mean heading error %.2f deg\n",
                           autopilot.dbg_flips,
                           autopilot.dbg_err_n
                             ? (double)autopilot.dbg_err_sum / autopilot.dbg_err_n
                               * 360.0 / 65536.0 : 0.0);
                /* An autodriven run is the direction field's lap, not the
                 * player's, so it is reported but never banked. */
                if (!autodrive && race_mode == SMK_MODE_TT) {
                    result.best_slot = smk_records_add(&records, track,
                                                       result.best_lap, character);
                    if (result.best_slot >= 0) smk_records_save(&records);
                }
                /* Not straight to the results screen any more.  The
                 * user: "the race doesn't stop abruptly."  The field is
                 * still racing and their times are what the results are
                 * for, so hand over to the celebration and let it run. */
                me->finish_frame = hud_race_frames;
                if (race_mode == SMK_MODE_TT) {
                    if (shell || getenv("SMK_RESULT_SHOT")) ui.screen = SMK_UI_RESULT;
                } else {
                    race_state = RACE_FINISH;
                    finish_t = 0;
                }
            }

            /* the celebration is over: settle the field and show the times */
            if (race_state == RACE_FINISH
                && finish_t >= SMK_FINISH_TURN + SMK_FINISH_HOLD) {
                race_state = RACE_RUN;          /* nothing more to celebrate */
                settle_field(racers, &trk, &crs, &phys, hud_race_frames);
                build_result_table(&result, racers, result.total);
                printf("  final order:\n");
                for (int q = 0; q < result.entries; q++) {
                    char tt[16];
                    smk_time_text(result.field[q].total, tt, sizeof tt);
                    printf("   %d  %-8s %s%s\n", q + 1,
                           SMK_DRIVERS[result.field[q].character
                                       % SMK_CHARACTERS].name,
                           result.field[q].total >= 0 ? tt : "DNF",
                           result.field[q].player ? "   <- you" : "");
                }
                if (shell || getenv("SMK_RESULT_SHOT")) ui.screen = SMK_UI_RESULT;
            }
        }
        (void)stepped;   /* edges deliberately survive a tickless iteration */

        /* SMK_RESULT_SHOT draws the results screen without the shell, which
         * --race bypasses - otherwise the layout can only be seen by
         * playing through the menus. */
        if (tex && fb && (shell || getenv("SMK_RESULT_SHOT"))
            && ui.screen != SMK_UI_RACE) {
            if (ui.screen == SMK_UI_RESULT) {
                smk_ui_draw_result(&ui, &rom, &menu_font, &records, &result,
                                   fb, rw, rh);
                /* SMK_RESULT_SHOT=path - the finished results screen */
                if (getenv("SMK_RESULT_SHOT")) {
                    save_ppm(getenv("SMK_RESULT_SHOT"), fb, rw, rh);
                    in.quit = true;
                }
            }
            else
                smk_ui_draw(&ui, &rom, &menu_font, &records, trk.palette,
                            fb, rw, rh);
            SDL_UpdateTexture(tex, NULL, fb, rw * (int)sizeof *fb);
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex, NULL, NULL);
            SDL_RenderPresent(ren);
        } else if (tex && fb) {
            smk_render_set_horizon(&horizon, kart.angle);
            if (plane_mask_sz < (size_t)rw * (size_t)rh) {
                free(plane_mask);
                plane_mask_sz = (size_t)rw * (size_t)rh;
                plane_mask = malloc(plane_mask_sz);
            }
            smk_render_set_plane_mask(plane_mask, rw);
            player_below = kart.z < 0;   /* a real drop, not the fall countdown */
            smk_render_mode7(&trk, &cam, fb, rw, rh, rw);
            hud_input = (in.left ? 1 : 0) | (in.right ? 2 : 0)
                      | (in.up ? 4 : 0);
            /* The lap SHOWN is the crossing count, not one more than it:
             * the grid is behind the line, so the first crossing enters
             * lap 1 rather than completing it ($8089C9 skips $8000).  This
             * used to read me->lap + 1 and so showed LAP 2 from the first
             * time you passed the flag. */
            hud_lap = me->lap < 1 ? 1 : me->lap;
            if (hud_lap > SMK_RACE_LAPS) hud_lap = SMK_RACE_LAPS;
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
                       (uint16_t)(kart.angle + finish_yaw), racers, &crs);
            if (!celebrating)
                draw_speedo(fb, rw, rh, &kart,
                            smk_track_surface(&trk, smk_kart_px(kart.x),
                                              smk_kart_px(kart.y)),
                            player.target);
            if (race_mode == SMK_MODE_TT)
                smk_ui_draw_splits(&menu_font, &result,
                                   hud_race_frames - lap_start_frames,
                                   result.laps_done, tt_mushroom, fb, rw, rh);
            /* SMK_FINISH_SHOT=t:path - a frame of the celebration, counted
             * from the moment the player crosses, so the camera swing can
             * be looked at rather than argued about. */
            if (getenv("SMK_FINISH_SHOT") && race_state == RACE_FINISH) {
                static int fwant = -2; static char fpath[512];
                if (fwant == -2) {
                    fwant = -1;
                    const char *e = getenv("SMK_FINISH_SHOT");
                    const char *c = strchr(e, ':');
                    if (c) { fwant = atoi(e); snprintf(fpath, sizeof fpath, "%s", c + 1); }
                }
                if (fwant >= 0 && finish_t >= fwant) {
                    save_ppm(fpath, fb, rw, rh);
                    fwant = -1;
                }
            }
            /* SMK_START_SHOT=frame:path - the same, counted from the
             * countdown's arm instead, which is the only way to see a
             * frame of the start sequence. */
            if (getenv("SMK_START_SHOT")) {
                static int swant = -2; static char spath[512];
                if (swant == -2) {
                    swant = -1;
                    const char *e = getenv("SMK_START_SHOT");
                    const char *c = strchr(e, ':');
                    if (c) { swant = atoi(e); snprintf(spath, sizeof spath, "%s", c + 1); }
                }
                if (swant >= 0 && hud_countdown >= swant) {
                    FILE *pf = fopen(spath, "wb");
                    if (pf) {
                        fprintf(pf, "P6\n%d %d\n255\n", rw, rh);
                        for (int i = 0; i < rw * rh; i++) {
                            uint32_t c = fb[i];
                            fputc((c >> 16) & 255, pf); fputc((c >> 8) & 255, pf);
                            fputc(c & 255, pf);
                        }
                        fclose(pf);
                    }
                    in.quit = true;
                }
            }
            /* SMK_SHOT=frame:path - save a rendered frame of a live race,
             * counted from the lights.  The replay path has its own below. */
            if (getenv("SMK_SHOT")) {
                static int want = -2; static char path[512];
                if (want == -2) {
                    want = -1;
                    const char *e = getenv("SMK_SHOT");
                    const char *c = strchr(e, ':');
                    if (c) { want = atoi(e); snprintf(path, sizeof path, "%s", c + 1); }
                }
                if (want >= 0 && hud_race_frames >= want) {
                    FILE *pf = fopen(path, "wb");
                    if (pf) {
                        fprintf(pf, "P6\n%d %d\n255\n", rw, rh);
                        for (int i = 0; i < rw * rh; i++) {
                            uint32_t c = fb[i];
                            fputc((c >> 16) & 255, pf); fputc((c >> 8) & 255, pf);
                            fputc(c & 255, pf);
                        }
                        fclose(pf);
                    }
                    in.quit = true;
                }
            }
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
