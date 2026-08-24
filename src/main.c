/* SDL2 host for the Super Mario Kart reimplementation. */
#include "smk.h"

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
    bool quit;
    /* sticky edges: set by events, cleared only when a tick consumes them */
    bool next_track, prev_track, next_pal, prev_pal, toggle_filter;
} input_state;

static void input_edges_clear(input_state *in)
{
    in->next_track = in->prev_track = false;
    in->next_pal = in->prev_pal = in->toggle_filter = false;
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
} smk_racer;

static void racer_start(smk_racer *r, const smk_track *trk, int slot)
{
    float x, y, a;
    memset(r, 0, sizeof *r);
    smk_track_start(trk, slot, &x, &y, &a);
    r->k.x = (int32_t)(x * SMK_POS_ONE);
    r->k.y = (int32_t)(y * SMK_POS_ONE);
    r->k.angle = 0;
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
        if (r->sector >= crs->sectors - 2 && sec <= 1)
            r->lap++;
        else if (sec >= crs->sectors - 2 && r->sector <= 1)
            r->lap--;
        r->sector = sec;
        int prog = (r->lap << 8) | sec;
        if (prog > r->progress_max) r->progress_max = prog;
    }

    int next = r->sector + 1;
    if (next >= crs->sectors) next = 0;
    uint16_t want = heading_to(&r->k, crs->wx[next], crs->wy[next]);
    int16_t diff = (int16_t)(want - r->k.angle);
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
     * waypoint attribute's low two bits, offset by the kart's $C8 row -
     * the demo AI runs at row +4 (measured speeds 700-1050). */
    int target = (int16_t)phys->w[SMK_PHYS_TARGET + 4 + (crs->wattr[r->sector] & 3)];
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
    int32_t accel;
    if (k->speed < target)
        accel = (int32_t)smk_physics_accel(phys, k->speed) << 8;   /* $80B043 */
    else if (k->speed > target)
        accel = -(int32_t)(target == 0 ? FEEL_DRAG : FEEL_BRAKE) << 8;
    else
        accel = 0;
    k->accel      = (int16_t)(accel >> 16);
    k->accel_frac = (uint16_t)(accel & 0xFFFF);

    smk_kart_accelerate(k);      /* the ROM's 32-bit speed integration */

    if (k->speed > target && target > 0) { k->speed = (int16_t)target; }

    /* steering authority falls off as the kart slows, as it must */
    int auth = (k->speed < 0 ? -k->speed : k->speed);
    if (auth > top) auth = top;
    int turn = top ? FEEL_TURN * auth / top : 0;
    if (in->left)  k->angle -= (uint16_t)turn;
    if (in->right) k->angle += (uint16_t)turn;

    smk_kart_face(k);            /* the ROM's (sin, -cos) * speed */
    smk_kart_move(k, trk);       /* the ROM's position += velocity << 8 */
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
                       uint16_t cam_heading, const smk_racer *racers)
{
    if (show_grid && karts->frames && racers) {
        static smk_sprites other[SMK_CHARACTERS];
        static bool loaded[SMK_CHARACTERS];
        for (int k = 1; k < SMK_CHARACTERS; k++) {
            float px, py, sc;
            float gx = (float)smk_kart_px(racers[k].k.x);
            float gy = (float)smk_kart_px(racers[k].k.y);
            if (!smk_project(cam, gx, gy, rw, rh, &px, &py, &sc)) continue;
            /* a kart is roughly 20 world units across and the sprite is 32
             * pixels, so it wants about 20/32 of the projected size */
            int scale = (int)(sc * 0.62f + 0.5f);
            if (scale < 1) scale = 1;
            if (scale > rh / 90) scale = rh / 90;
            const smk_driver *d2 = &SMK_DRIVERS[k];
            if (!loaded[k]) loaded[k] = smk_sprites_load(rom, d2->sheet, &other[k]);
            if (!loaded[k]) continue;
            int tier = scale > 3 ? SMK_SPR_TIER0
                     : scale > 1 ? SMK_SPR_TIER1 : SMK_SPR_TIER2;
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
        int scale = rh / 112;
        if (scale < 1) scale = 1;
        bool hf = frame < 0;
        smk_draw_sprite(karts, hf ? -frame : frame, trk->palette, drv->pal,
                        rw / 2, rh - rh / 12, scale, hf, fb, rw, rh, rw);
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
    float want = (in->left ? -1.0f : 0.0f) + (in->right ? 1.0f : 0.0f);
    *lean += (want - *lean) * 0.25f;
    uint16_t rel = (uint16_t)(int)(*lean * (float)0x1C00);
    bool hf = false;
    int f = smk_sprite_for_heading(SMK_SPR_TIER0, rel, &hf);
    return hf ? -f : f;
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
        fwrite(trk.map, 1, sizeof trk.map, f);
        fwrite(trk.tiles, 1, sizeof trk.tiles, f);
        fwrite(trk.palette, 4, 256, f);
        fclose(f);
        printf("track %d theme %d -> %s\n", track, trk.theme, dump);
        smk_rom_free(&rom);
        return 0;
    }

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
                racer_start(&shot_racers[i], &trk, i);
            draw_scene(&rom, &trk, &karts, drv, &c, px, sw, sh,
                       show_grid, show_kart, frame_for(&none, &lz),
                       heading, shot_racers);
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
    static smk_racer racers[SMK_CHARACTERS];
    for (int i = 0; i < SMK_CHARACTERS; i++)
        racer_start(&racers[i], &trk, i);
    smk_racer *me = &racers[0];

    float lean = 0.0f;
    smk_kart kart = {
        .x = (int32_t)(shot_x * SMK_POS_ONE),
        .y = (int32_t)(shot_y * SMK_POS_ONE),
        .angle = (uint16_t)(shot_a * (float)SMK_ANGLE_TURN / (2.0f * (float)M_PI)
                            + SMK_ANGLE_TURN / 4),
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
                    float sx, sy, sa;
                    track = nt; theme = nth;
                    smk_track_start(&trk, 0, &sx, &sy, &sa);
                    kart = (smk_kart){ .x = (int32_t)(sx * SMK_POS_ONE),
                                       .y = (int32_t)(sy * SMK_POS_ONE),
                                       .angle = 0 };
                    for (int i = 0; i < SMK_CHARACTERS; i++)
                        racer_start(&racers[i], &trk, i);
                    camera_from_kart(&cam, &kart);
                } else {
                    fprintf(stderr, "skipped: %s\n", err);
                    smk_track_load(&rom, track, theme, &trk, err, sizeof err);
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
                    if (me->sector >= crs.sectors - 2 && sec <= 1)
                        me->lap++;
                    else if (sec >= crs.sectors - 2 && me->sector <= 1)
                        me->lap--;
                    me->sector = sec;
                }
            }
        }
        (void)stepped;   /* edges deliberately survive a tickless iteration */

        if (tex && fb) {
            smk_render_mode7(&trk, &cam, fb, rw, rh, rw);
            draw_scene(&rom, &trk, &karts, drv, &cam, fb, rw, rh,
                       show_grid, show_kart, frame_for(&in, &lean),
                       kart.angle, racers);
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
                     "Super Mario Kart  -  track %d  lap %d  sector %d/%d  -  "
                     "%dx%d  %.0f fps", track, me->lap + 1,
                     me->sector, crs.sectors, rw, rh, frames / secs);
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
