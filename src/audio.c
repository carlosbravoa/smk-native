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
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#define SFX_LOOPS 8                 /* the roulette, the skid, five surfaces, spare */

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
    Mix_ReserveChannels(SFX_LOOPS);  /* the held sounds get their own */
    /* THE EFFECTS' LEVEL.  They played at the mixer's full volume while
     * the engine sits at smk_engine_base_volume() (0.40 of its sample),
     * so every effect landed a good 8 dB over the bed the game rides on -
     * the user, on the hop: "sounds too loud, like it was not passing
     * through the game's sound engine".
     *
     * MEASURED over the 65 captured effects: the median has an RMS of
     * 3073, the engine samples about 4700.  At 0.40 the engine's own
     * level is ~1880, so 0.75 puts the median effect at ~2300 - just
     * above the engine rather than on top of it.  OURS, labelled, and
     * SMK_SFX_VOL overrides it. */
    {
        const char *v = getenv("SMK_SFX_VOL");
        float f = v ? (float)atof(v) : 0.75f;
        if (f < 0.0f) f = 0.0f;
        if (f > 1.0f) f = 1.0f;
        Mix_Volume(-1, (int)(f * MIX_MAX_VOLUME + 0.5f));
    }
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

/* PAUSE.  Everything stops: the music, every sound effect channel and the
 * held ones with them (the engine, the roulette), because the simulation
 * that would have ended them is not running either.  Idempotent, so the
 * caller can just say what it wants every frame. */
static bool audio_paused;
void smk_audio_pause(bool on)
{
    if (on == audio_paused) return;
    audio_paused = on;
    if (!ready) return;
    if (on) {
        Mix_Pause(-1);
        if (music_on) Mix_PauseMusic();
    } else {
        Mix_Resume(-1);
        if (music_on) Mix_ResumeMusic();
    }
}

/* ---- Sound effects (NOTES 211) --------------------------------------
 *
 * One Mix_Chunk per game sound ID, loaded lazily from rom/sfx/<ID>.wav
 * and cached; the ids are the GAME'S own ($81:F57A's A), so a call site
 * here reads like the ROM's.  Nothing ROM-derived is committed: the wavs
 * live beside the ROM, like the music.
 */
#define SFX_SLOTS 128
static Mix_Chunk *sfx[SFX_SLOTS];
/* WHOSE SIDE a sound is on (NOTES 286).  In a split-screen race the user
 * wants "the whole sound for P1's game on the left speaker, P2's on the
 * right": main.c sets this to -1 while it runs player 1's half of the
 * frame, +1 for player 2's, 0 for the world's own sounds and every
 * one-player race, and every one-shot fired meanwhile is panned there. */
static float sfx_pan;
void smk_sfx_set_pan(float pan) { sfx_pan = pan < -1.0f ? -1.0f : (pan > 1.0f ? 1.0f : pan); }
static void pan_channel(int ch, float pan)
{
    if (ch < 0) return;
    float l = 1.0f - pan, r = 1.0f + pan;
    if (l > 1.0f) l = 1.0f; if (r > 1.0f) r = 1.0f;
    Mix_SetPanning(ch, (Uint8)(l * 255.0f), (Uint8)(r * 255.0f));
}
static int8_t     sfx_tried[SFX_SLOTS];
static bool       sfx_on = true;
static bool       sfx_checked;

void smk_sfx_toggle(void) { sfx_on = !sfx_on; }

/* SMK_SFX_OFF=1 silences the one-shot effects and leaves the engine
 * mixer running - the only way to hear (or measure) the engines alone. */
static bool sfx_enabled(void)
{
    if (!sfx_checked) { sfx_checked = true; if (getenv("SMK_SFX_OFF")) sfx_on = false; }
    return sfx_on;
}

/* Some sounds are not a game id at all - the countdown's beeps are three
 * key-ons of one voice inside the start passage (NOTES 217) - so they
 * live under a name rather than a number. */
void smk_sfx_play_name(const char *name)
{
    if (!ready || !sfx_enabled() || !name) return;
    static char cached[4][24];
    static Mix_Chunk *chunks[4];
    static int8_t tried[4];
    int slot = 0;
    for (; slot < 4; slot++)
        if (!cached[slot][0] || !strcmp(cached[slot], name)) break;
    if (slot == 4) slot = 3;
    if (!cached[slot][0]) snprintf(cached[slot], sizeof cached[slot], "%s", name);
    if (!chunks[slot]) {
        if (tried[slot]) return;
        tried[slot] = 1;
        char path[900];
        snprintf(path, sizeof path, "%ssfx/%s.wav", map_dir, name);
        chunks[slot] = Mix_LoadWAV(path);
        if (getenv("SMK_SFX_TRACE"))
            printf("sfx: %s %s\n", path, chunks[slot] ? "loaded" : "MISSING");
        if (!chunks[slot]) return;
    }
    if (getenv("SMK_SFX_TRACE")) printf("sfx: play %s pan %+.1f\n", name, sfx_pan);
    pan_channel(Mix_PlayChannel(-1, chunks[slot], 0), sfx_pan);
}

/* A sound the game HOLDS rather than fires: the item roulette is one
 * voice keyed once and left running, its pitch stepped through eight
 * notes until the roulette stops (NOTES 220).  Looped on a channel of
 * its own so it can be started and stopped by the state that owns it. */
static struct { char name[24]; Mix_Chunk *chunk; bool on; } loops[SFX_LOOPS];

void smk_sfx_loop(const char *name, bool on) { smk_sfx_loop_pan(name, on, 0.0f); }
/* the same, placed: a held sound both drivers want sits in the middle,
 * one only one of them wants sits on that one's side (NOTES 286) */
void smk_sfx_loop_pan(const char *name, bool on, float pan)
{
    if (!ready || !sfx_enabled() || !name) return;
    int slot = -1, free_slot = -1;
    for (int i = 0; i < SFX_LOOPS; i++) {
        if (loops[i].name[0] && !strcmp(loops[i].name, name)) { slot = i; break; }
        if (!loops[i].name[0] && free_slot < 0) free_slot = i;
    }
    if (slot < 0) {
        if (!on) return;                    /* nothing of that name is running */
        slot = free_slot >= 0 ? free_slot : SFX_LOOPS - 1;
        snprintf(loops[slot].name, sizeof loops[slot].name, "%s", name);
    }
    if (on) {
        if (loops[slot].on) { pan_channel(slot, pan); return; }   /* follow the side */
        if (!loops[slot].chunk) {
            char path[900];
            snprintf(path, sizeof path, "%ssfx/%s.wav", map_dir, name);
            loops[slot].chunk = Mix_LoadWAV(path);
            if (getenv("SMK_SFX_TRACE"))
                printf("sfx: %s %s (loop)\n", path, loops[slot].chunk ? "loaded" : "MISSING");
            if (!loops[slot].chunk) return;
        }
        Mix_PlayChannel(slot, loops[slot].chunk, -1);
        pan_channel(slot, pan);
        loops[slot].on = true;
        if (getenv("SMK_SFX_TRACE")) printf("sfx: loop %s ON\n", name);
    } else if (loops[slot].on) {
        Mix_HaltChannel(slot);
        loops[slot].on = false;
        if (getenv("SMK_SFX_TRACE")) printf("sfx: loop %s off\n", name);
    }
}

void smk_sfx_play(int id)
{
    if (!ready || !sfx_enabled() || id < 0 || id >= SFX_SLOTS) return;
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
    if (getenv("SMK_SFX_TRACE")) printf("sfx: play $%02X pan %+.1f\n", id, sfx_pan);
    pan_channel(Mix_PlayChannel(-1, sfx[id], 0), sfx_pan);   /* -1 skips reserved channels */
}

/* Play every captured effect in id order, announcing each - so a person
 * can NAME them by ear (`smk --sfx`).  The ROM's own call sites give the
 * event for a dozen of them (NOTES 211); the rest are for the user. */
/* Play every captured effect in id order and ASK - the ROM's call sites
 * name a dozen of them and the rest need an ear, and even the named ones
 * are only as good as my reading of the call site.  Answers go to
 * rom/sfx/names.txt (SMK_SFX_NAMES overrides), which is the file to hand
 * back: it is the naming, in the user's own words.
 *
 * ENTER = the label is right, i = wrong, r = play it again, s = skip,
 * q = stop here, anything else is a comment (and counts as a correction).
 */
static void audition_write(const char *path, const char *lines, int n)
{
    FILE *f = fopen(path, "w");
    if (!f) { printf("  (could not write %s)\n", path); return; }
    fprintf(f, "# The game's sound effects, as heard.  Written by `smk --sfx`.\n"
               "# id  verdict     what it is\n");
    fwrite(lines, 1, strlen(lines), f);
    fclose(f);
    printf("\n  %d answered -> %s\n", n, path);
}

#define ENG_KINDS 4
static const struct {
    unsigned srcn;
    int      base, slope;
    const char *who;
} eng_law[ENG_KINDS] = {
    { 0x02, 0x4700, 34, "Mario, Luigi" },
    { 0x03, 0x4800, 38, "Bowser, DK"   },
    { 0x17, 0x4600, 19, "Peach, Toad"  },
    { 0x18, 0x4600, 29, "Yoshi, Koopa" },
};
/* SMK_DRIVERS order: Mario Luigi Bowser Peach DK Yoshi Koopa Toad */
static const unsigned char eng_kind_of[SMK_CHARACTERS] = { 0, 0, 1, 2, 1, 3, 3, 2 };

int smk_sfx_audition(void)
{
    if (!ready) return 0;
    bool ask = isatty(fileno(stdin)) || getenv("SMK_SFX_ASK");
    char answers[16000];
    size_t used = 0;
    answers[0] = 0;
    const char *out = getenv("SMK_SFX_NAMES");
    char outpath[900], dirpath[900];
    snprintf(dirpath, sizeof dirpath, "%ssfx", map_dir);
    if (out) snprintf(outpath, sizeof outpath, "%s", out);
    else     snprintf(outpath, sizeof outpath, "%s/names.txt", dirpath);
    /* The directory, not a loop over ids: some sounds are SEVERAL ids
     * the game fires together and those live under a name like
     * "3C+3F.wav" (NOTES 215) - the user: "some sounds are just a part
     * of the whole sound". */
    char names[256][32];
    int n = 0;
    DIR *d = opendir(dirpath);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) && n < 256) {
            size_t l = strlen(e->d_name);
            if (l < 5 || l > 30 || strcmp(e->d_name + l - 4, ".wav")) continue;
            if (!strncmp(e->d_name, "engine", 6)) continue;
            snprintf(names[n], sizeof names[n], "%.*s", (int)(l - 4), e->d_name);
            n++;
        }
        closedir(d);
    }
    for (int i = 1; i < n; i++) {            /* plain ids first, then pairs */
        char t[32]; snprintf(t, sizeof t, "%s", names[i]);
        int j = i - 1;
        while (j >= 0 && ((strchr(names[j], '+') != NULL) != (strchr(t, '+') != NULL)
                          ? (strchr(names[j], '+') != NULL)
                          : strcmp(names[j], t) > 0)) {
            snprintf(names[j + 1], sizeof names[j + 1], "%s", names[j]); j--;
        }
        snprintf(names[j + 1], sizeof names[j + 1], "%s", t);
    }
    int played = 0, answered = 0;
    bool stopped = false;
    if (ask)
        printf("\n  c = right   w = wrong   ENTER = skip   r = again   q = stop\n"
               "  ...or just type what it really is.\n"
               "  (only the ones nobody has named yet; SMK_SFX_ALL=1 for all)\n\n");
    /* By default the audition offers only what NOBODY has named yet: a
     * second pass through sounds already assigned is wasted listening
     * (the user).  SMK_SFX_ALL=1 plays everything again. */
    bool all = getenv("SMK_SFX_ALL") != NULL;
    for (int i = 0; i < n && !stopped; i++) {
        char path[900];
        snprintf(path, sizeof path, "%s/%s.wav", dirpath, names[i]);
        if (!all) {
            bool hex = strlen(names[i]) == 2
                    && isxdigit((unsigned char)names[i][0])
                    && isxdigit((unsigned char)names[i][1]);
            if (!hex) continue;                       /* roulette, the beeps */
            if (smk_sfx_name((int)strtol(names[i], NULL, 16))[0] != '(')
                continue;                             /* already named       */
        }
        Mix_Chunk *c = Mix_LoadWAV(path);
        if (!c) continue;
        played++;
        int ms = (int)((double)c->alen * 1000.0 / (44100.0 * 4.0));
        bool pair = strchr(names[i], '+') != NULL;
        int id = (int)strtol(names[i], NULL, 16);
        const char *label = pair ? "the two together, as the game fires them"
                                 : smk_sfx_name(id);
        for (;;) {
            printf("  $%-7s %5.2f s  %s\n", names[i], ms / 1000.0, label);
            fflush(stdout);
            Mix_PlayChannel(-1, c, 0);
            if (!ask) { SDL_Delay((Uint32)(ms + 450)); break; }
            printf("      > "); fflush(stdout);
            char line[512];
            if (!fgets(line, sizeof line, stdin)) { ask = false; break; }
            char *p = line;
            while (*p == ' ' || *p == '\t') p++;
            size_t l = strlen(p);
            while (l && (p[l - 1] == '\n' || p[l - 1] == '\r' || p[l - 1] == ' ')) p[--l] = 0;
            if (!strcmp(p, "r")) continue;
            if (!l || !strcmp(p, "s")) break;              /* ENTER: no answer */
            if (!strcmp(p, "q")) { stopped = true; break; }
            bool named = !pair && smk_sfx_name(id)[0] != '(';
            const char *verdict, *note = label;
            if (!strcmp(p, "c"))      { verdict = named ? "correct" : "unknown";
                                        if (!named) note = "(right, but still unnamed)"; }
            else if (!strcmp(p, "w")) { verdict = "WRONG"; note = "(no name given)"; }
            else                      { verdict = named ? "WRONG" : "named"; note = p; }
            used += (size_t)snprintf(answers + used, sizeof answers - used,
                                     "$%-7s %5.2f s  %-9s  %s\n",
                                     names[i], ms / 1000.0, verdict, note);
            answered++;
            break;
        }
        Mix_FreeChunk(c);
        if (used > sizeof answers - 600) break;
    }
    /* and the ENGINE, which is no file of notes at all: the ROM's rev law
     * driving the game's own looped sample (NOTES 213) */
    /* all FOUR of them: the driver pairs do not share an engine
     * (NOTES 234), so the audition sweeps each one's own sample and law */
    static const int eng_demo[ENG_KINDS] = { 0, 2, 3, 5 };  /* Mario Bowser Peach Yoshi */
    while (!stopped) {
        for (int k = 0; k < ENG_KINDS && !stopped; k++) {
            int chr = eng_demo[k];
            printf("  engine   %-12s  the rev $01 -> $4F -> $01"
                   "  (SRCN $%02X, $%04X + %d*v)\n",
                   eng_law[k].who, eng_law[k].srcn, eng_law[k].base, eng_law[k].slope);
            fflush(stdout);
            for (int v = 1; v <= 0x4F; v++) { smk_engine_set(chr, v); SDL_Delay(24); }
            for (int i = 0; i < 30; i++) { smk_engine_set(chr, 0x4F - (i & 7)); SDL_Delay(28); }
            for (int v = 0x4F; v >= 1; v--) { smk_engine_set(chr, v); SDL_Delay(18); }
            smk_engine_off();
            SDL_Delay(400);
        }
        if (!ask) break;
        printf("      > "); fflush(stdout);
        char line[512];
        if (!fgets(line, sizeof line, stdin)) break;
        char *p = line;
        size_t l = strlen(p);
        while (l && (p[l - 1] == '\n' || p[l - 1] == '\r' || p[l - 1] == ' ')) p[--l] = 0;
        if (!strcmp(p, "r")) continue;
        if (!l || !strcmp(p, "s") || !strcmp(p, "q")) break;
        used += (size_t)snprintf(answers + used, sizeof answers - used,
                                 "engine   %-9s  %s\n",
                                 strcmp(p, "c") ? "WRONG" : "correct",
                                 strcmp(p, "c") ? p : "the rev and its pitch");
        answered++;
        break;
    }
    if (ask && answered) audition_write(outpath, answers, answered);
    return played;
}

/* What the sound IS, and on whose word (NOTES 214).  MEASURED beats
 * USER beats ROM: a call site's address only says where the code plays
 * it, and half of those readings turned out wrong once the ids were
 * watched firing against the game's own state. */
const char *smk_sfx_name(int id)
{
    switch (id) {
    case SMK_SFX_COIN:        return "coin (measured; you did not recognise it alone)";
    case SMK_SFX_HOP:         return "hop (measured; you did not recognise it alone)";
    case SMK_SFX_MOLE:        return "mole                (measured)";
    case SMK_SFX_RAMP:        return "ramp / going up     (measured)";
    case SMK_SFX_FEATHER:     return "feather             (you)";
    case SMK_SFX_LAND:        return "landing             (measured)";
    case SMK_SFX_FALL:        return "falling off the road (you)";
    case SMK_SFX_SPIN_OUT:    return "being spun out      ($80:B75A)";
    case SMK_SFX_SHELL_HIT:   return "hitting with a shell (you)";
    case SMK_SFX_THROW:       return "a shell thrown ahead ($80:F442)";
    case SMK_SFX_DROP:        return "an item left behind ($84:D8F2)";
    case SMK_SFX_AI_HIT:      return "an AI kart takes a hit (you)";
    case SMK_SFX_BOO:         return "Boo                 (you)";
    case SMK_SFX_MENU_MOVE:   return "menu move           (you)";
    case SMK_SFX_MENU_OK:     return "menu confirm        (you)";
    case SMK_SFX_MENU_BACK:   return "menu back           (you)";
    case SMK_SFX_WALL:        return "scraping a wall     (forced in the oracle)";
    case SMK_SFX_BUMP_HARD:   return "a hard kart bump    (forced; you: barrier)";
    case SMK_SFX_BUMP_SOFT:   return "a soft kart bump    (forced in the oracle)";
    case SMK_SFX_BRAKE:       return "braking hard        (you)";
    case SMK_SFX_WALL2:       return "the wall, harder    (the $80:D7DA table)";
    case SMK_SFX_BOO_START:   return "Boo, starting       (you)";
    case SMK_SFX_AI_ENGINE:   return "another kart's engine (you)";
    case SMK_SFX_OBJ_WALL:    return "a shell off a wall  ($84:D73A, near)";
    case SMK_SFX_OBJ_WALL_2:  return "a shell off a wall  ($84:D73A, middle)";
    case SMK_SFX_OBJ_WALL_3:  return "a shell off a wall  ($84:D73A, far)";
    case 0x4D: return "overtaking      ($84:D99B: Mario/Luigi/Peach/Yoshi/Koopa)";
    case 0x50: return "Toad passing    ($84:D99B/$84:D9CA)";
    case 0x51: return "Bowser passing  ($84:D99B/$84:D9CA)";
    case 0x5C: return "DK Jr passing   ($84:D99B/$84:D9CA)";
    case SMK_SFX_FEATHER2:    return "a feather           (you) - $24 is one too";
    case SMK_SFX_BOOST:       return "mushroom boost      (you)";
    case SMK_SFX_LAND_SOFT:   return "landing on soft ground (the ROM)";
    case SMK_SFX_MENU_SCROLL: return "a menu click        ($84:D986, uncaptured)";
    case SMK_SFX_JUMP_BIG:    return "a bump taken boosting (the ROM)";
    case SMK_SFX_ITEMBOX:     return "the item picked from the roulette (you)";
    case SMK_SFX_SHRINK:      return "poison mushroom, shrinking (you)";
    case SMK_SFX_GROW:        return "back to full size   (you)";
    case SMK_SFX_AI_FELL:     return "an AI kart fell on something (you)";
    case SMK_SFX_FINISH:      return "the finish          (you)";
    default:                  return smk_sfx_hint(id);
    }
}

/* For the ids nobody has named: WHEN the game fires them, straight from
 * the recordings (tools/labs/mame/sfxevent.lua).  A 60 ms blip means
 * nothing on its own - "you heard this one going into a wall" is the
 * hint that makes it placeable. */
const char *smk_sfx_hint(int id)
{
    /* Several of these are not whole sounds: $80:D7DA is a table of
     * $3F/$40/$41 chosen by ($AE & 7), and bank $84's stub list is full
     * of triples ($30/$31/$32, $36/$37/$38, $3C/$3D/$3E, $44/$45/$46,
     * $58/$59/$5A).  One event at three intensities - so a member on
     * its own is meant to sound like part of something (NOTES 228). */
    switch (id) {
    case 0x30: case 0x31: case 0x32:
    case 0x33: case 0x34: case 0x35:
    case 0x36: case 0x37: case 0x38:
    case 0x3D: case 0x3E:
    case 0x41: case 0x44: case 0x45: case 0x46:
    case 0x58: case 0x59: case 0x5A:
        return "(one of a TRIPLE - the same event at three intensities)";
    case 0x26: return "(unnamed - fires standing still)";
    case 0x28: return "(unnamed - the ROM plays it in the lava/pit handler)";
    case 0x39: return "(unnamed - fires at speed on the road)";
    case 0x49: return "(you thought a skid - the road skid is a held voice, so this is another)";
    case 0x4A: return "(unnamed - the ROM plays it from bank $85's object code)";
    case 0x4B: return "(unnamed - the ROM plays it from bank $85's object code)";
    case 0x4F: return "(unnamed - once, at speed on the road)";
    case 0x50: return "(HALF a sound - you: yoshi passing, or firing an object)";
    case 0x51: return "(HALF a sound - you: Bowser)";
    case 0x53: return "(unnamed - fires at speed, on and off the road)";
    case 0x54: return "(a SOFT kart bump - $80:D825, forced in the oracle)";
    case 0x57: return "(unnamed - the ROM's other Boo id)";
    case 0x5E: return "(unnamed - never seen fired anywhere)";
    case 0x5B: return "(one of $58's family - leaving an item)";
    case 0x5C: return "(you: another kart's engine - the port now mixes four)";
    case 0x61: return "(you: Mario/Luigi invincibility, maybe the wrong pitch)";
    case 0x64: return "(unnamed - the ROM plays it from bank $85's object code)";
    default:   return "(unnamed - never seen fired, and nobody has named it)";
    }
}

/* ---- The engine (NOTES 212/213/234) ---------------------------------
 *
 * The engine is NOT a queued effect: the 65816 hands the driver a
 * parameter every frame - $42's low seven bits, written to APU port 2 at
 * $80:9643 - and the driver plays ONE LOOPED SAMPLE at a pitch set by
 * it.  Both halves are read off the CHIP rather than the speaker
 * (NOTES 213): the engine is DSP voice 7 and its pitch register is a
 * straight line in that parameter.
 *
 * And there is not ONE engine.  The user: "every pair of characters has
 * their own engine sound".  Holding $1012 (the player's character, kept
 * doubled) at each of the eight drivers with the rev word pinned, and
 * reading voice 7 back, gives four samples and four laws (NOTES 234):
 *
 *     Mario, Luigi   SRCN $02   P = $4700 + 34 * v
 *     Bowser, DK     SRCN $03   P = $4800 + 38 * v
 *     Yoshi, Koopa   SRCN $18   P = $4600 + 29 * v
 *     Peach, Toad    SRCN $17   P = $4600 + 19 * v
 *
 * Eight rev values each, dead linear, no scatter - and Peach's whole
 * 1,400-frame in-play trace is identical to Toad's, frame for frame.
 * Note the ENGINE pairs are not the STAT pairs: Peach rides with Toad
 * here, not with Yoshi.  rom/sfx/engine<SRCN>.wav is each sample's own
 * loop region, decoded from the game's BRR by tools/labs/enginesample.py
 * and left RAW - $17 is the quietest of the four in the ROM too, and
 * that is the game's decision, not ours.  (The first version of all this
 * synthesised a tone from a spectral peak and came out an octave and a
 * half high - the peak was the sample's 9th partial, not its pitch.) */

static float  *eng_pcm[ENG_KINDS];  /* each pair's own loop, -1..1   */
static int     eng_len[ENG_KINDS];
static bool    eng_hooked, eng_tried;

/* One voice per kart (NOTES 229).  The user, naming $5C and $62: "it is
 * the engine of another player" - the game gives a NEARBY kart its own
 * engine, and the port had exactly one.  Voice 0 is the player's; the
 * rest are the closest AI karts, pitched by their own speed, panned by
 * where they are and quieter with distance. */
#define ENG_VOICES 4
static struct {
    double phase, step;
    float  vol, vol_want, pan, pan_want;
    int    kind;                    /* which pair's sample this voice plays */
} eng[ENG_VOICES];

/* THE SPIN (NOTES 293).  Through a spin the game keys the driver's sample
 * $00 on a voice it takes from the music and walks its PITCH register on
 * a triangle - up 219 a frame, down 293 - for 48 frames, from ten frames
 * into the spin, at volume 31 against the engine's 20.  It never passes
 * through the sound queue, which is why no capture of the queue ever
 * held it.  MEASURED per frame off the DSP in the user's spin1
 * recording (four slide spins, the same pattern each time).  The sample
 * is the game's own BRR (tools/labs/spinsample.py), a 64-sample lead-in
 * and a loop. */
#define SPIN_LOOP_FROM 64
static float *spin_pcm; static int spin_len; static bool spin_tried;
static struct { double phase, step; float vol, pan; bool on, keyed; } spin[2];
static void spin_load(void)
{
    spin_tried = true;
    char path[900]; SDL_AudioSpec spec; Uint8 *buf = NULL; Uint32 blen = 0;
    snprintf(path, sizeof path, "%ssfx/spin00.wav", map_dir);
    if (!SDL_LoadWAV(path, &spec, &buf, &blen)) { if (getenv("SMK_SFX_TRACE")) printf("spin: %s missing\n", path); return; }
    int n = (int)(blen / 2);
    if (spec.format != AUDIO_S16LSB || spec.channels != 1 || n <= SPIN_LOOP_FROM + 2) { SDL_FreeWAV(buf); return; }
    spin_pcm = malloc((size_t)n * sizeof *spin_pcm);
    if (!spin_pcm) { SDL_FreeWAV(buf); return; }
    const int16_t *src = (const int16_t *)buf;
    for (int i = 0; i < n; i++) spin_pcm[i] = (float)src[i] / 32768.0f;
    spin_len = n; SDL_FreeWAV(buf);
}
void smk_spin_voice(int view, bool on, int pitch14, float vol, float pan)
{
    if (view < 0 || view > 1) return;
    if (!spin_tried) spin_load();
    if (on && !spin[view].on) { spin[view].phase = 0.0; spin[view].keyed = true; }
    spin[view].on = on;
    spin[view].step = ((double)(pitch14 & 0x3FFF) / 4096.0 * 32000.0) / 44100.0;
    spin[view].vol = on ? vol : 0.0f;
    spin[view].pan = pan < -1.0f ? -1.0f : (pan > 1.0f ? 1.0f : pan);
}

static void engine_mix(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    int16_t *out = (int16_t *)stream;
    int frames = len / 4;                       /* stereo, 16-bit */
    memset(stream, 0, (size_t)len);
    for (int i = 0; i < frames; i++) {
        float l = 0.0f, r = 0.0f;
        for (int v = 0; v < 2; v++) {           /* the spins, one per view */
            if (!spin[v].on || !spin_pcm || spin[v].vol <= 0.0f) continue;
            int j = (int)spin[v].phase;
            if (j >= spin_len - 1) { spin[v].phase -= (double)(spin_len - SPIN_LOOP_FROM); j = (int)spin[v].phase; if (j < 0) j = 0; }
            float f = (float)(spin[v].phase - j);
            float a = spin_pcm[j], b = spin_pcm[j + 1 < spin_len ? j + 1 : SPIN_LOOP_FROM];
            float smp = (a + (b - a) * f) * spin[v].vol;
            float p = spin[v].pan;
            l += smp * (p > 0.0f ? 1.0f - p : 1.0f);
            r += smp * (p < 0.0f ? 1.0f + p : 1.0f);
            spin[v].phase += spin[v].step;
        }
        for (int v = 0; v < ENG_VOICES; v++) {
            /* the wanted volume is approached, never jumped to: a step
             * in gain is a click, and the rev moves every frame */
            eng[v].vol += (eng[v].vol_want - eng[v].vol) * 0.002f;
            eng[v].pan += (eng[v].pan_want - eng[v].pan) * 0.002f;
            if (eng[v].vol < 0.0005f) continue;
            const float *pcm = eng_pcm[eng[v].kind];
            int elen = eng_len[eng[v].kind];
            if (!pcm || elen < 2) continue;
            int j = (int)eng[v].phase;
            float f = (float)(eng[v].phase - j);
            float a = pcm[j % elen], b = pcm[(j + 1) % elen];
            float s = (a + (b - a) * f) * eng[v].vol;
            float p = eng[v].pan;               /* -1 left .. +1 right: at the ends, that side ONLY (NOTES 286) */
            l += s * (p > 0.0f ? 1.0f - p : 1.0f);
            r += s * (p < 0.0f ? 1.0f + p : 1.0f);
            eng[v].phase += eng[v].step;
            while (eng[v].phase >= elen) eng[v].phase -= elen;
        }
        if (l > 1.0f) l = 1.0f; if (l < -1.0f) l = -1.0f;
        if (r > 1.0f) r = 1.0f; if (r < -1.0f) r = -1.0f;
        out[i * 2]     = (int16_t)(l * 32767.0f);
        out[i * 2 + 1] = (int16_t)(r * 32767.0f);
        /* SMK_ENGINE_WAV=path - the engine mix alone, raw s16le stereo at
         * 44100, with a side log of the player's voice step per callback,
         * so its pitch can be measured against the game's (NOTES 291) */
        {
            static FILE *dump = NULL, *dlog = NULL; static long written = 0; static int tried = 0;
            if (!tried) {
                tried = 1;
                const char *e = getenv("SMK_ENGINE_WAV");
                if (e) { char lp[600]; dump = fopen(e, "wb"); snprintf(lp, sizeof lp, "%s.log", e); dlog = fopen(lp, "w"); }
            }
            if (dump) {
                fwrite(&out[i * 2], sizeof(int16_t), 2, dump);
                if (i == 0 && dlog) { fprintf(dlog, "%ld %.6f %.4f\n", written, eng[0].step, eng[0].vol); }
                written++;
                if (i == frames - 1) { fflush(dump); fflush(dlog); }
            }
        }
    }
}

/* One file per pair; a driver whose file is missing falls back to
 * engine.wav, which IS SRCN $02 byte for byte (the port's old single
 * engine was Mario's all along). */
static bool engine_load_one(int kind)
{
    char path[900];
    SDL_AudioSpec spec;
    Uint8 *buf = NULL; Uint32 blen = 0;
    snprintf(path, sizeof path, "%ssfx/engine%02X.wav", map_dir, eng_law[kind].srcn);
    if (!SDL_LoadWAV(path, &spec, &buf, &blen)) {
        snprintf(path, sizeof path, "%ssfx/engine.wav", map_dir);
        if (!SDL_LoadWAV(path, &spec, &buf, &blen)) {
            if (getenv("SMK_ENGINE_TRACE"))
                printf("engine: SRCN $%02X missing (%s)\n", eng_law[kind].srcn, path);
            return false;
        }
    }
    int n = (int)(blen / 2);
    if (spec.format != AUDIO_S16LSB || spec.channels != 1 || n < 2) {
        SDL_FreeWAV(buf);
        if (getenv("SMK_ENGINE_TRACE")) printf("engine: unexpected wav format\n");
        return false;
    }
    eng_pcm[kind] = malloc((size_t)n * sizeof *eng_pcm[kind]);
    if (!eng_pcm[kind]) { SDL_FreeWAV(buf); return false; }
    const int16_t *src = (const int16_t *)buf;
    for (int i = 0; i < n; i++) eng_pcm[kind][i] = (float)src[i] / 32768.0f;
    eng_len[kind] = n;
    SDL_FreeWAV(buf);
    if (getenv("SMK_ENGINE_TRACE"))
        printf("engine: SRCN $%02X (%s) %d samples\n",
               eng_law[kind].srcn, eng_law[kind].who, n);
    return true;
}

static void engine_load(void)
{
    eng_tried = true;
    for (int k = 0; k < ENG_KINDS; k++) engine_load_one(k);
}

/* chr is an SMK_DRIVERS index - it picks the sample AND the law; v is
 * the game's own $42; vol and pan place the kart (voice 0 is the
 * player, dead centre and loudest) */
/* the note each voice was last HANDED, before any of the reasons it might
 * not sound - so a trace, and the gate in `make check`, read what the
 * caller asked for and not what the caller computed (NOTES 284) */
static int eng_note_sent[ENG_VOICES];
int smk_engine_note_sent(int voice)
{
    return (voice >= 0 && voice < ENG_VOICES) ? eng_note_sent[voice] : 0;
}
void smk_engine_voice(int voice, int chr, int v, float vol, float pan)
{
    if (voice >= 0 && voice < ENG_VOICES) eng_note_sent[voice] = v;
    if (!ready || voice < 0 || voice >= ENG_VOICES) return;
    if (!eng_tried) engine_load();
    int kind = (chr >= 0 && chr < SMK_CHARACTERS) ? eng_kind_of[chr] : 0;
    if (!eng_pcm[kind]) return;
    if (v <= 0 || vol <= 0.0f || music_on) { eng[voice].vol_want = 0.0f; return; }
    if (eng[voice].kind != kind) {              /* a different driver here */
        /* the nearest-three set changes hands often, so wrap the phase
         * into the new sample rather than jumping it to zero: a jump is
         * a click, and the timbre is already changing underneath it */
        eng[voice].kind = kind;
        if (eng_len[kind] > 0)
            eng[voice].phase = fmod(eng[voice].phase, (double)eng_len[kind]);
    }
    int p = (eng_law[kind].base + eng_law[kind].slope * v) & 0x3FFF;  /* NOTES 234 */
    eng[voice].step = ((double)p / 4096.0 * 32000.0) / 44100.0;
    eng[voice].vol_want = vol;
    eng[voice].pan_want = pan < -1.0f ? -1.0f : (pan > 1.0f ? 1.0f : pan);
    if (!eng_hooked) {
        Mix_HookMusic(engine_mix, NULL);
        eng_hooked = true;
        if (getenv("SMK_ENGINE_TRACE"))
            printf("engine: hook installed (v %d)\n", v);
    }
    if (getenv("SMK_ENGINE_TRACE")) {
        static unsigned n[ENG_VOICES];
        if ((n[voice]++ % 240) == 0)
            printf("engine: voice %d kind %d (%s) rev %d vol %.2f pan %+.2f\n",
                   voice, kind, eng_law[kind].who, v, vol, pan);
    }
}

float smk_engine_base_volume(void)
{
    const char *ev = getenv("SMK_ENGINE_VOL");
    return ev ? (float)atof(ev) : 0.40f;
}

void smk_engine_set(int chr, int v)
{
    smk_engine_voice(0, chr, v, smk_engine_base_volume(), 0.0f);
}

void smk_engine_off(void)
{
    for (int v = 0; v < ENG_VOICES; v++) eng[v].vol_want = 0.0f;
}
