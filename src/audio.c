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

#define SFX_LOOPS 2                 /* the roulette and the skid at once */

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

/* Some sounds are not a game id at all - the countdown's beeps are three
 * key-ons of one voice inside the start passage (NOTES 217) - so they
 * live under a name rather than a number. */
void smk_sfx_play_name(const char *name)
{
    if (!ready || !sfx_on || !name) return;
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
    if (getenv("SMK_SFX_TRACE")) printf("sfx: play %s\n", name);
    Mix_PlayChannel(-1, chunks[slot], 0);
}

/* A sound the game HOLDS rather than fires: the item roulette is one
 * voice keyed once and left running, its pitch stepped through eight
 * notes until the roulette stops (NOTES 220).  Looped on a channel of
 * its own so it can be started and stopped by the state that owns it. */
static struct { char name[24]; Mix_Chunk *chunk; bool on; } loops[SFX_LOOPS];

void smk_sfx_loop(const char *name, bool on)
{
    if (!ready || !sfx_on || !name) return;
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
        if (loops[slot].on) return;
        if (!loops[slot].chunk) {
            char path[900];
            snprintf(path, sizeof path, "%ssfx/%s.wav", map_dir, name);
            loops[slot].chunk = Mix_LoadWAV(path);
            if (getenv("SMK_SFX_TRACE"))
                printf("sfx: %s %s (loop)\n", path, loops[slot].chunk ? "loaded" : "MISSING");
            if (!loops[slot].chunk) return;
        }
        Mix_PlayChannel(slot, loops[slot].chunk, -1);
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
    Mix_PlayChannel(-1, sfx[id], 0);   /* -1 skips reserved channels */
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
    while (!stopped) {
        printf("  engine   the rev, $01 -> $4F -> $01  (SRCN $02 at the"
               " game's own rate)\n");
        fflush(stdout);
        for (int v = 1; v <= 0x4F; v++) { smk_engine_set(v); SDL_Delay(28); }
        for (int i = 0; i < 40; i++) { smk_engine_set(0x4F - (i & 7)); SDL_Delay(30); }
        for (int v = 0x4F; v >= 1; v--) { smk_engine_set(v); SDL_Delay(22); }
        smk_engine_off();
        SDL_Delay(300);
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
    case SMK_SFX_SHELL_BOUNCE: return "a shell bouncing    (you)";
    case SMK_SFX_BRAKE:       return "braking hard        (you)";
    case SMK_SFX_WALL2:       return "the wall, harder    (the $80:D7DA table)";
    case SMK_SFX_BOO_START:   return "Boo, starting       (you)";
    case SMK_SFX_AI_ENGINE:   return "another kart's engine (you)";
    case SMK_SFX_FEATHER2:    return "a feather           (you) - $24 is one too";
    case SMK_SFX_BOOST:       return "mushroom boost      (you)";
    case SMK_SFX_LAND_SOFT:   return "landing on soft ground (the ROM)";
    case SMK_SFX_MENU_SCROLL: return "menu scrolling      (you)";
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
    case 0x2B: return "(unnamed - fires at full speed on the road)";
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

/* ---- The engine (NOTES 212/213) -------------------------------------
 *
 * The engine is NOT a queued effect: the 65816 hands the driver a
 * parameter every frame - $42's low seven bits, written to APU port 2 at
 * $80:9643 - and the driver plays ONE LOOPED SAMPLE at a pitch set by
 * it.  Both halves are now read off the CHIP rather than the speaker
 * (NOTES 213): the engine is DSP voice 7, sample SRCN $02, and its DSP
 * pitch register is exactly
 *
 *     P = $4700 + 34 * v        (masked to the DSP's 14 bits)
 *
 * measured at ten values of v, so the sample plays at
 * ((1792 + 34v) / 4096) * 32000 Hz.  rom/sfx/engine.wav is that sample's
 * own loop, decoded from the game's BRR by tools/labs/brr.py, so the
 * timbre and the pitch are both the game's.  (The first version
 * synthesised a tone from a spectral peak and came out an octave and a
 * half high - the peak was the sample's 9th partial, not its pitch.)
 */
static float  *eng_pcm;             /* the ROM's own loop, -1..1     */
static int     eng_len;
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
} eng[ENG_VOICES];

static void engine_mix(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    int16_t *out = (int16_t *)stream;
    int frames = len / 4;                       /* stereo, 16-bit */
    if (!eng_pcm || eng_len < 2) { memset(stream, 0, (size_t)len); return; }
    for (int i = 0; i < frames; i++) {
        float l = 0.0f, r = 0.0f;
        for (int v = 0; v < ENG_VOICES; v++) {
            /* the wanted volume is approached, never jumped to: a step
             * in gain is a click, and the rev moves every frame */
            eng[v].vol += (eng[v].vol_want - eng[v].vol) * 0.002f;
            eng[v].pan += (eng[v].pan_want - eng[v].pan) * 0.002f;
            if (eng[v].vol < 0.0005f) continue;
            int j = (int)eng[v].phase;
            float f = (float)(eng[v].phase - j);
            float a = eng_pcm[j % eng_len], b = eng_pcm[(j + 1) % eng_len];
            float s = (a + (b - a) * f) * eng[v].vol;
            float p = eng[v].pan;               /* -1 left .. +1 right */
            l += s * (p > 0.0f ? 1.0f - p * 0.8f : 1.0f);
            r += s * (p < 0.0f ? 1.0f + p * 0.8f : 1.0f);
            eng[v].phase += eng[v].step;
            while (eng[v].phase >= eng_len) eng[v].phase -= eng_len;
        }
        if (l > 1.0f) l = 1.0f; if (l < -1.0f) l = -1.0f;
        if (r > 1.0f) r = 1.0f; if (r < -1.0f) r = -1.0f;
        out[i * 2]     = (int16_t)(l * 32767.0f);
        out[i * 2 + 1] = (int16_t)(r * 32767.0f);
    }
}

static void engine_load(void)
{
    eng_tried = true;
    char path[900];
    snprintf(path, sizeof path, "%ssfx/engine.wav", map_dir);
    SDL_AudioSpec spec;
    Uint8 *buf = NULL; Uint32 blen = 0;
    if (!SDL_LoadWAV(path, &spec, &buf, &blen)) {
        if (getenv("SMK_ENGINE_TRACE")) printf("engine: %s missing\n", path);
        return;
    }
    int n = (int)(blen / 2);
    if (spec.format != AUDIO_S16LSB || spec.channels != 1 || n < 2) {
        SDL_FreeWAV(buf);
        if (getenv("SMK_ENGINE_TRACE")) printf("engine: unexpected wav format\n");
        return;
    }
    eng_pcm = malloc((size_t)n * sizeof *eng_pcm);
    if (!eng_pcm) { SDL_FreeWAV(buf); return; }
    const int16_t *src = (const int16_t *)buf;
    for (int i = 0; i < n; i++) eng_pcm[i] = (float)src[i] / 32768.0f;
    eng_len = n;
    SDL_FreeWAV(buf);
    if (getenv("SMK_ENGINE_TRACE")) printf("engine: loaded %d samples\n", n);
}

/* v is the game's own $42; vol and pan place the kart (voice 0 is the
 * player, dead centre and loudest) */
void smk_engine_voice(int voice, int v, float vol, float pan)
{
    if (!ready || voice < 0 || voice >= ENG_VOICES) return;
    if (!eng_tried) engine_load();
    if (!eng_pcm) return;
    if (v <= 0 || vol <= 0.0f || music_on) { eng[voice].vol_want = 0.0f; return; }
    int p = (0x4700 + 34 * v) & 0x3FFF;         /* MEASURED (NOTES 213) */
    eng[voice].step = ((double)p / 4096.0 * 32000.0) / 44100.0;
    eng[voice].vol_want = vol;
    eng[voice].pan_want = pan < -1.0f ? -1.0f : (pan > 1.0f ? 1.0f : pan);
    if (!eng_hooked) {
        Mix_HookMusic(engine_mix, NULL);
        eng_hooked = true;
        if (getenv("SMK_ENGINE_TRACE"))
            printf("engine: hook installed (v %d)\n", v);
    }
}

void smk_engine_set(int v)
{
    const char *ev = getenv("SMK_ENGINE_VOL");
    smk_engine_voice(0, v, ev ? (float)atof(ev) : 0.40f, 0.0f);
}

void smk_engine_off(void)
{
    for (int v = 0; v < ENG_VOICES; v++) eng[v].vol_want = 0.0f;
}
