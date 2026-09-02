/* SDL2 host for the Super Mario Kart reimplementation. */
#include "smk.h"
#include "itemart.inc"
#include "flatart.inc"
#include "rrthwomp.inc"
#include "fishart.inc"
#include "molart.inc"

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
    bool dpad_down;         /* the d-pad held DOWN (level): item + DOWN drops it behind */
    bool dpad_up;           /* the d-pad held UP (level): item + UP throws it AHEAD */
    bool toggle_map;
    bool toggle_speedo;      /* H: the telemetry overlay, which is OURS */        /* M: the track map on / off */
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
            case SDLK_m: in->toggle_map = true; break;
            case SDLK_h: in->toggle_speedo = true; break;
            case SDLK_n: smk_music_toggle(); break;
            case SDLK_LEFT:  case SDLK_a: in->nav_left = true; break;
            case SDLK_RIGHT: case SDLK_d: in->nav_right = true; break;
            default: break;
            }
        }
    }
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    in->up    = k[SDL_SCANCODE_UP]    || k[SDL_SCANCODE_W];
    in->down  = k[SDL_SCANCODE_DOWN]  || k[SDL_SCANCODE_S];
    in->dpad_down = k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S];
    in->dpad_up   = k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W];
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
        in->dpad_down |= BTN(DPAD_DOWN)
                      || SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) > DEAD;
        in->dpad_up   |= BTN(DPAD_UP)
                      || SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) < -DEAD;
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
/* Coins knocked out of the player, and the game's own art for them. */
static smk_coinart coin_art;
/* The item system (docs/ITEMS.md): the word, the tables, the projectiles. */
static smk_itemtab itemtab;
static smk_item    item;
static smk_itemicons item_icons;
static smk_projart   proj_art;
static int         cur_track;        /* for the item block ($81:8B73[track]) */
static smk_proj    projs[SMK_PROJ_MAX];
static unsigned    item_rng = 0x2545F491u;
static unsigned item_roll(void)
{
    /* OURS: five random bits.  The game's $1F26 is not reproduced. */
    item_rng = item_rng * 1103515245u + 12345u;
    return (item_rng >> 16) & 31u;
}
static smk_flagart flag_art;
static smk_coin    coins_fx[SMK_COINFX_MAX];
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
static bool item_used_once;              /* the slot shows the used look after it */
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
        /* Paint order = OAM priority: the plate is the lower entry (NOTES
         * 168 lists it first), so it sits ON TOP of the digit sprite whose
         * left half is the edge bar.  Bar first, plate last - drawing the
         * bar over the plate put a stripe through "LAP" (the user's
         * screenshot). */
        for (int r = 0; r < 2; r++) {
            part[n++] = (typeof(part[0])){ 8,  r * 8, SMK_LAPSIGN_BAR + r * 16, false };
            part[n++] = (typeof(part[0])){ 16, r * 8, sg.digit + r * 16, false };
            part[n++] = (typeof(part[0])){ 0,  r * 8, sg.plate + r * 16, false };
            part[n++] = (typeof(part[0])){ 8,  r * 8, sg.plate + 1 + r * 16, false };
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

/* Lakitu waving the chequered flag as you finish (NOTES 184).
 *
 * The same 8x8 tile list the lap sign uses, from the same shared blob and
 * the same palette; only the group and the path differ.  The flag waves
 * through three tile pairs, which is the whole animation. */
static void draw_finish_flag(uint32_t *fb, int rw, int rh,
                             const uint32_t *palette, int t)
{
    smk_flag fg;
    smk_flag_frame(t, &fg);
    if (!fg.on || !flag_art.ok) return;
    int sc = rw >= 640 ? 3 : 2;
    /* The WHOLE group from one source (NOTES 184).  Offsets from his head,
     * measured off OAM and steady through the pass:
     *   ( 0, 0) head   ( 0,16) cloud L   (16,16) cloud R
     *   (16, 0) flag upper       (24, 8) flag lower  */
    /* BACK TO FRONT, which is the game's own OAM order reversed - lower
     * slots paint on top there, and at the finish they run
     *   8 $6C (flag lower)  9 $4A (head)  10 $6E (flag upper)
     *   11 $4C (cloud L)   12 $44 (cloud R)
     * so his HEAD covers part of the flag's upper half.  Painting the
     * whole flag last instead, as the first version did, leaves the
     * triangle complete and the group reads as a detached sprite. */
    const uint8_t *art[5] = {
        flag_art.cloud[1], flag_art.cloud[0],
        flag_art.px[fg.pose][0],          /* $6E, upper - behind his head */
        flag_art.head,
        flag_art.px[fg.pose][1],          /* $6C, lower - in front        */
    };
    static const struct { int dx, dy; } AT[5] = {
        { 16, 16 }, { 0, 16 }, { 16, 0 }, { 0, 0 }, { 24, 8 },
    };
    /* the flag's OAM attribute is $6A through the whole pass (tmp/
     * lakflag.log: tiles $6C/$6E/$80/$82/$8C/$8E) - bit 6 set, H-FLIPPED.
     * Drawn straight it sat on the wrong hand and read as a broken
     * sprite (the user, twice). */
    static const bool HF[5] = { false, false, true, false, true };
    for (int p = 0; p < 5; p++) {
        int x0 = (fg.x + AT[p].dx) * sc, y0 = (fg.y + AT[p].dy) * sc;
        for (int yy = 0; yy < 16 * sc; yy++) {
            int sy = y0 + yy;
            if (sy < 0 || sy >= rh) continue;
            const uint8_t *row = art[p] + (yy / sc) * 16;
            for (int xx = 0; xx < 16 * sc; xx++) {
                int sx = x0 + xx;
                if (sx < 0 || sx >= rw) continue;
                uint8_t v = row[HF[p] ? 15 - xx / sc : xx / sc];
                if (!v) continue;
                fb[(size_t)sy * rw + sx] = palette[(SMK_START_PAL + v) & 0xFF];
            }
        }
    }
}

/* Lakitu lowering a fished-out kart back onto the road (NOTES 168a).
 * Only during the DROP phase - through the carry his sprites are parked
 * off the side of the screen, which is the game not drawing him. */
static int rescue_draw_lift;        /* the kart's drawn lift this frame */
/* after the drop he TAKES HIS FEE and rises; the kart is held until he
 * is gone (round 2, bugs 1/15 - the user: "you saw it taking two coins
 * from you with the rod, and going up and you were not released until
 * it was gone").  OURS: the pacing. */
#define SMK_LAKITU_EXIT 64
static int lakitu_exit_t;
static void draw_rescue_lakitu(uint32_t *fb, int rw, int rh,
                               const uint32_t *palette, int t, int rise)
{
    if (!hud_art.ok) return;
    int sc = rw >= 640 ? 3 : 2;
    /* LAKITU RIDES WITH THE KART (bug 3): through the whole drop his row
     * is the kart's sprite top minus 27 and his block sits 15 px left of
     * the kart's (NOTES 195's capture, both from the same frames).  The
     * kart's drawn top = the player line minus its lift minus 32. */
    int scale2 = rw / 256; if (scale2 < 1) scale2 = 1;
    int kart_top = (int)(SMK_PLAYER_LINE * (float)rh / 112.0f) / sc
                 - (rescue_draw_lift + 32 * scale2) / sc;
    int y = kart_top - 27 - rise;
    (void)t;
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
/* OURS: a small closed circuit beside the lap count, so the pair reads
 * as "lap 1 of 5" rather than two loose numbers.  The game has no such
 * icon - the kart icon it does have in that corner means LIVES - and the
 * user asked for it deliberately generic: "just a typical kidney-shaped
 * track as an icon", so it never has to match the course being raced.
 * One byte a row, bit 7 leftmost, drawn dark-then-light so it reads over
 * sky, sand and tarmac alike. */
static const uint8_t TRACK_ICON[8] = {
    0x3C,   /* ..####.. */
    0x42,   /* .#....#. */
    0x81,   /* #......# */
    0x81,   /* #......# */
    0x99,   /* #..##..# */
    0x81,   /* #......# */
    0x42,   /* .#....#. */
    0x3C,   /* ..####.. */
};

static void draw_track_icon(uint32_t *fb, int rw, int rh, int x, int y, int sc)
{
    /* The outline is the cells NEXT TO the shape and not part of it.
     * Ringing every set cell instead filled the loop's own hole and the
     * icon came out a solid blob. */
    for (int r = -1; r < 9; r++)
        for (int c = -1; c < 9; c++) {
            int on = (r >= 0 && r < 8 && c >= 0 && c < 8)
                   && (TRACK_ICON[r] & (0x80 >> c));
            int near = 0;
            for (int k = 0; k < 4 && !near; k++) {
                static const int nx[4] = { -1, 1, 0, 0 }, ny[4] = { 0, 0, -1, 1 };
                int rr = r + ny[k], cc = c + nx[k];
                if (rr >= 0 && rr < 8 && cc >= 0 && cc < 8
                    && (TRACK_ICON[rr] & (0x80 >> cc))) near = 1;
            }
            /* ...and never INSIDE the loop: an outline there closes the
             * hole and the icon reads as a solid blob. */
            if (!on && (r >= 2 && r <= 5 && c >= 2 && c <= 5)) continue;
            if (!on && !near) continue;
            uint32_t col = on ? 0xFFF0F0F8u : 0xFF201820u;
            for (int dy = 0; dy < sc; dy++)
                for (int dx = 0; dx < sc; dx++) {
                    int px = x + c * sc + dx, py = y + r * sc + dy;
                    if (px >= 0 && px < rw && py >= 0 && py < rh)
                        fb[py * rw + px] = col;
                }
        }
}

/* The dashboard (NOTES 248/249).  The user, correcting the first cut:
 * "the kart shows the number of lifes remaining, not the lap count.  The
 * game doesn't show lap number because it is lakitu who annouces that.
 * The position is the big number next to those two.  I don't want to
 * implement lifes, and I do want to add speed."
 *
 * So of the three things the game draws in that corner - lives, coins,
 * position - the port draws two.  Read off the ROM's own OAM
 * (tools/labs/hudlayout.py): the coin icon $4F, the multiply sign $4E and
 * up to two digits, at y 92, x 190/198/206/214.  The row above it, at
 * y 84, is the kart icon and the LIVES, and is deliberately not drawn.
 *
 * The POSITION is a big number to the right of both rows.  The game's is
 * background-layer art, not a sprite - it is nowhere in OAM - so this is
 * the ROM's own digit at twice the scale.  LABELLED.
 *
 * SPEED is the user's own addition and the original never had it, so it
 * sits under the block in the same digits with no icon. */
static void draw_hud(uint32_t *fb, int rw, int rh, const uint32_t *palette,
                     int coins, int rank, int speed, int lap)
{
    if (!hud_art.ok) return;
    int sc  = rw >= 640 ? 3 : 2;
    int adv = 8 * sc;
    /* the block is seven tiles wide: five for a coin row, two for the
     * big position digit that sits to its right, as the game has it */
    int x = rw - adv * 7 - 8, y = 8;

    /* COINS, in the game's own shape */
    int c = coins < 0 ? 0 : (coins > 99 ? 99 : coins);
    hud_tile(fb, rw, rh, x + adv,     y, 0x4F - SMK_HUD_TILE0, palette, sc);
    hud_tile(fb, rw, rh, x + adv * 2, y, 0x4E - SMK_HUD_TILE0, palette, sc);
    if (c >= 10)
        hud_tile(fb, rw, rh, x + adv * 3, y, smk_hud_digit(c / 10), palette, sc);
    hud_tile(fb, rw, rh, x + adv * (c >= 10 ? 4 : 3), y,
             smk_hud_digit(c % 10), palette, sc);

    /* POSITION: the big number beside it.  OURS: the digit is the ROM's,
     * the doubling is not (the game's own is on a background layer). */
    int r = rank < 1 ? 1 : (rank > 8 ? 8 : rank);
    hud_tile(fb, rw, rh, x + adv * 5, y - sc * 2, smk_hud_digit(r), palette, sc * 2);

    /* The LAP, back with the other numbers rather than sitting on the
     * dial (the user).  It is OURS - the game shows no lap at all, Lakitu
     * announces it (NOTES 249) - and it goes UNDER the coins, with no
     * kart icon: that icon means LIVES here. */
    if (lap > 0) {
        int ld = lap > 9 ? 9 : lap;
        int ly = y + adv;
        draw_track_icon(fb, rw, rh, x + adv, ly, sc);
        hud_tile(fb, rw, rh, x + adv * 2, ly, smk_hud_digit(ld), palette, sc);
        hud_tile(fb, rw, rh, x + adv * 3, ly, 0xA2 - SMK_HUD_TILE0, palette, sc);
        hud_tile(fb, rw, rh, x + adv * 4, ly,
                 smk_hud_digit(SMK_RACE_LAPS), palette, sc);
    }
    (void)speed;    /* the speed is a NEEDLE now, drawn by draw_gauge */
}

/* ---- the speedometer, which is entirely OURS ------------------------
 *
 * The user: "can we implement a needle instead a raw number? because
 * that number doesn't mean much" - and they are right, the game's own
 * speed is an internal 0..~1000 with no units anyone could read.  A
 * needle needs no units: what it shows is speed AGAINST THIS CLASS'S TOP,
 * which is the number that actually matters to a driver.
 *
 * The dial sweeps 200 degrees, from 190 (lower left) round to -10 (lower
 * right).  The arc past the SURFACE CAP is drawn in amber, so driving
 * off-road shows as a needle pinned against a wall it cannot pass.
 * `top` is $D6, the class top adjusted for coins; `capfrac` is the
 * surface's own fraction of it, per mille. */
static void gauge_px(uint32_t *fb, int rw, int rh, int x, int y, uint32_t c)
{
    if (x >= 0 && x < rw && y >= 0 && y < rh) fb[y * rw + x] = c;
}

static void gauge_line(uint32_t *fb, int rw, int rh, float cx, float cy,
                       float a, float r0, float r1, uint32_t col, int thick)
{
    float dx = cosf(a), dy = -sinf(a);
    int steps = (int)(r1 - r0) + 1;
    for (int i = 0; i <= steps; i++) {
        float r = r0 + (r1 - r0) * (float)i / (float)steps;
        for (int t = 0; t < thick; t++) {
            float o = (float)t - (float)(thick - 1) * 0.5f;
            gauge_px(fb, rw, rh, (int)(cx + dx * r - dy * o),
                     (int)(cy + dy * r + dx * o), col);
        }
    }
}

#define GAUGE_A0  (190.0f * (float)M_PI / 180.0f)   /* the zero end   */
#define GAUGE_A1  (-10.0f * (float)M_PI / 180.0f)   /* the full end   */

/* Half-dark the pixels under the dial: over a road it is legible on its
 * own, over a row of item boxes it was not. */
static void gauge_shade(uint32_t *fb, int rw, int rh, int cx, int cy, int rad)
{
    for (int dy = -rad - 3; dy <= rad / 3 + 3; dy++)
        for (int dx = -rad - 3; dx <= rad + 3; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 > (rad + 3) * (rad + 3)) continue;
            int x = cx + dx, y = cy + dy;
            if (x < 0 || x >= rw || y < 0 || y >= rh) continue;
            uint32_t p = fb[y * rw + x];
            fb[y * rw + x] = 0xFF000000u
                           | (((p & 0x00FF0000u) >> 1) & 0x00FF0000u)
                           | (((p & 0x0000FF00u) >> 1) & 0x0000FF00u)
                           | (((p & 0x000000FFu) >> 1) & 0x000000FFu);
        }
}

static void draw_gauge(uint32_t *fb, int rw, int rh, int cx, int cy, int rad,
                       int speed, int top, int capfrac)
{
    gauge_shade(fb, rw, rh, cx, cy, rad);
    /* The scale is ABSOLUTE - SMK_SPEED_MAX, the fastest any kart can go
     * anywhere (NOTES 251) - not this class's top.  The user: "make the
     * top of the dial an absolute number, so if I use a mushroom, I am
     * able to hit the high."  Against a class-top scale the needle sat at
     * full deflection all down every straight and a boost had nowhere to
     * show. */
    float frac = (float)speed / (float)SMK_SPEED_MAX;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    float topf = top > 0 ? (float)top / (float)SMK_SPEED_MAX : 1.0f;
    if (topf > 1.0f) topf = 1.0f;
    float capf = capfrac >= 1000 ? topf : topf * (float)capfrac / 1000.0f;

    /* the arc: dark to the surface cap, amber from there to the class
     * top, and dimmer still past it - the stretch only a boost reaches */
    for (int i = 0; i <= 160; i++) {
        float u = (float)i / 160.0f;
        float a = GAUGE_A0 + (GAUGE_A1 - GAUGE_A0) * u;
        uint32_t col = u > topf ? 0xFF283038u
                     : u > capf ? 0xFFB07020u : 0xFF303038u;
        gauge_line(fb, rw, rh, (float)cx, (float)cy, a,
                   (float)rad - 2.0f, (float)rad, col, 1);
    }
    /* five ticks across the whole scale */
    for (int i = 0; i <= 4; i++) {
        float u = (float)i / 4.0f;
        float a = GAUGE_A0 + (GAUGE_A1 - GAUGE_A0) * u;
        gauge_line(fb, rw, rh, (float)cx, (float)cy, a,
                   (float)rad * 0.72f, (float)rad - 2.0f, 0xFFE0E0E8u, 1);
    }
    /* and a longer white mark at THIS class's top, so the stretch a
     * mushroom reaches is visible as "past the marker" */
    {
        float a = GAUGE_A0 + (GAUGE_A1 - GAUGE_A0) * topf;
        gauge_line(fb, rw, rh, (float)cx, (float)cy, a,
                   (float)rad * 0.60f, (float)rad, 0xFFFFFFFFu, 2);
    }
    /* the needle, and a hub so its pivot reads as one */
    float na = GAUGE_A0 + (GAUGE_A1 - GAUGE_A0) * frac;
    uint32_t ncol = frac > topf + 0.01f ? 0xFF60FF60u      /* boosting */
                  : (capfrac < 1000 && frac >= capf - 0.01f) ? 0xFFFFA030u
                  : 0xFFFF4040u;
    gauge_line(fb, rw, rh, (float)cx, (float)cy, na, 0.0f,
               (float)rad * 0.80f, ncol, 3);
    for (int dy2 = -2; dy2 <= 2; dy2++)
        for (int dx2 = -2; dx2 <= 2; dx2++)
            if (dx2 * dx2 + dy2 * dy2 <= 4)
                gauge_px(fb, rw, rh, cx + dx2, cy + dy2, 0xFFE0E0E8u);
}
/* OURS, and marked as such (ROADMAP item 11): speed, surface, slip and
 * the class-top bar are telemetry the original never had.  H toggles
 * it; SMK_NO_TELEMETRY=1 starts with it off. */
static bool show_speedo;      /* off: the dial replaced it.  H brings it back */
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
    /* the dial owns the bottom-left corner now, so the raw metrics sit
     * above it when H turns them on */
    int sc = rw >= 640 ? 2 : 1;
    int x = 8, y = rh - 10 * sc - 8 - 40 * sc;
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
/* the loaded ROM, so the per-frame sound code can read its tables
 * (the overtake voices, NOTES 235) without threading it through */
static const smk_rom *the_rom;
static const smk_driver *drv;
static smk_kart     kart;
static int          grid[8];

static smk_font     menu_font;
static smk_records  records;
static smk_ui       ui;
static smk_ui_result result;
/* THE TRACK MAP (the user's list, item 12): the whole course as a small
 * picture with every kart on it.  The original only has one because its
 * screen is split; ours must not cost the big view, so the ART is the
 * course's own tilemap, downsampled, and the PLACEMENT is ours: the
 * bottom-right corner, translucent.  OURS, ledgered (S33). */
#define SMK_MAP_PX 96
static uint32_t track_map[SMK_MAP_PX * SMK_MAP_PX];
static bool     track_map_ok;
static bool     show_map = true;
static bool     obj_whole;   /* the sheet's near art is a whole 16x16, not a mirrored half */
static void build_track_map(const smk_track *t)
{
    const int step = SMK_WORLD_PX / SMK_MAP_PX;        /* world px per map px */
    for (int y = 0; y < SMK_MAP_PX; y++)
        for (int x = 0; x < SMK_MAP_PX; x++) {
            /* a 2x2 sample inside the cell, averaged, so thin walls survive */
            unsigned r = 0, g = 0, b = 0;
            for (int q = 0; q < 4; q++) {
                uint32_t c = smk_track_texel(t, x * step + (q & 1) * (step / 2) + step / 4,
                                                y * step + (q >> 1) * (step / 2) + step / 4);
                r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255;
            }
            track_map[y * SMK_MAP_PX + x] = 0xFF000000u | ((r / 4) << 16) | ((g / 4) << 8) | (b / 4);
        }
    track_map_ok = true;
}
static void draw_track_map(uint32_t *fb, int rw, int rh, const smk_kart *me,
                           const smk_racer *rs, int nrs)
{
    if (!track_map_ok || !show_map) return;
    int sc = rw / 512; if (sc < 1) sc = 1;                /* 96 px at 512 wide */
    int size = SMK_MAP_PX * sc;
    int x0 = rw - size - 8 * sc, y0 = rh - size - 8 * sc;
    for (int y = 0; y < size; y++) {
        int sy = y0 + y; if (sy < 0 || sy >= rh) continue;
        for (int x = 0; x < size; x++) {
            int sx = x0 + x; if (sx < 0 || sx >= rw) continue;
            uint32_t m = track_map[(y / sc) * SMK_MAP_PX + x / sc], d = fb[(size_t)sy * rw + sx];
            /* 3/4 map over 1/4 scene: readable, and the road shows through it */
            fb[(size_t)sy * rw + sx] = 0xFF000000u
                | ((((m >> 16) & 255) * 3 + ((d >> 16) & 255)) / 4) << 16
                | ((((m >> 8) & 255) * 3 + ((d >> 8) & 255)) / 4) << 8
                | ((((m) & 255) * 3 + ((d) & 255)) / 4);
        }
    }
    /* the karts: a dot each, the player's larger and gold, drawn last */
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < nrs; i++) {
            bool player = (i == 0);
            if (player != (pass == 1)) continue;
            const smk_kart *k = player ? me : &rs[i].k;
            int mx = x0 + smk_kart_px(k->x) * size / SMK_WORLD_PX;
            int my = y0 + smk_kart_px(k->y) * size / SMK_WORLD_PX;
            int rad = (player ? 2 : 1) * sc;
            uint32_t col = player ? 0xFFFFD040u : 0xFFF0F0F0u;
            for (int dy = -rad; dy <= rad; dy++)
                for (int dx = -rad; dx <= rad; dx++) {
                    int sx = mx + dx, sy = my + dy;
                    if (sx < 0 || sx >= rw || sy < 0 || sy >= rh) continue;
                    if (dx * dx + dy * dy > rad * rad + rad) continue;
                    fb[(size_t)sy * rw + sx] = col;
                }
        }
}
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
static bool engine_throttle;        /* B held: the rev, countdown included */
static int  fx_kind_now = -1;       /* the ground effect this frame ($80:D37A) */
/* sounds queued a few frames ahead: the game spaces some pairs out */
static struct { int id, t; } sfx_later[4];
static void smk_sfx_after(int id, int frames)
{
    for (int i = 0; i < (int)(sizeof sfx_later / sizeof sfx_later[0]); i++)
        if (sfx_later[i].t <= 0) { sfx_later[i].id = id; sfx_later[i].t = frames; return; }
}

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
    k->star = (player.flags & 2) ? 1 : 0;
    k->hazard_hit = 0;
    if (course_for_step && !player.hazard) smk_collide_objects(k, course_for_step);
    if (k->hazard_hit == 2) {                        /* bug 13: flattened */
        player.squash_t = SMK_SQUASH_T;
        if (getenv("SMK_SQUASH_TRACE")) printf("squash f%ld: the player\n", hud_race_frames);
    } else if (k->hazard_hit == 3) {                 /* bug 12: the mole grabs on */
        if (!player.mole_on) {
            player.mole_on = 1; player.mole_hops = 0;
            if (getenv("SMK_MOLE_TRACE")) printf("mole ON f%ld\n", hud_race_frames);
        }
    } else if (k->hazard_hit) {
        /* a plant or a fish: MEASURED by driving into one in the oracle
         * (NOTES 229) - Choco's plants play $23, from $80:B66A, the
         * same routine the ramps use, because they throw you as a ramp
         * does */
        smk_sfx_play(SMK_SFX_RAMP);
        smk_player_hit_banana(&player, k);
    }
    /* a mole on the kart drags it to a crawl - it "sticks forever" until
     * shaken (the recording's rides sit at crawl speeds; the cap and the
     * three-hop shake-off are OURS) */
    if (player.mole_on && k->speed > 0x100) k->speed = 0x100;
    if (player.squash_t > 0) { player.squash_t--; k->speed = 0; k->speed_frac = 0; }
    /* the rescue's EXIT (round 2, bugs 1/15): the moment the drop ends,
     * Lakitu takes his two-coin fee and rises; the kart is held (speed 0)
     * until he is gone */
    {
        static int was_hazard;
        if (was_hazard == 0x0E && player.hazard == 0) {
            if (getenv("SMK_RESCUE_TRACE"))
                printf("rescue DONE f%ld: the fee and the exit\n", hud_race_frames);
            lakitu_exit_t = SMK_LAKITU_EXIT;
            int fee = player.coins < 2 ? player.coins : 2;
            player.coins -= fee;
            if (fee && !replay_path)
                smk_coinfx_spawn(coins_fx, SMK_COINFX_MAX, kart.x, kart.y,
                                 player.heading, kart.vx, kart.vy, fee);
        }
        was_hazard = player.hazard;
    }
    if (lakitu_exit_t > 0) { lakitu_exit_t--; k->speed = 0; k->speed_frac = 0; }
    /* the collector ($81B73B) serves ONE player per frame, alternating:
     * every P1 pickup in the demo lands on an odd frame, every P2 pickup
     * on an even one, with the cell the kart is on after that frame's
     * move (NOTES 110).  A coin crossed on the other parity is missed if
     * the kart has left its cell by the next frame - as in the game. */
    {
        unsigned parity = replay_path ? (unsigned)replay_i : fx_ticks;
        unsigned mine = (replay_path && replay_kart == 1100) ? 0u : 1u;
        if (rom_for_step && (parity & 1u) == mine) {
            bool had = player.item_held;
            {   /* a coin taken: the count moved.  Its hop is NOTES 189 */
                int before = player.coins;
                smk_pickup_step(rom_for_step, trk, &player, k, grounded);
                if (player.coins != before && !replay_path) {
                    smk_coinfx_pickup(coins_fx, SMK_COINFX_MAX);
                    smk_sfx_play(SMK_SFX_COIN);         /* MEASURED (NOTES 214) */
                }
            }
            /* a fresh box: choose the item and start the roulette ($81:B34A
             * and the pick at $81:B6D1, docs/ITEMS.md §3).  The lap and
             * rank are the HUD's, which are the race's own. */
            if (player.item_held && !had && itemtab.ok && !replay_path) {
                smk_item_box(&item, &itemtab, cur_track, hud_lap, hud_rank - 1, item_roll());
            }
        }
    }

    /* THE SOUND EFFECTS (NOTES 211/214).  The ROM plays these from
     * inside the routines this port transcribed; rather than scatter
     * calls through player.c (which the headless tools link WITHOUT the
     * audio), the same events are read off the state machine here, once
     * a frame.  Which id belongs to which event is now mostly MEASURED -
     * the ids were watched firing against the game's own state in the
     * user's recordings - and the rest is the user's ear. */
    {
        uint8_t surf = smk_track_surface(trk, smk_kart_px(k->x), smk_kart_px(k->y));
        fx_kind_now = smk_effects_pick(surf, !k->airborne && k->z == 0,
                                       (player.flags & 0x0008) != 0,
                                       (player.flags & 0x0020) != 0, k->speed);
    }
    {   /* A shell off a wall.  The DISTANCE rule is measured and is in:
         * $84:D9DA weighs each player's distance to the object against
         * $0120 = 288, and the table entries for "out of range" are ZERO,
         * i.e. a bounce you are far from is silent.  The port had no gate
         * at all - the user: "right now you hear them bouncing no matter
         * how far you are".
         *
         * The SOUND is not settled.  $2A was wrong (it is the kart's own
         * spin-out, NOTES 233) and $30 is wrong too: rendered, it comes
         * back the same length, the same 2410 Hz and the same rising
         * sweep as $48, the mushroom boost - which is exactly what the
         * user heard.  The baseline-diff renderer has now produced two
         * such collisions ($32 came back as $50), so the id stays
         * UNRESOLVED and the port plays nothing rather than something
         * known to be wrong. */
        for (int i = 0; i < SMK_PROJ_MAX; i++)
            if (projs[i].bounced) {
                projs[i].bounced = 0;
                float dx = (float)(smk_kart_px(projs[i].x) - smk_kart_px(kart.x));
                float dy = (float)(smk_kart_px(projs[i].y) - smk_kart_px(kart.y));
                float d2 = dx * dx + dy * dy;
                int id = d2 <= 96.0f * 96.0f    ? SMK_SFX_OBJ_WALL
                       : d2 <= 192.0f * 192.0f  ? SMK_SFX_OBJ_WALL_2
                       : d2 <= (float)(SMK_SFX_OBJ_RANGE * SMK_SFX_OBJ_RANGE)
                                                ? SMK_SFX_OBJ_WALL_3 : 0;
                if (id) smk_sfx_play(id);
            }
    }
    {   /* sounds the game spaces out - the coin item is TWO coins, and
         * the recording puts its two $20s seven frames apart */
        for (int i = 0; i < (int)(sizeof sfx_later / sizeof sfx_later[0]); i++)
            if (sfx_later[i].t > 0 && --sfx_later[i].t == 0)
                smk_sfx_play(sfx_later[i].id);
    }
    if (!replay_path) {
        static int was_state, was_hazard2, was_mole, was_air, was_shrink, was_drive;
        static int16_t was_speed;
        static int8_t was_bump;
        static int was_boo;
        static uint8_t was_surf;
        int st = player.state;
        uint8_t surf_now = smk_track_surface(trk, smk_kart_px(k->x), smk_kart_px(k->y));
        bool spin = (st == 0x0A || st == 0x0C || st == 0x1A);
        bool was_spin = (was_state == 0x0A || was_state == 0x0C || was_state == 0x1A);
        /* MEASURED (NOTES 233): poking the tumble state ($A6 = $1A)
         * makes the game play $2A from $80:B75A - so that is being spun
         * out, whatever hit you.  The port had $29 here by ear. */
        if (spin && !was_spin) smk_sfx_play(SMK_SFX_SPIN_OUT);
        if (player.hazard != was_hazard2 && player.hazard == 6)
            smk_sfx_play(SMK_SFX_FALL);              /* off the road   */
        if (player.mole_on && !was_mole) smk_sfx_play(SMK_SFX_MOLE);
        if (k->airborne && !was_air && player.state != 0x18) {
            /* Three different take-offs, and the ROM keeps them apart
             * (NOTES 229, each forced by filling the surface):
             *   a RAMP  (class $2C, and the $10/$12/$1C launches) -> $23
             *   the BUMP (class $2A)  -> $21, or $4E while boosting
             *   a HOP    (the shoulder button) -> $21
             */
            uint8_t c = surf_now & 0xFE;
            if (c == 0x2C || c == 0x10 || c == 0x12 || c == 0x1C)
                smk_sfx_play(SMK_SFX_RAMP);
            else
                smk_sfx_play(player.drive == 0x10 ? SMK_SFX_JUMP_BIG : SMK_SFX_HOP);
        }
        if (!k->airborne && was_air && player.state != 0x18) {
            /* $80:B1F0 picks the landing sound off the surface: class
             * $5C and up, or water, land SOFT ($4C); everything else is
             * $25.  One routine, two sounds - which is why $4C looked
             * like "the mud surface" from correlation alone. */
            uint8_t c = surf_now & 0xFE;
            smk_sfx_play(c >= 0x5C || c == 0x22 ? SMK_SFX_LAND_SOFT : SMK_SFX_LAND);
        }
        /* a WALL is the $3F/$40/$41 family ($80:D7DA, indexed by
         * $AE & 7); a KART is $42 or $54 by the impact ($80:D818).
         * Both forced in the oracle (NOTES 228). */
        /* THE OVERTAKE VOICES (NOTES 235).  The user: "check the sound
         * they make when passing by".  $84:EF05 is the whole rule -
         * compare the rank with the remembered one, and if it moved
         * (and the $0042 cooldown has run out) somebody speaks: your
         * own voice when you gain a place, and when you LOSE one the
         * voice of the driver who has just gone past.  Only Bowser, DK
         * Jr and Toad have one of those; the rest overtake in silence,
         * which is the ROM's own 0 in the table, not a gap in ours. */
        {
            static int rank_prev = -1, rank_cool;
            if (race_state != RACE_RUN) { rank_prev = -1; rank_cool = 0; }
            else if (rank_cool > 0) rank_cool--;
            else {
                int r = racers[0].rank;
                if (rank_prev >= 0 && r != rank_prev) {
                    int id = 0;
                    if (r < rank_prev) {          /* we passed somebody */
                        id = smk_sfx_pass_voice(the_rom,
                                 racers[0].character % SMK_CHARACTERS, true);
                    } else {                       /* somebody passed us */
                        for (int q = 0; q < SMK_CHARACTERS; q++)
                            if (racers[q].rank == r - 1) {
                                id = smk_sfx_pass_voice(the_rom,
                                         racers[q].character % SMK_CHARACTERS, false);
                                break;
                            }
                    }
                    if (id) smk_sfx_play(id);
                    rank_cool = SMK_SFX_PASS_COOL;
                }
                rank_prev = r;
            }
        }
        /* A wall is TWO sounds, not one (NOTES 245).  Over the 'crash'
         * session every one of 17 wall hits fires $3C from $84:D77F and
         * then $3F from $80:D7F4 one frame later - 17 of 17, never one
         * without the other.  The port had only $3F, which is why the
         * user heard bumping into walls and pipes as thin or missing.
         * $3C is the OBJECT-vs-wall family's kart sibling ($80:FBCA,
         * NOTES 239); $3F is $80:D7DA's own table. */
        if (k->bounce_hit) {
            smk_sfx_play(SMK_SFX_WALL_THUD);
            smk_sfx_play(SMK_SFX_WALL);
        }
        if (k->bump_cool == SMK_BUMP_COOL && was_bump != SMK_BUMP_COOL)
            smk_sfx_play(was_speed > 0x500 ? SMK_SFX_BUMP_HARD : SMK_SFX_BUMP_SOFT);
        /* THE SKID (NOTES 221/223).  A HELD voice, and the user pinned
         * exactly when it should sound: "it should sound exactly at the
         * same times you are displaying some smoke from the tyres" -
         * which is the game's own condition, not a slide threshold of
         * mine.  Effect kind $24 is the drift smoke on a ROAD class
         * ($80:D37A's own branch); the other surfaces have their own
         * kinds and their own sounds, which is why the user hears a
         * different one off-road. */
        smk_sfx_loop("skid", fx_kind_now == 0x24 && race_state == RACE_RUN);
        /* THE OFF-ROAD HISS (NOTES 236).  The user: "when driving on
         * grass there is a sound coming up, like S-S-S".  It is never
         * queued, so no amount of tapping the sound entry finds it -
         * DSP voice 5 keys sample $04 over and over while the kart is on
         * rough ground.  FORCED rather than hunted: RAM $0B00 is the
         * tilemap-byte -> class table, so the whole course can be made
         * one class and the same recorded inputs run over each in turn.
         * Of fourteen classes exactly two hiss, at their own pitches -
         * $5A at $0600, $58 at $0400 - and the voice is re-keyed about
         * every 10 frames, which is the loop file's own length.
         * OURS: the speed gate (the measurement only ever saw it at one
         * speed, so whether it thins out slowly is untested). */
        {
            /* Five of them, and each class has its OWN voice (NOTES 236,
             * extended in 237 after the user asked for the bridge and
             * named Mario Circuit's off-road).  Forced class by class
             * and diffed against the road control:
             *   $50 bridge          sample $16 at pitch $0300
             *   $54 MC off-road     sample $00, pitch dithered $0280..$0400
             *   $58 sand            sample $04 at pitch $0400  (pulsed)
             *   $5A grass           sample $04 at pitch $0600  (pulsed)
             *   $5C mud             sample $12 at pitch $0A00
             * $44 and $52 have none.  The two pulsed ones re-key on a
             * 10-frame period; the other three are held, so they are
             * rendered as seamless loops of the sample's own loop region
             * at its own level (LOOP_RAW - normalising them to a common
             * peak is what wrecked the balance the first time). */
            static const struct { uint8_t cls; const char *name; } ROUGH[] = {
                { 0x50, "offroad50" }, { 0x54, "offroad54" },
                { 0x56, "offroad56" }, { 0x58, "offroad58" },
                { 0x5A, "offroad5A" }, { 0x5C, "offroad5C" },
            };
            uint8_t sc = surf_now & 0xFE;
            bool rough = race_state == RACE_RUN && !k->airborne
                         && k->speed > 0x100;
            if (getenv("SMK_SFX_TRACE") && (fx_ticks % 60) == 0)
                printf("surf: $%02X (class $%02X) speed %d state %d air %d\n",
                       surf_now, sc, k->speed, (int)race_state, (int)k->airborne);
            for (int i = 0; i < (int)(sizeof ROUGH / sizeof ROUGH[0]); i++)
                smk_sfx_loop(ROUGH[i].name, rough && sc == ROUGH[i].cls);
        }
        /* braking hard (the user's $3C): Y held and the speed really going */
        {
            static int brake_cool;
            if (brake_cool > 0) brake_cool--;
            if ((player.pad & 0x4000) && k->speed > 0x200
                && k->speed < was_speed - 24 && brake_cool == 0) {
                smk_sfx_play(SMK_SFX_BRAKE);
                brake_cool = 45;
            }
        }
        was_speed = k->speed;
        if (player.drive == 0x10 && was_drive != 0x10)
            smk_sfx_play(SMK_SFX_BOOST);     /* mushroom, boost pad, AND
                                              * the turbo start (the user) */
        if (player.boo_t == 0 && was_boo > 0) smk_sfx_play(SMK_SFX_BOO);  /* back in view */
        if (player.shrink_t > 0 && !was_shrink) smk_sfx_play(SMK_SFX_SHRINK);
        if (player.shrink_t == 0 && was_shrink) smk_sfx_play(SMK_SFX_GROW);
        was_state = st; was_hazard2 = player.hazard;
        was_mole = player.mole_on; was_air = k->airborne;
        was_surf = surf_now; was_shrink = player.shrink_t > 0;
        was_drive = player.drive; was_boo = player.boo_t; was_bump = k->bump_cool;
    }

    /* THE ENGINE (NOTES 214/216).  $42 - the byte the driver gets every
     * frame - is written from $C2 inside the sound update, which the
     * physics overwrites later in the same frame, so $80:9543 cannot be
     * checked against anything a frame-end sample sees.  It is fitted to
     * the game's own trace instead (10,172 logged frames), and the trace
     * says three things:
     *
     *   - it sits at $01 for the WHOLE countdown, which is the idle: the
     *     engine is audible from the moment the race appears, not from
     *     the green light (the user);
     *   - holding the throttle at a standstill climbs it about 0.4 a
     *     frame - that is the rev you hear before the lights;
     *   - once moving it tracks speed * 0.07.
     *
     * LABELLED (S38): the shape is the game's, the constants are a fit. */
    {
        static float rev = 1.0f;
        bool thr = (player.pad & 0x8000) != 0 || engine_throttle;
        /* THE MAP FROM SPEED, measured (NOTES 219).  The engine note is
         * NOT proportional to speed: the game's own $42 sits in a narrow
         * band - about $14 at a standstill under throttle and $3D flat
         * out - because the parameter is a REV accumulator ($80:B121,
         * +$0120 a frame under $2000, +$0080 over, -$0200 coasting,
         * ceiling $3FFF) and $42 is that rev over about 222.  Medians
         * from 8,600 logged frames with the throttle held:
         *
         *   speed  150 300 450 650 750 850 950+
         *   $42     36  33  42  50  54  61  61
         *
         * which is 20 + speed * 0.048, and that is what the port uses.
         * A straight speed * 0.079 - 6 (the first fit) swept the whole
         * range instead and the note ran away from the kart. */
        float target = 20.0f + (float)k->speed * 0.048f;
        if (race_state == RACE_COUNTDOWN) {
            /* the port already keeps the game's own countdown rev
             * (smk_player_rev transcribes $80:95BB - NOTES 163) */
            target = (float)((player.rev >> 8) & 0x7F);
        } else if (!thr && k->speed < 0x40) {
            target = 1.0f;                    /* stopped and coasting: idle */
        }
        if (target < 1.0f) target = 1.0f;
        if (target > 63.0f) target = 63.0f;   /* the rev's own ceiling      */
        /* MEASURED rates, and they are not symmetric: the trace rises
         * about 1 a frame and FALLS up to 3 - after a crash from $3F it
         * is back at $20 within ten frames. */
        float d = target - rev;
        rev += d > 0.0f ? (d < 0.8f ? d : 0.8f) : (d > -3.0f ? d : -3.0f);
        int v = (int)(rev + 0.5f);
        bool racing = race_state == RACE_RUN || race_state == RACE_COUNTDOWN;
        smk_engine_set(racers[0].character % SMK_CHARACTERS,
                       racing ? (v < 1 ? 1 : v) : 0);
        /* AND THE OTHER KARTS (NOTES 229).  The user named $5C and $62
         * as "the engine of another player": the game gives a nearby
         * kart its own engine, so the port now does too - the three
         * closest, each pitched by its OWN speed through the same law,
         * panned by where it sits relative to the camera and fading out
         * with distance.  OURS: the range and the falloff. */
        {
            struct { float d2; int idx; } near[3] = {{1e18f,-1},{1e18f,-1},{1e18f,-1}};
            for (int q = 1; q < SMK_CHARACTERS; q++) {
                float dx = (float)(smk_kart_px(racers[q].k.x) - smk_kart_px(k->x));
                float dy = (float)(smk_kart_px(racers[q].k.y) - smk_kart_px(k->y));
                float d2 = dx * dx + dy * dy;
                for (int j = 0; j < 3; j++)
                    if (d2 < near[j].d2) {
                        for (int m = 2; m > j; m--) near[m] = near[m - 1];
                        near[j].d2 = d2; near[j].idx = q;
                        break;
                    }
            }
            /* MEASURED (NOTES 242), and it overturns NOTES 229: the game
             * gives another kart its own engine voice ONLY ON THE GRID.
             * Logging every voice in the engine sample AND pitch band
             * beside every kart's position, over four sessions: whenever
             * a second engine sounds the nearest kart is at 40 px - the
             * grid spacing, every time - and once the race is running a
             * kart THREE pixels away still gets none.  NOTES 229 built
             * this on the user naming $5C "another kart's engine"; $5C
             * turned out to be DK Jr's overtake voice (NOTES 235).
             * So: the countdown keeps them, the race does not. */
            bool grid = race_state == RACE_COUNTDOWN;
            for (int j = 0; j < 3; j++) {
                int q = near[j].idx;
                if (!grid || q < 0 || near[j].d2 > 220.0f * 220.0f) {
                    smk_engine_voice(j + 1, 0, 0, 0.0f, 0.0f);
                    continue;
                }
                float d = sqrtf(near[j].d2);
                int vq = (int)(20.0f + (float)racers[q].k.speed * 0.048f);
                if (vq > 63) vq = 63;
                if (vq < 1) vq = 1;
                /* where it is, left to right, in the camera's frame */
                float ang = (float)k->angle * (float)(2.0 * M_PI) / 65536.0f;
                float dx = (float)(smk_kart_px(racers[q].k.x) - smk_kart_px(k->x));
                float dy = (float)(smk_kart_px(racers[q].k.y) - smk_kart_px(k->y));
                float lat = -dx * sinf(ang) + dy * cosf(ang);
                float pan = lat / 120.0f;
                /* MEASURED (NOTES 237): in the ROM another kart's engine
                 * sits at a MEDIAN 0.89 of the player's OWN voice - 395
                 * samples over the 137 frames of a race where two engines
                 * sound at once, range 0.17..1.44, with the engine pitch
                 * range as the discriminator (the music plays two of the
                 * four engine samples as instruments an octave down, so
                 * the sample alone counts the wrong voices).  The port had
                 * 0.14 against the player's 0.40 - about a sixth - which
                 * is why the user could not hear a kart go past at all.
                 * OURS: the falloff, still linear over the same range. */
                float vol = smk_engine_base_volume()
                          * (1.3f - 1.0f * d / 220.0f);
                if (vol < 0.0f) vol = 0.0f;
                smk_engine_voice(j + 1, racers[q].character % SMK_CHARACTERS,
                                 vq, vol, pan);
                if (getenv("SMK_ENGINE_TRACE") && (fx_ticks % 30) == 0)
                    printf("  ai%d kart %d d %.0f vq %d vol %.3f pan %.2f\n",
                           j, q, d, vq, vol, pan);
            }
        }
        if (getenv("SMK_ENGINE_TRACE") && (fx_ticks % 15) == 0)
            printf("engine f%ld %-6s speed %4d thr %d -> $%02X\n",
                   hud_race_frames,
                   SMK_DRIVERS[racers[0].character % SMK_CHARACTERS].name,
                   k->speed, (int)thr, v);
    }

    /* the ground effect object ($80CF7B..$80D4A3): what the surface under
     * the kart and the slide/spin state ask for this frame */
    {
        smk_effects_step(&fx_state, fx_kind_now, fx_frame_idx);
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

/* SMK_SURF_FILL: every driveable tile -> this class (the oracle's
 * surface_fill, port side).  A helper because BOTH load paths must apply
 * it - the first draft filled only the boot path and load_race's fresh
 * track wiped it. */
static void apply_surf_fill(void)
{
    const char *sf = getenv("SMK_SURF_FILL");
    if (!sf) return;
    int fcls = (int)strtol(sf, NULL, 0);
    for (int i = 0; i < SMK_TILE_TOTAL; i++)
        if (trk.surface[i] >= 0x40) trk.surface[i] = (uint8_t)fcls;
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
    apply_surf_fill();
    if (mode == SMK_MODE_TT) strip_pickups(rom, &trk);
    smk_blocks_bind(&trk);
    smk_objgfx_load(rom, trk.theme, &obj_art);
    smk_shadow_load(rom, &shadow_art);
    smk_horizon_load(rom, trk.theme, &horizon);
    /* the minimap follows the course (round 2, bug 13: the shell never
     * rebuilt it, so every race showed the BOOT track's map) */
    obj_whole = (trk.theme == 3 || trk.theme == 5);
    build_track_map(&trk);

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
    if (getenv("SMK_TELEMETRY")) show_speedo = true;
    race_state = RACE_COUNTDOWN;
    race_count = 0;
    hud_race_frames = 0;
    lap_start_frames = 0;
    crossings = 0;
    race_over = false;
    finish_t = 0;
    memset(&item, 0, sizeof item);
    memset(projs, 0, sizeof projs);
    cur_track = track;
    /* the results screen names the track from the UI's own field, which
     * only the shell sets - so a --race or --timetrial run showed a blank */
    ui.track = track;
    smk_race_frame = 0;
    race_reported = false;
    lap_sign_t = -1;
    item_used_once = false;
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
/* MEASURED (tools/labs/spincam.py, NOTES 196): through the object tumble
 * (state $1A) the camera azimuth $94 turns WITH the spin - $A4 + $C0 +
 * $AA/2, while the pose goes the other way, $A4 - $AA/2 - so the whole
 * view does 360s at half the tumble rate (a turn every 16 frames at $2000,
 * decaying with $E4).  The banana's and the coinless bump's spins leave
 * $94 at $A4 + $C0: only the sprite turns.  The user remembered the
 * camera turning; this is which one. */
static int cam_spin;            /* $AA/2 while tumbling, else 0 */
static void camera_from_kart(smk_camera *cam, const smk_kart *k)
{
    cam->x = (float)k->x / (float)SMK_POS_ONE;
    cam->y = (float)k->y / (float)SMK_POS_ONE;
    /* the ROM's camera azimuth is the heading plus a constant $C0 lead
     * ($808632, measured on the live race: cam - $A4 == 192 every frame) */
    uint16_t az = (uint16_t)(k->angle + SMK_CAM_LEAD + cam_spin);
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
    /* whoever is STILL out there (stuck on an obstacle, off in the weeds)
     * gets a time anyway - "the simulation needs them to arrive, with
     * times" (bug 4).  Ordered by their progress; OURS, labelled. */
    {
        int left[SMK_CHARACTERS], nl = 0;
        for (int i = 1; i < SMK_CHARACTERS; i++)
            if (rs[i].finish_frame < 0) left[nl++] = i;
        for (int a = 0; a < nl; a++)
            for (int b = a + 1; b < nl; b++)
                if (rs[left[b]].lap > rs[left[a]].lap) { int q = left[a]; left[a] = left[b]; left[b] = q; }
        for (int a = 0; a < nl; a++)
            rs[left[a]].finish_frame = now + CAP + (long)(a + 1) * 300;
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
    if (getenv("SMK_RESULT_TRACE"))
        for (int p = 0; p < SMK_CHARACTERS; p++)
            printf("result %d: racer %d finish %ld\n", p, order[p], rs[order[p]].finish_frame);
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
    if (aw == SMK_OBJ_NEAR_W && obj_whole) {
        /* Choco Island's plant and Koopa Beach's fish (NOTES 195/198):
         * their sheets' first drawing is the WHOLE sprite - a fish with
         * one face - so the near band samples it 2:1 instead of folding
         * a half and its mirror, which made a two-headed fish */
        ax >>= 1; ay >>= 1;
        int tl = (ay / 8) * SMK_OBJ_STRIDE + (ax / 8);
        if (tl < 0 || tl >= SMK_OBJ_TILES) return 0;
        return obj_art.px[tl][(ay % 8) * 8 + (ax % 8)];
    }
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
        /* Rainbow Road's Thwomp is the ripped BLUE sprite through RR's
         * own palettes - "the flashy color is totally wrong. Probably
         * even the sprite used" (round 2, bug 8).  Palette 1 fits it
         * with zero error and palette 2 is its flash coloring (three
         * entries differ); the alternation period is OURS. */
        const uint8_t *rrart = trk->theme == 7 ? SMK_RRTHWOMP : NULL;
        if (rrart) { aw = SMK_RRTHWOMP_W; ah = SMK_RRTHWOMP_H; }
        /* Koopa Beach's entity is ONE white cheep-cheep, not the theme
         * sheet's four-fish block (round 2, bug 10) - the ripped ladder
         * on KB's own palette, two flip frames (the flip period is OURS,
         * half a hop). */
        const smk_itemart_tier *fart = trk->theme == 5
            ? &SMK_FISHART[(fx_ticks / 18 + (unsigned)i) & 1] : NULL;
        if (fart) { aw = fart->w; ah = fart->h; }
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
        if (trk->theme == 2) {
            /* DONUT PLAINS' entity is the MOLE (bug 12, NOTES 210): the
             * recording's blocks are track 2's own list entries, and the
             * pop is the block's +$20 step, 0..6 on a ~130-frame cycle.
             * Drawn rising out of its hole: the top step/6 of the ripped
             * art (zero-error on DP's palette 7), bottom at the ground. */
            int mstep = smk_mole_step(fx_ticks, i);
            if (mstep <= 0) return;                    /* underground */
            const smk_itemart_tier *mt = &SMK_MOLART[0];
            /* the mole RESTS +6,+6 from its hole - every recorded block
             * sat exactly there - so it projects from that point, not
             * the list entry; ~12 world px wide (OURS, sized to the
             * hole art) */
            float mpx, mpy, msc;
            if (!smk_project(cam, (float)course->ent[i].x + 6.0f,
                             (float)course->ent[i].y + 6.0f, rw, rh,
                             &mpx, &mpy, &msc)) return;
            float ms = 10.0f * msc / (float)mt->w;
            int mdw = (int)((float)mt->w * ms + 0.5f);
            int mdh = (int)((float)mt->h * ms + 0.5f);
            int vis = mdh * mstep / 6;
            if (mdw < 1 || vis < 1) return;
            int mx0 = (int)mpx - mdw / 2, my0 = (int)mpy - vis;
            for (int dy = 0; dy < vis; dy++) {
                int yy = my0 + dy;
                if (yy < 0 || yy >= rh) continue;
                int ty = dy * mt->h / mdh;
                for (int dx2 = 0; dx2 < mdw; dx2++) {
                    int xx = mx0 + dx2;
                    if (xx < 0 || xx >= rw) continue;
                    int tx = dx2 * mt->w / mdw;
                    uint8_t v = mt->px[ty * mt->w + tx];
                    if (!v) continue;
                    fb[yy * rw + xx] = trk->palette[(0x80 + SMK_MOLART_PAL * 16 + v) & 0xFF];
                }
            }
            return;
        }
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
        if (fart) {
            /* the cheep-cheep HOPS - measured off the recording (NOTES
             * 209): the jump velocity saws 316 -> -316 at 18 a frame in
             * 8.8 units, a ~35-frame leap peaking ~11 world px; each
             * fish on its own phase */
            int t35 = (int)((fx_ticks + (unsigned)i * 13u) % 35u);
            int hopz = (316 * t35 - 9 * t35 * t35) / 256;
            if (hopz < 0) hopz = 0;
            lift += (int)((float)hopz * (SMK_PROJ_LES / depth)
                          * ((float)rh / 112.0f));
        }
        if (getenv("SMK_LIFT_TRACE") && smk_mover_z(course, i) > 0)
            printf("lift f%u ent %d z %d world %.1f depth %.1f sc %.2f lift_px %d rh %d\n",
                   fx_ticks, i, smk_mover_z(course, i), smk_mover_world(course, i), depth, sc, lift, rh);
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
                    if (!(fart ? fart->px[ay * aw + ax]
                        : rrart ? rrart[ay * aw + ax]
                                : obj_texel(obase, aw, ax, ay))) continue;
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
                uint8_t v = fart ? fart->px[ty * aw + tx]
                          : rrart ? rrart[ty * aw + tx]
                                  : obj_texel(obase, aw, tx, ty);
                if (!v) continue;
                int pbase = fart ? 0x80 + SMK_FISHART_PAL * 16
                          : rrart
                          ? (((fx_ticks >> 3) & 1u) ? SMK_RRTHWOMP_PAL2
                                                    : SMK_RRTHWOMP_PAL1)
                          : smk_obj_pal(trk->theme);
                fb[yy * rw + xx] = trk->palette[((unsigned)pbase + v) & 0xFF];
            }
        }
}

/* One AI kart, split out for the same reason as draw_entity. */
/* The SQUASHED racer (bug 13): the user's ripped flattened art, drawn like
 * the road items - nearest-neighbour at a continuous scale, through the
 * driver palette its block quantised to (tools/labs/flatsheet.py), anchored
 * at the wheels like every kart sprite.  The big art always, by the pipe
 * law; frame 1 is the straight pose. */
static void draw_flat(uint32_t *fb, int rw, int rh, const uint32_t *palette,
                      int ch, int frame, int cx, int cy, float s)
{
    if (ch < 0 || ch >= SMK_CHARACTERS || s <= 0.0f) return;
    const smk_flat_tier *t = &SMK_FLATART[ch][frame];
    int dw = (int)((float)t->w * s + 0.5f), dh = (int)((float)t->h * s + 0.5f);
    if (dw < 1 || dh < 1) return;
    int x0 = cx - dw / 2, y0 = cy - dh;          /* anchored at the wheels */
    for (int yy = 0; yy < dh; yy++) {
        int sy = y0 + yy;
        if (sy < 0 || sy >= rh) continue;
        int ty = yy * t->h / dh;
        for (int xx = 0; xx < dw; xx++) {
            int sx = x0 + xx;
            if (sx < 0 || sx >= rw) continue;
            int tx = xx * t->w / dw;
            uint8_t v = t->px[ty * t->w + tx];
            if (!v) continue;
            fb[(size_t)sy * rw + sx] = palette[(t->pal + v) & 0xFF];
        }
    }
}

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
        int rel = (int16_t)(uint16_t)(racers[k].k.angle + racers[k].spin_pose - cam_heading);
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
        /* the surface's shake, as the player's (NOTES 197; LABELLED: the
         * sweep measured P1's sprite, the AI's is assumed the same) */
        if (!racers[k].k.airborne && racers[k].k.speed != 0)
            py -= (float)smk_shake_of(smk_track_surface(trk, smk_kart_px(racers[k].k.x),
                                                        smk_kart_px(racers[k].k.y)))[(fx_ticks + (unsigned)k * 3) & 7];
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
        if (racers[k].shrink_t > 0) kwant *= 0.5f;      /* lightning: OURS, half size */
        if (kwant > (float)KTIER[0].h) kwant = (float)KTIER[0].h;
        int kt = 0;
        for (int t = 1; t < 4; t++)
            if (fabsf((float)KTIER[t].h - kwant)
                < fabsf((float)KTIER[kt].h - kwant)) kt = t;
        int kscale = rw / 256;
        if (kscale < 1) kscale = 1;
        if (getenv("SMK_TIER_TRACE") && (fx_ticks % 10) == 0)
            printf("tier f%u kart %d kd %.0f kwant %.1f tier %d (h %d) mirror %d\n",
                   fx_ticks, k, kd, kwant, kt, KTIER[kt].h, (int)mirror);
        /* THE SIZE IS CONTINUOUS (the user: "normal size when far, midget
         * at mid distance, then magically grow back").  The tiers pick the
         * ART, as the game does; the pixel size follows the karts' own
         * 1/distance law - full at the player's depth, never larger - so
         * a kart is smaller than the one in front of it at every window
         * shape.  Drawing every tier at rw/256 and the mini at half was
         * right only for the 256x224 view the tiers were made for: in a
         * 16:9 window the road compresses vertically and a kart 100 px
         * away sits at the horizon at 75 px tall (LABELLED: ours, S10). */
        /* THE PIPE LAW (the user: "there is a specific point where they grow
         * faster than how they get closer... This also happened previously
         * with the pipes (now the way they scale is perfect)").  Size =
         * smk_project's own scale, measured from the EYE; the earlier
         * 66/distance was measured from the KART, 61 px nearer, which is
         * NOTES 154b's swell all over again.  At the player's depth the
         * scale is the player's own (kscale), so ks = sc * TRAIL / LES.
         * And the highest-resolution drawing always: the tier ladder is
         * the SNES's substitute for scaling, and switching to a coarser
         * drawing while the size is still large is the step the eye sees. */
        float ks = sc * SMK_CAM_TRAIL / SMK_PROJ_LES;
        if (ks > (float)kscale) ks = (float)kscale;
        if (racers[k].shrink_t > 0) ks *= 0.5f;
        int fdraw = mirror ? 0 : f;
        bool hf2 = hf;
        (void)kt;
        if (racers[k].squash_t > 0) {              /* bug 13: flattened */
            draw_flat(fb, rw, rh, trk->palette, ch, 1,
                      (int)lroundf(px), (int)lroundf(py), ks);
            return;
        }
        {
            /* THE WINNER (NOTES 199, measured from a real finish): once the
             * camera has swung to the front the game shows frame 46 as its
             * left half and mirror, and from SMK_WIN_ARMS_AT frames after
             * the crossing it alternates that with the arms-up build
             * (frame 48).  Before the swing settles the rotation frames
             * are the ordinary ones, which the path below already picks. */
            if (celebrating && k == smk_ai_player_block) {
                int rel2 = (int16_t)(uint16_t)(racers[k].k.angle - cam_heading + 0x8000);
                if (rel2 < 0) rel2 = -rel2;
                if (rel2 < 0x0C00) {
                    int fr = SMK_SPR_FRONT;
                    if (finish_t >= SMK_WIN_ARMS_AT && (((finish_t - SMK_WIN_ARMS_AT) / SMK_WIN_TOGGLE) & 1) == 0)
                        fr = SMK_SPR_WIN_FRAME;
                    smk_draw_sprite_scaled(&other[k], fr, trk->palette, d2->pal,
                                           (int)lroundf(px), (int)lroundf(py), ks, false, true,
                                           fb, rw, rh, rw);
                    return;
                }
            }
            /* Mario / Luigi under their own star: the player's measured
             * palette run (NOTES 189) */
            static const int AI_STAR_PAL[8] = { 5, 4, 7, 6, 1, 0, 3, 2 };
            int apal = (racers && racers[k].star_t > 0)
                     ? 0x80 + AI_STAR_PAL[fx_ticks & 7] * 0x10 : d2->pal;
            /* ROUNDED, not truncated: near karts flickered a pixel in X as
             * the projected centre and the scaled size crossed integer
             * boundaries out of step (round 2, bug 16) */
            smk_draw_sprite_scaled(&other[k], fdraw, trk->palette, apal,
                                   (int)lroundf(px), (int)lroundf(py), ks, hf2, mirror,
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
                if (course->dead[i]) continue;
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
    /* the spilled coins: the CAPTURED screen path (SMK_COIN_PATH), relative
     * to the top centre of the player's kart sprite, at the kart's scale,
     * with the game's own spin frame per step.  Not projected: the game
     * shows the coin appearing far up the screen four frames after the
     * loss and coming down to the kart, which no throw from the kart
     * reproduces (NOTES 186). */
    if (coin_art.ok && !celebrating) {
        int scale = rw / 256; if (scale < 1) scale = 1;
        int prow = (int)(SMK_PLAYER_LINE * (float)rh / 112.0f);
        int kx = rw / 2, ky = prow - player_height_px * scale - 32 * scale;
        for (int i = 0; i < SMK_COINFX_MAX; i++) {
            const smk_coin *kc = &coins_fx[i];
            if (!kc->live) continue;
            int step = kc->t - (kc->kind ? SMK_COINUP_DELAY : SMK_COIN_DELAY);
            int len = kc->kind ? SMK_COINUP_PATH_LEN : SMK_COIN_PATH_LEN;
            if (step < 0 || step >= len) continue;
            const smk_coin_step *st = &(kc->kind ? SMK_COINUP_PATH : SMK_COIN_PATH)[step];
            int cx = kx + (kc->kind ? st->dx : st->dx * kc->side) * scale,   /* the 2-coin item: same X (the user) */
                cy = ky + st->dy * scale;
            if (getenv("SMK_COIN_TRACE"))
                printf("coin t%d screen (%d,%d)\n", kc->t, cx * 256 / rw, cy * 224 / rh);
            const uint8_t *art = coin_art.px[st->frame];
            for (int yy = 0; yy < SMK_COIN_PX * scale; yy++) {
                int sy = cy + yy;
                if (sy < 0 || sy >= rh) continue;
                const uint8_t *row = art + (yy / scale) * SMK_COIN_PX;
                for (int xx = 0; xx < SMK_COIN_PX * scale; xx++) {
                    int sx = cx - SMK_COIN_PX * scale / 2 + xx;
                    if (sx < 0 || sx >= rw) continue;
                    uint8_t v = row[xx / scale];
                    if (!v) continue;
                    fb[(size_t)sy * rw + sx] = trk->palette[(0xE0 + v) & 0xFF];
                }
            }
        }
    }
    /* the bananas and shells: projected like the coins, with the game's
     * own sprites (smk_projart), sized by the entities' 1/distance law */
    for (int i = 0; i < SMK_PROJ_MAX; i++) {
        const smk_proj *pr = &projs[i];
        if (pr->kind == SMK_PROJ_NONE) continue;
        float px, py, sc;
        if (!smk_project(cam, (float)smk_kart_px(pr->x + pr->wx), (float)smk_kart_px(pr->y + pr->wy),
                         rw, rh, &px, &py, &sc)) continue;
        int scale = rw / 256; if (scale < 1) scale = 1;
        float kdx = (float)smk_kart_px(pr->x) - cam->x, kdy = (float)smk_kart_px(pr->y) - cam->y;
        float kd = sqrtf(kdx * kdx + kdy * kdy); if (kd < SMK_OBJ_NEAR) kd = SMK_OBJ_NEAR;
        int cs = (int)((float)scale * SMK_KART_SCALE_K / kd * 0.5f + 0.5f);
        if (cs < 1) cs = 1;
        if (cs > scale) cs = scale;
        int lift = (int)(pr->z / 25029) * scale;
        /* THE ROAD ITEMS' ART (NOTES 192): the size ladders the game's own
         * scaler produces, from the ripped sheet, drawn through the OBJ
         * palettes they quantize to without error.  The tier is picked by
         * the width the karts' 1/distance law asks for, then drawn at the
         * screen scale; the shell spins through its three frames every
         * four frames (OURS, the rate). */
        {
            const smk_itemart_tier *lad; int nl, frames = 1, ipal;
            switch (pr->kind) {
            case SMK_PROJ_GREEN: case SMK_PROJ_RED:
                lad = pr->kind == SMK_PROJ_RED ? SMK_ITEMART_SHELL_RED : SMK_ITEMART_SHELL;
                nl = SMK_ITEMART_SHELL_N; frames = SMK_ITEMART_SHELL_FRAMES;
                ipal = pr->kind == SMK_PROJ_RED ? SMK_ITEMART_SHELL_RED_PAL : SMK_ITEMART_SHELL_PAL; break;
            case SMK_PROJ_MUSHROOM: lad = SMK_ITEMART_MUSHROOM; nl = SMK_ITEMART_MUSHROOM_N; ipal = SMK_ITEMART_MUSHROOM_PAL; break;
            case SMK_PROJ_EGG:      lad = SMK_ITEMART_EGG;      nl = SMK_ITEMART_EGG_N;      ipal = SMK_ITEMART_EGG_PAL; break;
            case SMK_PROJ_FIREBALL: lad = SMK_ITEMART_FIREBALL; nl = SMK_ITEMART_FIREBALL_N; ipal = SMK_ITEMART_FIREBALL_PAL; break;
            default:                lad = SMK_ITEMART_BANANA;   nl = SMK_ITEMART_BANANA_N;   ipal = SMK_ITEMART_BANANA_PAL; break;
            }
            /* the LARGEST tier, scaled by the pipe law (the user: "use our
             * scaling formula from the higher res shell, switching to the
             * low res sprites makes them look awful too soon") */
            float ks2 = sc * SMK_CAM_TRAIL / SMK_PROJ_LES;
            if (ks2 > (float)scale) ks2 = (float)scale;
            float want = 16.0f * ks2 / (float)scale;             /* native px, for the record */
            int ntier = nl / frames; (void)ntier;
            int tier = 0;
            /* the shell's three frames are its spin: only while it travels
             * (a dropped one sits still) - the user */
            bool moving = pr->speed != 0 || pr->vx || pr->vy;
            const smk_itemart_tier *t = &lad[tier * frames + (frames > 1 && moving ? (int)((fx_ticks >> 2) % (unsigned)frames) : 0)];
            float s = ks2 * (16.0f / (float)t->w); (void)want;
            if (s < 0.1f) s = 0.1f;
            int dw = (int)((float)t->w * s + 0.5f), dh = (int)((float)t->h * s + 0.5f);
            if (dw < 1 || dh < 1) continue;
            int x0 = (int)px - dw / 2, y0 = (int)py - dh - lift;
            for (int yy = 0; yy < dh; yy++) {
                int sy = y0 + yy;
                if (sy < 0 || sy >= rh) continue;
                int ty = yy * t->h / dh;
                for (int xx = 0; xx < dw; xx++) {
                    int sx = x0 + xx;
                    if (sx < 0 || sx >= rw) continue;
                    int tx = xx * t->w / dw;
                    uint8_t v = t->px[ty * t->w + tx];
                    if (!v) continue;
                    fb[(size_t)sy * rw + sx] = trk->palette[(0x80 + ipal * 16 + v) & 0xFF];
                }
            }
            continue;
        }
    }
    if (show_kart && !celebrating && karts->frames
        && !(player.boo_t > 0 && (fx_ticks & 1))) {   /* Boo: OURS, a flicker */
        int scale = rw / 256;                 /* the SNES 32px proportion */
        if (scale < 1) scale = 1;
        /* small after lightning ($84): OURS, half the art scale; the star
         * ($4E bit 15): OURS, the palette cycling through the drivers' */
        if (player.shrink_t > 0 && scale > 1) scale /= 2;
        /* MEASURED (tools/labs/starpal2.py, NOTES 189): the starred kart's
         * OAM palette runs 5 4 7 6 1 0 3 2, ONE frame each, round and round
         * - every sprite palette, at 60 Hz.  Holding each for four frames
         * was what read as "colours, then nothing" (the user). */
        static const int STAR_PAL[8] = { 5, 4, 7, 6, 1, 0, 3, 2 };
        int ppal = player.star_t > 0 ? 0x80 + STAR_PAL[fx_ticks & 7] * 0x10 : drv->pal;
        /* the hop lifts the sprite; the shadow stays on the ground */
        int lift = player_height_px * scale;
        /* the surface's shake (NOTES 197), while on the ground and moving */
        /* in ABSOLUTE pixels, not window-scaled: "in real game it is never
         * that big. Always super subtle 1px maybe" (the user) */
        if (!kart.airborne && kart.speed != 0 && !celebrating)
            lift -= smk_shake_of(smk_track_surface(trk, smk_kart_px(kart.x), smk_kart_px(kart.y)))[fx_ticks & 7];
        /* The fall is a RENDERING effect here and nowhere else (NOTES
         * 142).  The game holds `$1F` at 1 for the whole countdown and
         * animates the sprite; we have no fall animation, so the kart is
         * simply drawn lower each frame.  It is NOT clipped against the
         * plane - that only ever applied because our physics used to put
         * the kart underneath it. */
        if (player.hazard == 6) lift -= player.resc_t * 2 * scale;
        /* IN THE WATER (hazard 8): the kart rides low - the sink has to be
         * SEEN, not only felt (round 2, bug 4: the crawl worked and read
         * as "not working" because the sprite sat at full height).
         * OURS: 9 px down; the game's own submerged drawing is unread. */
        if (player.hazard == 8) lift -= 9 * scale;
        /* THE RESCUE (the user: "it should come down with you with his
         * fishing rod... Normally you appear from the top, attached to
         * lakitu's fishing rod cable").  Lakitu's descent is the captured
         * path (NOTES 168a, his block's row y per frame of the $0E phase);
         * the kart's own z ran on its own clock and landed first.  The kart
         * now HANGS from him: at the end of the path his row is 38 and his
         * 32-px block's bottom, 70, is exactly the kart sprite's top on the
         * ground (the player line 102 minus 32) - so the kart's lift is
         * 38 - his row, all the way down, and the two cannot drift. */
        /* MEASURED (tools/labs/lakitu_rescue.py, the kart's own sprite,
         * NOTES 195): through $0E the kart's sprite row is 128 + 2t,
         * wrapping - it is off the bottom until frame 64, enters from the
         * TOP at row 0 and lands on row 70 at frame 98, 2 px a frame.  The
         * physics z is not the picture ($1E reads 0 / -32768 the whole
         * way); the picture is this. */
        if (player.hazard == 0x0E) {
            int row = (128 + 2 * rescue_t) & 0xFF;          /* the sprite's top */
            if (rescue_t < 64) lift = 100000;                /* off-screen: not drawn */
            else lift = (70 - row) * scale;
        }
        rescue_draw_lift = lift;
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
        if (player.squash_t > 0)                   /* bug 13: flattened */
            draw_flat(fb, rw, rh, trk->palette, (int)(drv - SMK_DRIVERS), 1,
                      rw / 2, prow - lift, (float)scale);
        else if (frame == SMK_POSE_LEAN || frame == -SMK_POSE_LEAN)
            /* the SAME block as the straight pose, drawn UNFOLDED so its
             * own right half shows: that is the lean (NOTES 182) */
            smk_draw_sprite(karts, SMK_SPR_LEAN, trk->palette, ppal,
                            rw / 2, prow - lift, scale,
                            frame < 0, fb, rw, rh, rw);
        else if (frame == 1000)               /* the mirrored straight pose */
            smk_draw_sprite_mirror(karts, 0, trk->palette, ppal,
                                   rw / 2, prow - lift, scale,
                                   fb, rw, rh, rw);
        else {
            bool hf = frame < 0;
            /* ppal, not drv->pal: the star's colour run must survive the
             * TURNING sprites too (round 2, bug 17: "when turning, the
             * colors go away") */
            smk_draw_sprite(karts, hf ? -frame : frame, trk->palette,
                            ppal, rw / 2, prow - lift, scale,
                            hf, fb, rw, rh, rw);
        }
        if (player.mole_on) {
            /* the mole RIDES THE DRIVER (bug 12): the ripped art over the
             * kart sprite's head, wobbling with the frame counter */
            const smk_itemart_tier *mt = &SMK_MOLART[0];
            int mdw = mt->w * scale * 2 / 3, mdh = mt->h * scale * 2 / 3;
            int mcx = rw / 2 + (int)(((fx_ticks >> 3) & 1u) ? 1 : -1) * scale;
            int mcy = prow - lift - 22 * scale;
            for (int dy = 0; dy < mdh; dy++) {
                int yy = mcy - mdh + dy;
                if (yy < 0 || yy >= rh) continue;
                int ty = dy * mt->h / mdh;
                for (int dx2 = 0; dx2 < mdw; dx2++) {
                    int xx = mcx - mdw / 2 + dx2;
                    if (xx < 0 || xx >= rw) continue;
                    int tx = dx2 * mt->w / mdw;
                    uint8_t v = mt->px[ty * mt->w + tx];
                    if (!v) continue;
                    fb[(size_t)yy * rw + xx] = trk->palette[(0x80 + SMK_MOLART_PAL * 16 + v) & 0xFF];
                }
            }
        }
        /* the puffs sit relative to the kart sprite's top-left + (0,16)
         * and draw over the wheels (lower OAM slots than the kart) */
        /* the base: the sweep's OAM puts cls-$54 puffs at kart_left -5..+21
         * (avg +7) while kind $00's template dx average -11, so the game's
         * base is kart_left + 18 - the old -16 sat every puff ~34 px left
         * (bug 5, "coming shifted to the left") */
        smk_effects_draw(&fx, &fx_state, fx_mirror, fx_ticks,
                         rw / 2 + 2 * scale, prow - lift - 16 * scale, scale,
                         trk->palette, fb, rw, rh);
        smk_draw_set_clip_mask(NULL, 0);
    }
    if (!celebrating) {
        draw_hud(fb, rw, rh, trk->palette, player.coins, hud_rank, kart.speed, hud_lap);
        {   /* The needle lives in the BOTTOM-LEFT corner, where the raw
             * metrics used to be - the user: "move the dial to the bottom
             * left corner instead of current metrics that are not needed
             * anymore".  The lap goes above it: the game shows none of its
             * own (Lakitu announces it), so this is ours too. */
            int sc2 = rw >= 640 ? 3 : 2, adv2 = 8 * sc2;
            int gr = adv2 * 2;
            int gx = 8 + gr + adv2 / 2, gy = rh - 8 - adv2 / 2;
            uint8_t su = smk_track_surface(trk, smk_kart_px(kart.x),
                                           smk_kart_px(kart.y));
            draw_gauge(fb, rw, rh, gx, gy, gr, kart.speed, player.target,
                       smk_surface_cap_frac(su));

        }
        draw_track_map(fb, rw, rh, &kart, racers, SMK_CHARACTERS);
        draw_clock(fb, rw, rh, trk->palette, hud_race_frames);
        /* THE ITEM SLOT: the game's own BG3 icon, 2x2 tiles at the top left
         * where the HUD tilemap puts it ($0C26 = row 0, column 19 of a
         * 32-wide map -> x 152, y 0 on a 256-wide screen), drawn at the
         * HUD's scale.  The empty box shows when nothing is held; the held
         * icon blinks every eight frames, and stops blinking when READY. */
        if (item_icons.ok) {
            int sc = rw >= 640 ? 3 : 2;
            /* THE BOX, from the game's own HUD map (tools/labs/hudslot.py,
             * NOTES 189): columns 18-21, rows 0-3 of the $0C00 tilemap -
             *     $D2  a    b    $D2(h)      rows 0-1: the icon, or the
             *     $D3  c    d    $D3(h)      pristine $CC $CD / $EA $EB, or
             *     $D2  $EC  $ED  $D2(h)      the used $E9 $E9 / $E8 $E8
             *     $D3  $EE  $EF  $D3(h)      rows 2-3: always these
             * all attr $3C (palette 7), the sides H-flipped on the right.
             * There is no $D0/$D1 row: that was a misread of the roulette
             * dump and drew a black bar over the box (the user). */
            {
                /* TWO rows in a one-player race.  The 2P attract race's
                 * map has two more under it with a "2" in them; the user:
                 * "There wasn't a second box and the number you see is
                 * just a placeholder for Player 1 or two until you get
                 * your first item.  The position in game is a different
                 * rendering mechanism." */
                static const struct { int col, row, tile; bool hf; } F[4] = {
                    { 0, 0, 0xD2, false }, { 3, 0, 0xD2, true },
                    { 0, 1, 0xD3, false }, { 3, 1, 0xD3, true },
                };
                int fpal = 7;
                for (int q = 0; q < 4; q++) {
                    int tn = F[q].tile - SMK_ICON_BASE;
                    if (tn < 0 || tn >= SMK_ICON_TILES) continue;
                    const uint8_t *px = item_icons.px[tn];
                    int bx = (144 + F[q].col * 8) * sc, by = F[q].row * 8 * sc;
                    for (int yy = 0; yy < 8 * sc; yy++)
                        for (int xx = 0; xx < 8 * sc; xx++) {
                            int col = xx / sc; if (F[q].hf) col = 7 - col;
                            uint8_t v = px[(yy / sc) * 8 + col];
                            if (!v) continue;
                            int sx = bx + xx, sy = by + yy;
                            if (sx < 0 || sx >= rw || sy < 0 || sy >= rh) continue;
                            fb[(size_t)sy * rw + sx] = trk->palette[(SMK_HUD_BG_PAL + fpal * 4 + v) & 0xFF];
                        }
                }
            }
            int which = -1;                                  /* nothing held: nothing drawn */
            if (smk_item_present(&item) && !(item.word & 0x1000)) {
                int id = smk_item_shown(&item);
                which = (id >= 0 && id < SMK_ITEMS) ? id : SMK_ITEMS + 1;
                if (smk_item_blink(&item)) which = SMK_ITEMS;   /* the blank */
            }
            int t0 = which >= 0 ? item_icons.tile[which] - SMK_ICON_BASE : 0;
            int pal = which >= 0 ? item_icons.pal[which] : 7;
            /* nothing held: the pristine slot until the first use of the
             * race, the used one after ($E9 $E9 / $E8 $E8 is also what
             * the roulette's blank frame shows) */
            static const int USED[4] = { 0x69, 0x69, 0x68, 0x68 };
            static const int FRESH[4] = { 0x4C, 0x4D, 0x6A, 0x6B };
            int ox = 152 * sc, oy = 0;                         /* the icon is map rows 0-1 */
            for (int q = 0; q < 4; q++) {
                int tn = (which == SMK_ITEMS + 1) ? USED[q]
                       : (which < 0) ? (item_used_once ? USED[q] : FRESH[q])
                       : t0 + q;
                if (tn < 0 || tn >= SMK_ICON_TILES) continue;
                const uint8_t *px = item_icons.px[tn];
                int bx = ox + (q & 1) * 8 * sc, by = oy + (q >> 1) * 8 * sc;
                for (int yy = 0; yy < 8 * sc; yy++)
                    for (int xx = 0; xx < 8 * sc; xx++) {
                        uint8_t v = px[(yy / sc) * 8 + xx / sc];
                        if (!v) continue;
                        int sx = bx + xx, sy = by + yy;
                        if (sx < 0 || sx >= rw || sy < 0 || sy >= rh) continue;
                        fb[(size_t)sy * rw + sx] = trk->palette[(SMK_HUD_BG_PAL + pal * 4 + v) & 0xFF];
                    }
            }
        }
    }
    /* live input state, so a stuck control is visible rather than
     * looking like a physics bug.  Diagnostic, so it goes with the rest
     * of them behind H - the dial has this corner now. */
    if (hud_input && show_speedo) {
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
    /* he waves you home; the rest of the furniture stands down */
    if (celebrating)
        draw_finish_flag(fb, rw, rh, trk->palette, finish_t);
    if (!celebrating) {
        if (hud_countdown >= 0)           /* Lakitu, and his light */
            draw_start_light(fb, rw, rh, trk->palette, hud_countdown);
        if (lap_sign_t >= 0)              /* and again with the lap sign */
            draw_lap_sign(fb, rw, rh, trk->palette, lap_sign_t,
                          hud_lap, SMK_RACE_LAPS);
        if (player.hazard == 0x0E)        /* and once more, on a rescue */
            draw_rescue_lakitu(fb, rw, rh, trk->palette, rescue_t, 0);
        else if (lakitu_exit_t > 0)       /* ...and he leaves, fee in hand */
            draw_rescue_lakitu(fb, rw, rh, trk->palette, 0,
                               (SMK_LAKITU_EXIT - lakitu_exit_t) * 3);
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
           "  --sfx           play every captured sound effect and ask what it is\n"
           "                  (c right, w wrong, ENTER skip, r again, q stop, or\n"
           "                  type the real name); answers -> rom/sfx/names.txt\n"
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
    int sfx_audition = 0;
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
        if (!strcmp(a, "--sfx")) { sfx_audition = 1; continue; }
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
    the_rom = &rom;
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
    if (getenv("SMK_ENT_DUMP")) {      /* the decoded entity list (bug 14) */
        printf("ents track %d: n %d live %d\n", track, crs.nent, crs.nlive);
        for (int i = 0; i < crs.nent; i++)
            printf("ent %2d: kind %02X x %4d y %4d\n",
                   i, crs.ent[i].kind, crs.ent[i].x, crs.ent[i].y);
        for (int i = 0; i < crs.nlive; i++)
            printf("live %d: ent %d\n", i, crs.live[i]);
        printf("segs %d:", crs.nseg);
        for (int i = 0; i < crs.nseg; i++) printf(" %d", crs.seg_thresh[i]);
        printf("\n");
    }
    smk_hud_load(&rom, &hud_art);
    if (!smk_coin_load(&rom, &coin_art))
        fprintf(stderr, "warning: coin sprite not loaded\n");
    if (smk_items_load(&rom, &itemtab)) smk_item_tables = &itemtab;
    else fprintf(stderr, "warning: item tables not loaded\n");
    if (!smk_projart_load(&rom, &proj_art))
        fprintf(stderr, "warning: banana/shell sprites not loaded\n");
    if (!smk_itemicons_load(&rom, &item_icons))
        fprintf(stderr, "warning: item icons not loaded\n");
    if (!smk_flag_load(&rom, &flag_art))
        fprintf(stderr, "warning: chequered flag not loaded\n");

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
    if (getenv("SMK_SURF_MAP")) {   /* every cell's CLASS byte as a P5 PGM,
                                     * 128x128 - the water diagnosis (round 2).
                                     * AFTER the track load: an earlier draft
                                     * ran before trk existed and wrote zeros */
        FILE *sfp = fopen(getenv("SMK_SURF_MAP"), "wb");
        if (sfp) {
            fprintf(sfp, "P5\n128 128\n255\n");
            for (int sy = 0; sy < 128; sy++)
                for (int sx = 0; sx < 128; sx++)
                    fputc(smk_track_surface(&trk, sx * 8 + 4, sy * 8 + 4), sfp);
            fclose(sfp);
        }
    }
    apply_surf_fill();
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
    smk_objgfx_load(&rom, trk.theme, &obj_art); obj_whole = (trk.theme == 3 || trk.theme == 5); build_track_map(&trk);   /* the theme's objects */
    /* SMK_OBJ_SHEET=path: the theme's object tiles as a sheet (16 a row),
     * palette 7 - to look at a theme's art without driving to one */
    if (getenv("SMK_OBJ_SHEET") && obj_art.ok) {
        FILE *pf = fopen(getenv("SMK_OBJ_SHEET"), "wb");
        if (pf) {
            int W = 16 * 8, H = ((SMK_OBJ_TILES + 15) / 16) * 8;
            fprintf(pf, "P6\n%d %d\n255\n", W, H);
            for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
                int t = (y / 8) * 16 + x / 8; uint8_t v = t < SMK_OBJ_TILES ? obj_art.px[t][(y % 8) * 8 + x % 8] : 0;
                uint32_t c = v ? trk.palette[(0x80 + 7 * 16 + v) & 0xFF] : 0xFF181828u;
                fputc((c >> 16) & 255, pf); fputc((c >> 8) & 255, pf); fputc(c & 255, pf);
            }
            fclose(pf);
        }
    }
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
                if (show_speedo)
                    draw_speedo(px, sw, sh, &shotk,
                                smk_track_surface(&trk, (int)shot_x, (int)shot_y),
                                672);
                /* the dashboard too, so a still can be checked against the
                 * game's own (SMK_SHOT_HUD=coins,rank,speed) */
                int scoin = 12, srank = 2, sspd = 583;
                const char *hs = getenv("SMK_SHOT_HUD");
                if (hs) sscanf(hs, "%d,%d,%d", &scoin, &srank, &sspd);
                draw_hud(px, sw, sh, trk.palette, scoin, srank, sspd, 1);
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
    if (!smk_audio_init()) printf("audio: unavailable (silent)\n");
    smk_audio_set_dir(rom_path);
    if (sfx_audition) {
        /* `--sfx`: play every captured effect, named where the ROM's own
         * call site says what it is (NOTES 211).  For naming by ear. */
        printf("the game's sound effects (id, length, what it is - and how we know):\n");
        int nplayed = smk_sfx_audition();
        if (!nplayed) printf("  none found - capture them first (docs/SOUND.md)\n");
        smk_rom_free(&rom);
        return 0;
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
        /* the music follows the state; the mapping is the user's
         * music/map.txt (NOTES 202) */
        {
            char mkey[16];
            if (!shell || ui.screen == SMK_UI_RACE)
                snprintf(mkey, sizeof mkey, "theme%d", trk.theme % SMK_THEME_COUNT);
            else if (ui.screen == SMK_UI_RESULT || ui.screen == SMK_UI_STANDINGS)
                snprintf(mkey, sizeof mkey, "results");
            else if (ui.screen == SMK_UI_TITLE)
                snprintf(mkey, sizeof mkey, "title");
            else
                snprintf(mkey, sizeof mkey, "menu");
            smk_music_set(mkey);
            smk_audio_pump();
        }

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
                /* the shell's own clicks, on the ROM's menu ids */
                /* one click for every screen: $4D used to be the
                 * course screen's, and $4D is not a menu sound at all
                 * (NOTES 235).  The ROM's fourth menu id IS $5B, but
                 * that one cannot be captured by poking during a race -
                 * the menus load their own sample bank - so until it is,
                 * the measured $2C serves everywhere. */
                if (nav.up || nav.down || nav.left || nav.right)
                    smk_sfx_play(SMK_SFX_MENU_MOVE);
                else if (nav.confirm) smk_sfx_play(SMK_SFX_MENU_OK);
                else if (nav.back)    smk_sfx_play(SMK_SFX_MENU_BACK);
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
                    smk_objgfx_load(&rom, trk.theme, &obj_art); obj_whole = (trk.theme == 3 || trk.theme == 5); build_track_map(&trk);
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
            if (in.toggle_map) { show_map = !show_map; in.toggle_map = false; }
            if (in.toggle_speedo) { show_speedo = !show_speedo; in.toggle_speedo = false; }
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
            /* Lakitu's lights, measured off the game's own start passage
             * (NOTES 217).  These are the cue a player times the turbo
             * launch against, so they play whether the music is on or
             * not - they are not part of the song. */
            if (race_count == SMK_COUNT_BEEP1 || race_count == SMK_COUNT_BEEP2)
                smk_sfx_play_name("count_beep");
            else if (race_count == SMK_COUNT_GO)
                smk_sfx_play_name("count_go");
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
                engine_throttle = thr;   /* the rev before the lights */
                if (race_count >= SMK_COUNT_FRAMES) {
                    race_state = RACE_RUN;
                    smk_player_launch(&player);   /* $80956A pays out here */
                    engine_throttle = false;
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
                /* BOTH halves of the hop.  Clearing only hop_held left the
                 * fresh PRESS - step_kart reads them separately, `pressed
                 * |= 0x0020` - so the countdown could still be hopped
                 * through, which is what the user found: "I am able to
                 * jump ... in the original game it is blocked".  Forcing
                 * the pad to L for 200 countdown frames in the ROM moves
                 * Z by exactly $0000 (NOTES 247). */
                in.hop_held = in.hop = false;
            }
            if (getenv("SMK_SPEED_TRACE") && race_state == RACE_RUN)
                printf("spd f%ld speed %d surf $%02X type %d drive $%02X fc %d state $%02X target %d at %d,%d z %d air %d\n",
                       hud_race_frames, kart.speed,
                       smk_track_surface(&trk, smk_kart_px(kart.x), smk_kart_px(kart.y)),
                       player.type, player.drive, player.fc, player.state, player.target,
                       smk_kart_px(kart.x), smk_kart_px(kart.y), (int)(kart.z >> 8), (int)kart.airborne);
            if (getenv("SMK_SPEED_TRACE")) {
                static int was_state = -1, was_screen = -1;
                if (race_state != was_state || ui.screen != was_screen) {
                    printf("race_state %d -> %d screen %d -> %d at f%ld\n",
                           was_state, race_state, was_screen, ui.screen, hud_race_frames);
                    was_state = race_state; was_screen = ui.screen;
                }
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
            /* $28 is the RESCUE (NOTES 245): $80:B644 plays it and then
             * falls straight into $80:B373, the routine that picks the
             * rescue waypoint.  $27 is the fall; $28 is Lakitu taking
             * hold, and the port had never played it. */
            if (player.hazard == 0x0E && rescue_t == 0) smk_sfx_play(SMK_SFX_RESCUE);
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
            {   /* SMK_MV_ON=1: release the movers from frame 0 (testing) */
                static int mv_on = -1;
                if (mv_on < 0) mv_on = getenv("SMK_MV_ON") ? 1 : 0;
                smk_course_movers_step(&crs, mv_on || crossings >= 2);
            }
            smk_obj_ticks = fx_ticks;   /* the moles' clock, shared with collide */
            /* shaking the mole off: three fresh hops (OURS - the user let
             * one ride forever by doing nothing, which is the measured
             * baseline; the shake count is not) */
            if (player.mole_on && in.hop && ++player.mole_hops >= 3)
                player.mole_on = 0;
            if (getenv("SMK_MV_TRACE") && (fx_ticks % 300) == 0)
                printf("mv f%ld cross %d z0 %d p0 %d z1 %d p1 %d\n",
                       hud_race_frames, crossings,
                       smk_mover_z(&crs, 0), crs.mv[0].phase,
                       smk_mover_z(&crs, 1), crs.mv[1].phase);
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
            /* The item button is an EDGE, and the edges are cleared right
             * here - before the item block runs - so a real press never
             * reached the roulette; only the test hook did, because it set
             * in.item after this line.  Capture it first.  One press stops
             * the roulette early ($81:B3C1), the next fires the item
             * ($81:B40A); the ROM tests the level, which would fire the
             * item the frame it turned READY if the button were still
             * down - the user asked for press, then press. */
            bool item_btn = in.item;
            {   /* SMK_ITEM_PRESS=f1,f2,... presses the button on those race
                 * frames, at the same point the pad would */
                static const char *ip = NULL; static bool ip_init = false;
                if (!ip_init) { ip_init = true; ip = getenv("SMK_ITEM_PRESS"); }
                if (ip) {
                    char buf[64]; snprintf(buf, sizeof buf, "%s", ip);
                    for (char *t = strtok(buf, ","); t; t = strtok(NULL, ","))
                        if (atol(t) == hud_race_frames) item_btn = true;
                }
            }
            input_edges_clear(&in);
            /* $94 = $A4 + $C0 + $AA/2 - and $AA lives in EVERY spin, not
             * just the shell tumble: the feather's $80B6D1 steps it, the
             * banana states carry it.  The camera turning with the spin
             * is the user's round-2 bug 14. */
            cam_spin = (player.state == 0x0A || player.state == 0x0C
                        || player.state == 0x0E || player.state == 0x10
                        || player.state == 0x18 || player.state == 0x1A)
                     ? player.plag / 2 : 0;
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
                /* THE ITEMS (docs/ITEMS.md).  The word steps every frame;
                 * the button is the LEVEL, as the game tests it. */
                {
                    /* off-road is not a reason to refuse an item: the user
                     * "used a mushroom for a boost while going off road and
                     * there was no boost effect. The mushroom should also
                     * work here, since it is used to get out from those
                     * areas quickly".  Only the fall, the carry and the drop
                     * (6 / $0C / $0E) hold the item. */
                    bool can_use = !kart.airborne
                                   && player.hazard != 6 && player.hazard != 0x0C && player.hazard != 0x0E
                                   && player.state != 0x0A && player.state != 0x0C
                                   && player.state != 0x1A;
                    {   /* SMK_ITEM_TEST=id:frame - the item appears READY on that
                         * frame and is fired the next, with no box, so every
                         * effect can be shot headlessly */
                        static int tid = -2, tframe = 0;
                        if (tid == -2) {
                            tid = -1;
                            const char *e = getenv("SMK_ITEM_TEST");
                            if (e && strchr(e, ':')) { tid = atoi(e); tframe = atoi(strchr(e, ':') + 1); }
                        }
                        if (tid >= 0 && hud_race_frames == tframe) {
                            /* id 99: a BOX instead - the whole roulette, from the tables */
                            if (tid == 99) smk_item_box(&item, &itemtab, cur_track, hud_lap, hud_rank - 1, item_roll());
                            else item.word = (uint16_t)(0xC000 | tid);
                        }
                        if (tid >= 0 && tid != 99 && hud_race_frames == tframe + 1) item_btn = true;
                    }
                    {   /* SMK_SQUASH_TEST=frame: flatten P1 and kart 1 on
                         * that frame - bug 13's art, eyeballed headlessly */
                        static int sqf = -2;
                        if (sqf == -2) { const char *e = getenv("SMK_SQUASH_TEST"); sqf = e ? atoi(e) : -1; }
                        if (sqf >= 0 && hud_race_frames == sqf) {
                            player.squash_t = SMK_SQUASH_T;
                            racers[1].squash_t = SMK_SQUASH_T;
                        }
                    }
                    bool was_spin = smk_item_spinning(&item);
                    int used = smk_item_step(&item, item_btn, can_use);
                    player.item_held = smk_item_present(&item);
                    /* MEASURED (NOTES 218): the game plays $55 on the
                     * frame the roulette STOPS - every one of ten spins
                     * in the recording, at the exact frame the $2000 bit
                     * clears - not when the item becomes usable 65
                     * frames later, which is where this used to fire.
                     * Hitting the box itself is silent. */
                    if (was_spin && !smk_item_spinning(&item) && smk_item_present(&item))
                        smk_sfx_play(SMK_SFX_ITEMBOX);
                    /* THE ROULETTE ITSELF (NOTES 220): the game holds one
                     * voice from the box hit to the stop, stepping its
                     * pitch through eight notes - the user: "a cyclic
                     * sound rapidly going through 6 notes".  It is not
                     * queued, which is why it looked absent; it is held,
                     * so the port holds it too. */
                    smk_sfx_loop("roulette", smk_item_spinning(&item));
                    if (used >= 0) {
                        item_used_once = true;
                        int ahead_idx = -1;                 /* the red shell's target */
                        for (int q = 1; q < SMK_CHARACTERS; q++)
                            if (racers[q].rank == racers[0].rank - 1) { ahead_idx = q; break; }
                        if (ahead_idx < 0)
                            for (int q = 1; q < SMK_CHARACTERS; q++)
                                if (racers[q].rank == racers[0].rank + 1) { ahead_idx = q; break; }
                        switch (used) {
                        case SMK_ITEM_MUSHROOM:  smk_player_boost(&player); break;
                        case SMK_ITEM_FEATHER:   smk_player_feather(&player, &kart); smk_sfx_play(SMK_SFX_FEATHER); break;
                        case SMK_ITEM_STAR:      smk_player_star(&player); break;
                        /* the user: the button alone THROWS (banana in an arc,
                         * shell fast and bouncing); button + DOWN leaves it
                         * behind you, static - the same for both */
                        case SMK_ITEM_BANANA: {
                            /* MEASURED (NOTES 232): a banana thrown ahead
                             * makes NO sound at all; left behind it is $58
                             * from $84:D8F2.  One flag for both the throw
                             * and its sound, so a debug override cannot
                             * make them disagree. */
                            /* MEASURED (NOTES 240), and the user found the
                             * missing input: UP.  A banana's DEFAULT is to be
                             * left behind - button alone gives an object at
                             * speed 0 sitting where it was dropped, and no
                             * sound at all.  Held UP it is THROWN, the object
                             * flies at 1596, and $2B plays two frames later:
                             * the same falling sound as a shell, which is
                             * what the user heard ("same pitch drop").
                             * The $58 that used to play here was NOTES 232's
                             * mis-attribution; dropping one is silent. */
                            bool ahead = in.dpad_up || getenv("SMK_BANANA_UP") != NULL;
                            if (ahead) smk_sfx_play(SMK_SFX_THROW);
                            smk_proj_throw(projs, SMK_PROJ_MAX, SMK_PROJ_BANANA, &kart,
                                           player.heading, 0, -1, false, ahead);
                            break;
                        }
                        case SMK_ITEM_GREEN: {
                            /* A shell's default is the other way round: the
                             * button alone THROWS it (the object flies at
                             * 1596) and $2B plays; held DOWN it is dropped at
                             * speed 0 and is SILENT (NOTES 240).  The $58
                             * that used to play on the drop was measured
                             * away with the banana's. */
                            bool behind = in.dpad_down || getenv("SMK_SHELL_DOWN") != NULL;
                            if (!behind) smk_sfx_play(SMK_SFX_THROW);
                            smk_proj_throw(projs, SMK_PROJ_MAX, SMK_PROJ_GREEN, &kart,
                                           player.heading, 0, -1, behind, false);
                            break;
                        }
                        case SMK_ITEM_RED:
                            smk_sfx_play(SMK_SFX_THROW);
                            smk_proj_throw(projs, SMK_PROJ_MAX, SMK_PROJ_RED, &kart,
                                           player.heading, 0, ahead_idx, false, false);
                            break;
                        case SMK_ITEM_BOO:
                            player.boo_t = 0x480;
                            /* MEASURED by forcing the item in the oracle
                             * (NOTES 227): the Boo handler plays $57 from
                             * $80:E9C1 - $56 is something else. */
                            smk_sfx_play(SMK_SFX_BOO);
                            break;
                        case SMK_ITEM_COIN:
                            player.coins += 2; if (player.coins > 99) player.coins = 99;
                            smk_coinfx_pickup2(coins_fx, SMK_COINFX_MAX);
                            /* TWO coins, so two coin sounds - and the game
                             * spaces them SEVEN FRAMES apart (measured at
                             * two +2 pickups in the recordings) */
                            smk_sfx_play(SMK_SFX_COIN);
                            smk_sfx_after(SMK_SFX_COIN, 7);
                            break;
                        case SMK_ITEM_LIGHTNING:
                            for (int q = 1; q < SMK_CHARACTERS; q++)
                                smk_racer_hit(&racers[q], 3, (int)(fx_ticks & 1));
                            break;
                        default: break;
                        }
                        /* the feather launched `kart` AFTER the me->k sync
                         * above; without this the collide pass copies the
                         * stale grounded kart back and the flight is lost
                         * the same frame (bug 6) */
                        me->k = kart;
                    }
                    if (getenv("SMK_ITEM_TRACE")) {
                        printf("item f%ld word=$%04X t=%d used=%d | kart (%d,%d) spd %d st=%02X drv=%02X coins %d |",
                               hud_race_frames, item.word, item.timer, used,
                               smk_kart_px(kart.x), smk_kart_px(kart.y), kart.speed,
                               player.state, player.drive, player.coins);
                        for (int q = 0; q < SMK_PROJ_MAX; q++)
                            if (projs[q].kind)
                                printf(" P%d k%d (%d,%d,z%d) v(%d,%d) h%04X t%d%s", q, projs[q].kind,
                                       smk_kart_px(projs[q].x), smk_kart_px(projs[q].y),
                                       (int)(projs[q].z / 25029), projs[q].vx, projs[q].vy,
                                       projs[q].heading, projs[q].t, projs[q].dying ? " dying" : "");
                        printf("\n");
                    }
                    /* THE AI'S WEAPONS (NOTES 190).  The user's rule, and the
                     * `attack` recording: only against the player, only
                     * from lap 2, only when the player is near; the object
                     * rides behind the kart for 58 frames and is let go.
                     * The cooldown and the distance are OURS, bounded by
                     * the five drops measured (S31). */
                    if (!replay_path)
                        for (int q = 1; q < SMK_CHARACTERS; q++) {
                            smk_racer *r = &racers[q];
                            if (r->weapon_cool > 0) r->weapon_cool--;
                            if (r->star_t > 0) r->star_t--;
                            int wp = smk_ai_weapon_of(r->character % SMK_CHARACTERS);
                            if (wp == SMK_AI_WEAPON_NONE || r->weapon_cool > 0 || r->hit_t > 0) continue;
                            if (r->lap < 2 || r->finish_frame >= 0) continue;
                            int ddx = smk_kart_px(r->k.x) - smk_kart_px(kart.x);
                            int ddy = smk_kart_px(r->k.y) - smk_kart_px(kart.y);
                            if (ddx * ddx + ddy * ddy > SMK_AI_NEAR * SMK_AI_NEAR) continue;
                            if (wp == SMK_AI_WEAPON_STAR) r->star_t = 0x200;
                            else smk_proj_ai_drop(projs, SMK_PROJ_MAX, wp, &r->k, q);
                            r->weapon_cool = SMK_AI_COOL;
                            if (getenv("SMK_ITEM_TRACE"))
                                printf("AI %d (%s) uses weapon %d at (%d,%d), player %d px away, lap %d\n", q,
                                       SMK_DRIVERS[r->character % SMK_CHARACTERS].name, wp,
                                       smk_kart_px(r->k.x), smk_kart_px(r->k.y), (int)sqrt((double)(ddx * ddx + ddy * ddy)), r->lap);
                        }
                    /* the projectiles fly, and anything they touch reacts */
                    const smk_kart *field_k[SMK_CHARACTERS];
                    for (int q = 0; q < SMK_CHARACTERS; q++) field_k[q] = &racers[q].k;
                    field_k[0] = &kart;
                    smk_proj_step(projs, SMK_PROJ_MAX, &trk, field_k, SMK_CHARACTERS);
                    int hk = smk_proj_hit(projs, SMK_PROJ_MAX, &kart, 0);
                    if (hk == SMK_PROJ_BANANA) {
                        if (smk_player_hit_banana(&player, &kart)) {
                            int lost = player.coins < 4 ? player.coins : 4;   /* $85:E4DA */
                            player.coins -= lost;
                            smk_coinfx_spawn(coins_fx, SMK_COINFX_MAX, kart.x, kart.y,
                                             player.heading, kart.vx, kart.vy, lost);
                        }
                    } else if (hk == SMK_PROJ_MUSHROOM) {
                        /* the poison mushroom: the SHRINK only - "doesn't
                         * trigger spinning, it triggers the shrinking
                         * animation" (the user); $80:EA3B's $0300 tumble is
                         * the lightning's, not this */
                        if (!(player.flags & 2)) {
                            /* round 2, bug 21: "when small, eating another
                             * poison mushroom makes you come back" */
                            player.shrink_t = player.shrink_t > 0 ? 0 : 0x440;
                            int lost = player.coins < 4 ? player.coins : 4;
                            player.coins -= lost;
                            smk_coinfx_spawn(coins_fx, SMK_COINFX_MAX, kart.x, kart.y,
                                             player.heading, kart.vx, kart.vy, lost);
                        }
                    } else if (hk == SMK_PROJ_GREEN || hk == SMK_PROJ_RED
                               || hk == SMK_PROJ_EGG || hk == SMK_PROJ_FIREBALL) {
                        if (smk_player_hit_shell(&player, &kart, (int)(fx_ticks & 1))) {
                            int lost = player.coins < 4 ? player.coins : 4;
                            player.coins -= lost;
                            smk_coinfx_spawn(coins_fx, SMK_COINFX_MAX, kart.x, kart.y,
                                             player.heading, kart.vx, kart.vy, lost);
                        }
                    }
                    for (int q = 1; q < SMK_CHARACTERS; q++) {
                        int hq = smk_proj_hit(projs, SMK_PROJ_MAX, &racers[q].k, q);
                        if (hq && getenv("SMK_ITEM_TRACE"))
                            printf("item HIT kart %d (%s) at (%d,%d) rank %d by kind %d; player at (%d,%d) rank %d\n",
                                   q, SMK_DRIVERS[racers[q].character % SMK_CHARACTERS].name,
                                   smk_kart_px(racers[q].k.x), smk_kart_px(racers[q].k.y), racers[q].rank, hq,
                                   smk_kart_px(kart.x), smk_kart_px(kart.y), racers[0].rank);
                        if (hq) {
                            /* the user: hitting an AI is BOTH - the hit and
                             * the spin it puts them into */
                            smk_sfx_play(SMK_SFX_AI_HIT);
                            smk_sfx_play(SMK_SFX_AI_FELL);
                        }
                        if (hq == SMK_PROJ_BANANA) smk_racer_hit(&racers[q], 1, (int)(fx_ticks & 1));
                        else if (hq == SMK_PROJ_MUSHROOM) { if (racers[q].star_t <= 0) racers[q].shrink_t = racers[q].shrink_t > 0 ? 0 : 0x440; }   /* shrink only; a second one restores (bug 21) */
                        else if (hq != SMK_PROJ_NONE) smk_racer_hit(&racers[q], 2, (int)(fx_ticks & 1));
                    }
                }
                /* SMK_PLAYER_AT=x,y:frame - put the player there on that race frame */
                { const char *e = getenv("SMK_PLAYER_AT");
                  if (e && strchr(e, ':') && hud_race_frames == atol(strchr(e, ':') + 1)) {
                      int ax = atoi(e), ay = atoi(strchr(e, ',') + 1);
                      kart.x = (int32_t)ax << SMK_POS_SHIFT; kart.y = (int32_t)ay << SMK_POS_SHIFT;
                      me->k = kart;   /* or the collide pass copies the old
                                       * position straight back - the same
                                       * lost-write as the feather (NOTES 203) */
                  } }
                /* SMK_TEST_PLACE=q:d - park racer q d px straight ahead of
                 * the player every frame, to look at one kart at one distance */
                { const char *e = getenv("SMK_TEST_PLACE");
                  if (e && strchr(e, ':')) {
                      int q = atoi(e), dd = atoi(strchr(e, ':') + 1);
                      if (q >= 1 && q < SMK_CHARACTERS) {
                          int16_t sx, cy; smk_dsp_sincos(player.heading, 256, &sx, &cy);
                          racers[q].k.x = kart.x + (((int32_t)sx * dd) << (SMK_POS_SHIFT - 8));
                          racers[q].k.y = kart.y - (((int32_t)cy * dd) << (SMK_POS_SHIFT - 8));
                          racers[q].k.angle = player.heading; racers[q].k.speed = 0;
                          racers[q].k.vx = racers[q].k.vy = 0;
                      }
                  } }
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
                /* THE COIN LOSS, which this port never had.  $85:E4B2
                 * takes one coin on a plain contact - the user: "regular
                 * bumps against other players is just one coin" - and
                 * $85:E4E5 takes four for an item hit, which is not
                 * reachable yet with no items.  The coins are thrown up
                 * and fall back (NOTES 183). */
                int8_t was_cool = kart.bump_cool;
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
                if (getenv("SMK_SQUASH_TRACE"))
                    for (int q = 1; q < SMK_CHARACTERS; q++)
                        if (racers[q].squash_t == SMK_SQUASH_T)
                            printf("squash f%ld: kart %d\n", hud_race_frames, q);
                /* a FRESH contact on the player costs one coin, and the
                 * coins are thrown out of him (NOTES 183) */
                /* not while replaying: coins set the top speed
                 * ($D6 = $B4 + 8*min(coins,10)), so dropping one would
                 * make the ghost diverge from the recording it is being
                 * compared against. */
                bool star_bumped_player = false;
                for (int q = 1; q < SMK_CHARACTERS; q++)
                    if (racers[q].star_t > 0 && racers[q].k.bump_cool == SMK_BUMP_COOL)
                        star_bumped_player = true;
                if (kart.bump_cool == SMK_BUMP_COOL && was_cool == 0 && !replay_path
                    && star_bumped_player) {
                    /* bumped by an INVINCIBLE kart: the banana roll, no coin
                     * rule (the user) */
                    smk_player_hit_banana(&player, &kart);
                } else if (kart.bump_cool == SMK_BUMP_COOL && was_cool == 0 && !replay_path) {
                    if (player.coins > 0) {
                        player.coins--;
                        smk_coinfx_spawn(coins_fx, SMK_COINFX_MAX,
                                         kart.x, kart.y, player.heading,
                                         kart.vx, kart.vy, 1);
                    } else {
                        /* no coin to lose: the bump spins you out instead.
                         * The user: "bumping another kart with no coins in
                         * the pocket, the kart spins and ends up with zero
                         * speed" - the same reaction as a banana. */
                        smk_player_hit_bump(&player, &kart);
                    }
                }
                /* and the same for every AI kart it touched */
                {
                    static int8_t was_ai[SMK_CHARACTERS];
                    for (int q = 1; q < SMK_CHARACTERS; q++) {
                        int8_t bc = racers[q].k.bump_cool;
                        /* the user: "When bumping an AI player, they should
                         * never get the spinning animation. They don't get
                         * affected by the coin situation as the player.
                         * This applies also to them hitting between
                         * themselves." - so a bump costs an AI nothing */
                        if (bc == SMK_BUMP_COOL && was_ai[q] == 0 && !replay_path) {
                            if (player.star_t > 0) smk_racer_hit(&racers[q], 1, (int)(fx_ticks & 1));
                            else if (racers[q].coins > 0) racers[q].coins--;
                        }
                        was_ai[q] = bc;
                    }
                }
            }
            /* SMK_COIN_TEST=frame - throw coins on that race frame with
             * no collision needed, so the arc can be shot and looked at */
            { const char *e = getenv("SMK_COIN_TEST");
              if (e && hud_race_frames == atol(e))
                  smk_coinfx_spawn(coins_fx, SMK_COINFX_MAX,
                                   kart.x, kart.y, player.heading,
                                   kart.vx, kart.vy, 1); }
            smk_coinfx_step(coins_fx, SMK_COINFX_MAX);

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
                smk_sfx_play(SMK_SFX_FINISH);    /* the user: "getting to the goal" */
                if (race_mode == SMK_MODE_TT) {
                    if (shell || getenv("SMK_RESULT_SHOT")) { smk_ui_gp_award(&ui, &result); ui.screen = SMK_UI_RESULT; }
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
                if (shell || getenv("SMK_RESULT_SHOT")) { smk_ui_gp_award(&ui, &result); ui.screen = SMK_UI_RESULT; }
            }
        }
        (void)stepped;   /* edges deliberately survive a tickless iteration */

        /* SMK_RESULT_SHOT draws the results screen without the shell, which
         * --race bypasses - otherwise the layout can only be seen by
         * playing through the menus. */
        if (tex && fb && (shell || getenv("SMK_RESULT_SHOT"))
            && ui.screen != SMK_UI_RACE) {
            if (ui.screen == SMK_UI_STANDINGS)
                smk_ui_draw_standings(&ui, &rom, &menu_font, fb, rw, rh);
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
            /* The SPIN states draw through the FULL rotation rule, not
             * frame_for's seven bands: those cap at the side-on frame, so
             * half of every spin showed one frame and the feather's 360
             * never read as a rotation (round 2, bug 3).  The pose lag
             * carries the spin; the AI's measured heading rule turns it
             * into the whole frame circle. */
            if (player.state == 0x0A || player.state == 0x0C
                || player.state == 0x0E || player.state == 0x10
                || player.state == 0x18 || player.state == 0x1A) {
                uint16_t r16 = (uint16_t)player_slip_units;
                int ar16 = (int16_t)r16 < 0 ? -(int16_t)r16 : (int16_t)r16;
                if (ar16 < 0x0400) pframe = 1000;
                else {
                    bool hfs = false;
                    int fs = smk_sprite_for_heading(SMK_SPR_TIER0, r16, &hfs);
                    pframe = hfs ? -fs : fs;
                }
            }
            {
                int af = pframe < 0 ? -pframe : pframe;
                if (af > 7 && af != 47 && pframe != 1000) af = 7;   /* the fx column caps at 7 */
                /* the game's $BC frame index: 0 straight, 1..7 the rotation
                 * bands; the drift-onset sheet frame 47 counts as band 1
                 * (LABELLED: its $BC index is not measured) */
                fx_frame_idx = (pframe == 1000) ? 0 : (af == 47 ? 1 : af);
                fx_mirror = pframe != 1000 && pframe < 0;
            }
            draw_scene(&rom, &trk, &karts, drv, &cam, fb, rw, rh,
                       show_grid, show_kart, pframe,
                       (uint16_t)(kart.angle + finish_yaw), racers, &crs);
            if (!celebrating && show_speedo)
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
