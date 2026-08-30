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

static bool     ready;
static Mix_Music *cur, *loop_next;
static char     cur_key[32];
static char     map_dir[512];
static bool     music_on = true;

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
    music_on = !music_on;
    if (!ready) return;
    if (!music_on) Mix_PauseMusic(); else Mix_ResumeMusic();
}

void smk_music_set(const char *key)
{
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
