/* Music playback (P7, NOTES 201/202).
 *
 * The songs are PRE-RECORDED from the game's own sound driver: a MAME
 * replay is snapshotted mid-song into an .spc (tools/labs/mame/spcdump.lua)
 * and libgme renders it to WAV.  DECIDED with the user (ROADMAP P7): no
 * SPC700 runs in the shipped game.
 *
 * The mapping from game state to file is the user's: music/map.txt next
 * to the ROM, one `key file.wav` per line.  Keys the port asks for:
 *   title menu results standings theme0..theme7 win
 * A missing map, key or file is silence, never an error.  The WAV loops
 * whole; SDL_QueueAudio is pumped once a frame from the main loop.
 */
#include "smk.h"
#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static SDL_AudioDeviceID adev;
static SDL_AudioSpec     aspec;
static uint8_t *cur_buf; static uint32_t cur_len, cur_pos;
static char cur_key[32];
static char map_dir[512];
static bool music_on = true;

bool smk_audio_init(void)
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return false;
    SDL_AudioSpec want; memset(&want, 0, sizeof want);
    want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 2048;
    adev = SDL_OpenAudioDevice(NULL, 0, &want, &aspec, 0);
    if (!adev) return false;
    SDL_PauseAudioDevice(adev, 0);
    return true;
}

void smk_audio_set_dir(const char *rom_path)
{
    /* music/ lives beside the ROM */
    snprintf(map_dir, sizeof map_dir, "%s", rom_path);
    char *slash = strrchr(map_dir, '/');
    if (slash) slash[1] = 0; else map_dir[0] = 0;
}

void smk_music_toggle(void) { music_on = !music_on; if (adev) SDL_ClearQueuedAudio(adev); }

static bool load_key(const char *key)
{
    char path[900]; snprintf(path, sizeof path, "%smusic/map.txt", map_dir);
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char k[64], v[256]; bool got = false;
    while (fscanf(f, "%63s %255s", k, v) == 2)
        if (!strcmp(k, key)) { got = true; break; }
    fclose(f);
    if (!got) return false;
    snprintf(path, sizeof path, "%smusic/%s", map_dir, v);
    if (getenv("SMK_MUSIC_TRACE")) printf("music: %s -> %s\n", key, path);
    SDL_AudioSpec ws; uint8_t *wb; uint32_t wl;
    if (!SDL_LoadWAV(path, &ws, &wb, &wl)) return false;
    SDL_AudioCVT cvt;
    if (SDL_BuildAudioCVT(&cvt, ws.format, ws.channels, ws.freq,
                          aspec.format, aspec.channels, aspec.freq) < 0) { SDL_FreeWAV(wb); return false; }
    uint8_t *out = NULL; uint32_t outlen = 0;
    if (cvt.needed) {
        cvt.len = (int)wl;
        cvt.buf = malloc((size_t)wl * (size_t)cvt.len_mult);
        if (!cvt.buf) { SDL_FreeWAV(wb); return false; }
        memcpy(cvt.buf, wb, wl);
        SDL_ConvertAudio(&cvt);
        out = cvt.buf; outlen = (uint32_t)cvt.len_cvt;
    } else {
        out = malloc(wl);
        if (!out) { SDL_FreeWAV(wb); return false; }
        memcpy(out, wb, wl); outlen = wl;
    }
    SDL_FreeWAV(wb);
    free(cur_buf);
    cur_buf = out; cur_len = outlen; cur_pos = 0;
    return true;
}

void smk_music_set(const char *key)
{
    if (!adev || !key) return;
    if (!strcmp(key, cur_key)) return;
    if (getenv("SMK_MUSIC_TRACE")) printf("music: set %s\n", key);
    snprintf(cur_key, sizeof cur_key, "%s", key);
    SDL_ClearQueuedAudio(adev);
    if (cur_buf) { free(cur_buf); cur_buf = NULL; }
    cur_len = cur_pos = 0;
    load_key(key);
}

void smk_audio_pump(void)
{
    if (!adev || !cur_buf || !cur_len || !music_on) return;
    /* keep about half a second queued */
    const uint32_t want = (uint32_t)aspec.freq * aspec.channels * 2 / 2;
    while (SDL_GetQueuedAudioSize(adev) < want) {
        uint32_t n = cur_len - cur_pos;
        if (n > 16384) n = 16384;
        SDL_QueueAudio(adev, cur_buf + cur_pos, n);
        cur_pos += n;
        if (cur_pos >= cur_len) cur_pos = 0;       /* the loop */
    }
}
