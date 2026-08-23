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
    bool next_track, prev_track, next_pal, prev_pal, next_tiles, toggle_filter;
} input_state;

static void input_edges_clear(input_state *in)
{
    in->next_track = in->prev_track = false;
    in->next_pal = in->prev_pal = in->next_tiles = in->toggle_filter = false;
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
            case SDLK_t: in->next_tiles = true; break;
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
/* Provisional kart-ish motion.  This is NOT the game's physics - Super
 * Mario Kart's handling lives in its own routines and has not been decoded
 * yet.  Named here so it is never mistaken for a faithful decode.          */
typedef struct {
    float speed;        /* world px per tick */
    float turn;
} kart_feel;

static const kart_feel FEEL = { .speed = 3.6f, .turn = 0.048f };

static void step_camera(smk_camera *cam, const input_state *in, float *vel)
{
    float target = 0.0f;
    if (in->up)   target += FEEL.speed * (in->shift ? 1.9f : 1.0f);
    if (in->down) target -= FEEL.speed * 0.55f;

    *vel += (target - *vel) * 0.09f;           /* simple lag, not the game's */

    float steer = 0.0f;
    if (in->left)  steer -= FEEL.turn;
    if (in->right) steer += FEEL.turn;
    /* steering authority falls off when barely moving, as it should */
    cam->angle += steer * (0.35f + 0.65f * fminf(fabsf(*vel) / FEEL.speed, 1.0f));

    cam->x += cosf(cam->angle) * *vel;
    cam->y += sinf(cam->angle) * *vel;

    /* the world wraps; the SNES tilemap does too */
    if (cam->x < 0) cam->x += SMK_WORLD_PX;
    if (cam->y < 0) cam->y += SMK_WORLD_PX;
    if (cam->x >= SMK_WORLD_PX) cam->x -= SMK_WORLD_PX;
    if (cam->y >= SMK_WORLD_PX) cam->y -= SMK_WORLD_PX;
}

/* ------------------------------------------------------------------ */
static void usage(const char *argv0)
{
    printf("usage: %s [options]\n"
           "  --rom PATH      Super Mario Kart (USA) ROM   [rom/smk_usa.sfc]\n"
           "  --track N       0..23  (20 courses + 4 battle arenas)\n"
           "  --tileset N     Mode 7 tileset index         [1]\n"
           "  --palette N     palette index                [0]\n"
           "  --width W       window width                 [1024]\n"
           "  --height H      window height                [896]\n"
           "  --pixel N       render at 1/N resolution     [2]\n"
           "  --fullscreen\n"
           "  --frames N      run N frames then exit (benchmark)\n"
           "  --shot PATH     render one frame to a BMP and exit\n"
           "  --at X Y DEG    camera placement for --shot\n"
           "  --height-cam H  eye height above the plane   [15]\n"
           "  --horizon F     horizon row, 0..1            [0.36]\n"
           "  --fov F         focal length scale           [0.55]\n\n"
           "  arrows/WASD steer and accelerate, shift = boost\n"
           "  [ ] change track, o p change palette, t change tileset\n"
           "  f toggles linear filtering, esc quits\n", argv0);
}

int main(int argc, char **argv)
{
    const char *rom_path = "rom/smk_usa.sfc";
    int track = 0, tileset = 1, palette = 0;
    int win_w = 1024, win_h = 896, pixel = 2, fullscreen = 0;
    const char *shot = NULL;          /* render one frame to a BMP and exit */
    float shot_x = 512, shot_y = 512, shot_a = 0;
    int have_at = 0;
    long max_frames = 0;              /* >0: run headless for N frames, then exit */
    float cam_height = 15.0f, cam_horizon = 0.36f, cam_fov = 0.55f;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        #define ARG(name, var) if (!strcmp(a, name) && i + 1 < argc) { var = atoi(argv[++i]); continue; }
        if (!strcmp(a, "--rom") && i + 1 < argc) { rom_path = argv[++i]; continue; }
        ARG("--track", track) ARG("--tileset", tileset) ARG("--palette", palette)
        ARG("--width", win_w) ARG("--height", win_h) ARG("--pixel", pixel)
        if (!strcmp(a, "--fullscreen")) { fullscreen = 1; continue; }
        if (!strcmp(a, "--frames") && i + 1 < argc) { max_frames = atol(argv[++i]); continue; }
        if (!strcmp(a, "--shot") && i + 1 < argc) { shot = argv[++i]; continue; }
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

    static smk_track trk;
    if (!smk_track_load(&rom, track, tileset, palette, &trk, err, sizeof err)) {
        fprintf(stderr, "error: %s\n", err);
        smk_rom_free(&rom);
        return 1;
    }
    if (!have_at) smk_track_guess_start(&trk, &shot_x, &shot_y, &shot_a);
    printf("loaded \"%s\"\n", rom.title);
    printf("track %d, tileset %d, palette %d\n", track, tileset, palette);

    /* Headless single-frame render: no window, no event loop.  Also the
     * cheapest way to eyeball the renderer from a script. */
    if (shot) {
        int sw = win_w / pixel, sh = win_h / pixel;
        uint32_t *px = malloc((size_t)sw * (size_t)sh * sizeof *px);
        smk_camera c = { .x = shot_x, .y = shot_y, .angle = shot_a,
                         .height = cam_height, .horizon = cam_horizon,
                         .fov = cam_fov };
        smk_render_mode7(&trk, &c, px, sw, sh, sw);
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

    smk_camera cam = { .x = shot_x, .y = shot_y, .angle = shot_a,
                       .height = cam_height, .horizon = cam_horizon,
                       .fov = cam_fov };
    float vel = 0.0f;

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

            if (in.next_track || in.prev_track || in.next_pal || in.prev_pal
                || in.next_tiles) {
                int nt = track, np = palette, ns = tileset;
                if (in.next_track) nt = (track + 1) % SMK_TRACK_COUNT;
                if (in.prev_track) nt = (track + SMK_TRACK_COUNT - 1) % SMK_TRACK_COUNT;
                if (in.next_pal)   np = (palette + 1) & 7;
                if (in.prev_pal)   np = (palette + 7) & 7;
                if (in.next_tiles) ns = (tileset + 1) & 7;
                if (smk_track_load(&rom, nt, ns, np, &trk, err, sizeof err)) {
                    track = nt; palette = np; tileset = ns;
                    smk_track_guess_start(&trk, &cam.x, &cam.y, &cam.angle);
                    vel = 0;
                } else {
                    fprintf(stderr, "skipped: %s\n", err);
                    /* restore whatever was last good */
                    smk_track_load(&rom, track, tileset, palette, &trk, err, sizeof err);
                }
            }
            if (in.toggle_filter) {
                filter = !filter;
                SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, filter ? "1" : "0");
                if (tex) { SDL_DestroyTexture(tex); tex = NULL; rw = rh = 0; }
            }
            input_edges_clear(&in);

            step_camera(&cam, &in, &vel);
        }
        (void)stepped;   /* edges deliberately survive a tickless iteration */

        if (tex && fb) {
            smk_render_mode7(&trk, &cam, fb, rw, rh, rw);
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
                     "Super Mario Kart  -  track %d  tileset %d  palette %d  -  "
                     "%dx%d  %.0f fps", track, tileset, palette, rw, rh,
                     frames / secs);
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
