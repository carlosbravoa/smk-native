/* Music playback (P7, NOTES 202) - SDL_mixer edition.
 *
 * The songs are PRE-RECORDED digital dumps of the game's own sound driver
 * (an .spc snapshot rendered by libgme, docs/SOUND.md); the mapping from
 * game state to file is the user's rom/music/map.txt.  The first player
 * hand-fed SDL_QueueAudio and starved ("tremendously stuck" - the user),
 * so the playing is SDL_mixer's now: load the file, loop it, done.
 *
 * Keys the port asks for: title menu results standings theme0..theme7.
 * A missing map, key or file is silence, never an error.  N toggles.
 */
#include "smk.h"
#include <SDL.h>
#include <SDL_mixer.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

static bool     ready;
static Mix_Music *cur, *loop_next;
static char     cur_key[32];
static char     map_dir[512];
/* OFF by default (the user, 2026-08-31): "let's focus on sfx.  Disable
 * music for the moment so we can do clean testing".  N turns it on, and
 * SMK_MUSIC=1 starts with it on for a music session. */
static bool     music_on;
static bool     music_checked;

/* the intro has finished: the loop file takes over, forever */
static void on_music_done(void)
{
    if (loop_next) {
        Mix_Music *l = loop_next;
        loop_next = NULL;
        if (cur) Mix_FreeMusic(cur);
        cur = l;
        Mix_PlayMusic(cur, -1);
        if (!music_on) Mix_PauseMusic();
    }
}

bool smk_audio_init(void)
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return false;
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) != 0) return false;
    Mix_AllocateChannels(16);        /* several effects can overlap */
    Mix_HookMusicFinished(on_music_done);
    ready = true;
    return true;
}

void smk_audio_set_dir(const char *rom_path)
{
    snprintf(map_dir, sizeof map_dir, "%s", rom_path);
    char *slash = strrchr(map_dir, '/');
    if (slash) slash[1] = 0; else map_dir[0] = 0;
}

void smk_music_toggle(void)
{
    music_checked = true;
    music_on = !music_on;
    if (!ready) return;
    if (!music_on) Mix_PauseMusic(); else Mix_ResumeMusic();
}

void smk_music_set(const char *key)
{
    if (!music_checked) { music_checked = true; music_on = getenv("SMK_MUSIC") != NULL; }
    if (!music_on) return;             /* not even loaded: silence is silence */
    if (!ready || !key || !strcmp(key, cur_key)) return;
    snprintf(cur_key, sizeof cur_key, "%s", key);
    if (getenv("SMK_MUSIC_TRACE")) printf("music: set %s\n", key);
    Mix_HaltMusic();
    if (loop_next) { Mix_FreeMusic(loop_next); loop_next = NULL; }
    if (cur) { Mix_FreeMusic(cur); cur = NULL; }
    char path[900]; snprintf(path, sizeof path, "%smusic/map.txt", map_dir);
    FILE *f = fopen(path, "r");
    if (!f) return;
    /* `key song.wav` loops the file whole; `key intro.wav loop.wav` plays
     * the intro once and then loops the loop file - the real structure
     * (tools/labs/songcut.py cuts a long render into the pair) */
    char line[400], k[64], v[256], v2[256]; int nf = 0;
    while (fgets(line, sizeof line, f)) {
        nf = sscanf(line, "%63s %255s %255s", k, v, v2);
        if (nf >= 2 && !strcmp(k, key)) break;
        nf = 0;
    }
    fclose(f);
    if (nf < 2) return;
    snprintf(path, sizeof path, "%smusic/%s", map_dir, v);
    cur = Mix_LoadMUS(path);
    if (!cur) return;
    if (nf == 3) {
        snprintf(path, sizeof path, "%smusic/%s", map_dir, v2);
        loop_next = Mix_LoadMUS(path);      /* may be NULL: then it just ends */
    }
    if (getenv("SMK_MUSIC_TRACE")) printf("music: playing %s (%s)\n", v, nf == 3 ? v2 : "whole-file loop");
    Mix_PlayMusic(cur, loop_next ? 1 : -1);
    if (!music_on) Mix_PauseMusic();
}

void smk_audio_pump(void) { /* SDL_mixer feeds itself */ }

/* ---- Sound effects (NOTES 211) --------------------------------------
 *
 * One Mix_Chunk per game sound ID, loaded lazily from rom/sfx/<ID>.wav
 * and cached; the ids are the GAME'S own ($81:F57A's A), so a call site
 * here reads like the ROM's.  Nothing ROM-derived is committed: the wavs
 * live beside the ROM, like the music.
 */
#define SFX_SLOTS 128
static Mix_Chunk *sfx[SFX_SLOTS];
static int8_t     sfx_tried[SFX_SLOTS];
static bool       sfx_on = true;

void smk_sfx_toggle(void) { sfx_on = !sfx_on; }

void smk_sfx_play(int id)
{
    if (!ready || !sfx_on || id < 0 || id >= SFX_SLOTS) return;
    if (!sfx[id]) {
        if (sfx_tried[id]) return;             /* one look per id, then quiet */
        sfx_tried[id] = 1;
        char path[900];
        snprintf(path, sizeof path, "%ssfx/%02X.wav", map_dir, id);
        sfx[id] = Mix_LoadWAV(path);
        if (getenv("SMK_SFX_TRACE"))
            printf("sfx: %s %s\n", path, sfx[id] ? "loaded" : "MISSING");
        if (!sfx[id]) return;
    }
    if (getenv("SMK_SFX_TRACE")) printf("sfx: play $%02X\n", id);
    Mix_PlayChannel(-1, sfx[id], 0);
}

/* Play every captured effect in id order, announcing each - so a person
 * can NAME them by ear (`smk --sfx`).  The ROM's own call sites give the
 * event for a dozen of them (NOTES 211); the rest are for the user. */
int smk_sfx_audition(void)
{
    if (!ready) return 0;
    int played = 0;
    for (int id = 0; id < SFX_SLOTS; id++) {
        char path[900];
        snprintf(path, sizeof path, "%ssfx/%02X.wav", map_dir, id);
        Mix_Chunk *c = Mix_LoadWAV(path);
        if (!c) continue;
        played++;
        int ms = (int)((double)c->alen * 1000.0 / (44100.0 * 4.0));
        printf("  $%02X  %5.2f s  %s\n", id, ms / 1000.0, smk_sfx_name(id));
        fflush(stdout);
        Mix_PlayChannel(-1, c, 0);
        SDL_Delay((Uint32)(ms + 450));
        Mix_FreeChunk(c);
    }
    /* and the ENGINE, which is no file at all: the ROM's rev law driving
     * the measured tone (NOTES 212) - idle, up to the limit, and back */
    printf("  engine  the rev, $01 -> $4F -> $01  (400 Hz -> 984 Hz)\n");
    fflush(stdout);
    for (int v = 1; v <= 0x4F; v++) { smk_engine_set(v); SDL_Delay(28); }
    for (int i = 0; i < 40; i++) { smk_engine_set(0x4F - (i & 7)); SDL_Delay(30); }
    for (int v = 0x4F; v >= 1; v--) { smk_engine_set(v); SDL_Delay(22); }
    smk_engine_off();
    SDL_Delay(400);
    return played;
}

/* what the ROM's own call site says the sound is, where we know it */
const char *smk_sfx_name(int id)
{
    switch (id) {
    case SMK_SFX_HOP:       return "hop / bump          ($80:B555, $80:B68C)";
    case SMK_SFX_MOLE:      return "mole                ($80:B6B9)";
    case SMK_SFX_FALL:      return "the drop            ($80:B66A)";
    case SMK_SFX_FEATHER:   return "feather             ($80:B57B)";
    case SMK_SFX_RESCUE:    return "rescue / hazard     ($80:B20E)";
    case SMK_SFX_WATER:     return "water               ($80:B5BB)";
    case SMK_SFX_LAVA:      return "lava / pit          ($80:B647)";
    case SMK_SFX_SPIN:      return "spin out            ($80:B75A, $80:A9A8)";
    case SMK_SFX_MENU_MOVE: return "menu move           ($85:885E ...)";
    case SMK_SFX_MENU_OK:   return "menu confirm        ($85:853C ...)";
    case SMK_SFX_MENU_BACK: return "menu back           ($85:855F)";
    case SMK_SFX_BOOST:     return "mushroom boost      ($80:B48C)";
    case SMK_SFX_ITEMBOX:   return "item box            ($85:B10F ...)";
    case SMK_SFX_HAZARD:    return "hazard              ($80:B204)";
    case SMK_SFX_COIN:      return "coin                ($80:9B32)";
    case SMK_SFX_LAP:       return "lap                 ($80:A497)";
    case SMK_SFX_START:     return "the lights          ($80:8A2A)";
    default:                return "(unnamed - the ROM has no immediate call site)";
    }
}

/* ---- The engine (NOTES 212) -----------------------------------------
 *
 * The engine is NOT a queued effect: the 65816 hands the driver a
 * parameter every frame - $42's low seven bits, written to APU port 2 at
 * $80:9643 - and the driver holds a tone whose pitch is that parameter.
 * MEASURED, by pinning $42 to a constant with a ROM patch and reading
 * the spectrum: the pitch is LINEAR, f = 392 + 7.5 * v Hz (572/632/692/
 * 752/812/872 Hz for $18/$20/$28/$30/$38/$40), and the tone is nearly
 * pure - the second and third harmonics measure 0.10 and 0.15 of the
 * fundamental, everything above that under 0.04.
 *
 * So the port SYNTHESISES it from that measured profile rather than
 * looping a capture: a single-cycle table stepped at the wanted pitch,
 * which cannot click when the rev moves (LABELLED, S38).  The rev itself
 * is the ROM's own ($80:9543) and lives in main.c.
 */
#define ENG_TAB 512
static float  eng_tab[ENG_TAB];
static double eng_phase, eng_step;
static float  eng_vol, eng_vol_want;
static bool   eng_hooked, eng_built;

static void engine_mix(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    int16_t *out = (int16_t *)stream;
    int frames = len / 4;                       /* stereo, 16-bit */
    for (int i = 0; i < frames; i++) {
        /* the wanted volume is approached, never jumped to: a step in
         * gain is a click, and the rev moves every frame */
        eng_vol += (eng_vol_want - eng_vol) * 0.002f;
        int j = (int)eng_phase;
        float f = (float)(eng_phase - j);
        float a = eng_tab[j & (ENG_TAB - 1)], b = eng_tab[(j + 1) & (ENG_TAB - 1)];
        int16_t s = (int16_t)((a + (b - a) * f) * eng_vol * 32767.0f);
        out[i * 2] = s; out[i * 2 + 1] = s;
        eng_phase += eng_step;
        if (eng_phase >= ENG_TAB) eng_phase -= ENG_TAB;
    }
}

static void engine_build(void)
{
    /* the measured harmonic profile (V = $30 and $40 agreeing) */
    static const float H[6] = { 1.00f, 0.10f, 0.15f, 0.01f, 0.02f, 0.02f };
    float peak = 0.0f;
    for (int i = 0; i < ENG_TAB; i++) {
        double t = 2.0 * M_PI * i / ENG_TAB, s = 0.0;
        for (int h = 0; h < 6; h++) s += H[h] * sin(t * (h + 1) + h * 0.7);
        eng_tab[i] = (float)s;
        if (fabsf(eng_tab[i]) > peak) peak = fabsf(eng_tab[i]);
    }
    if (peak > 0.0f) for (int i = 0; i < ENG_TAB; i++) eng_tab[i] /= peak;
    eng_built = true;
}

void smk_engine_set(int v)
{
    if (!ready) return;
    if (!eng_built) engine_build();
    /* the driver's own silence: $42 = 0 is what $81:A26F writes to stop
     * the sound, and an idle kart sits at 1 */
    if (v <= 1 || music_on) { eng_vol_want = 0.0f; return; }
    double f = 392.0 + 7.5 * (double)v;         /* MEASURED (NOTES 212) */
    eng_step = (double)ENG_TAB * f / 44100.0;
    const char *ev = getenv("SMK_ENGINE_VOL");
    eng_vol_want = ev ? (float)atof(ev) : 0.22f;
    if (!eng_hooked) { Mix_HookMusic(engine_mix, NULL); eng_hooked = true; }
}

void smk_engine_off(void)
{
    eng_vol_want = 0.0f;
}
