/* Coins thrown out of a kart that has been hit.
 *
 * The user: "when passing over one, coins are thrown up and then fall
 * down."  The art is the game's (see smk.h); the arc is ours, ledgered as
 * S29, because the game's own launch constants sit in an effect slot this
 * log could not pin down.
 */
#include "smk.h"
#include <string.h>

/* The shared sprite blob is uploaded to VRAM from tile 48, so tile n's 32
 * bytes are at (n - 48) * 32 in it.  Established by dumping VRAM at a real
 * coin loss and finding those exact bytes in the blob (NOTES 183). */
#define BLOB_FIRST_TILE 48
static const int COIN_TILE[SMK_COIN_FRAMES] = { 0x86, 0xA2, 0x60 };

bool smk_coin_load(const smk_rom *rom, smk_coinart *out)
{
    static uint8_t buf[131072];
    memset(out, 0, sizeof *out);
    long n = smk_decompress_into(rom->data, rom->size,
                                 smk_snes_to_pc(rom, SMK_SHADOW_SRC),
                                 buf, sizeof buf, 0, NULL);
    if (n <= 0) return false;
    for (int f = 0; f < SMK_COIN_FRAMES; f++) {
        /* a 16x16 sprite is tiles t, t+1, t+16, t+17 */
        for (int q = 0; q < 4; q++) {
            int tn = COIN_TILE[f] + (q >> 1) * 16 + (q & 1);
            long off = (long)(tn - BLOB_FIRST_TILE) * 32;
            if (off < 0 || off + 32 > n) return false;
            const uint8_t *src = buf + off;
            for (int pair = 0; pair < 2; pair++) {
                const uint8_t *p = src + pair * 16;
                for (int y = 0; y < 8; y++) {
                    uint8_t lo = p[y * 2], hi = p[y * 2 + 1];
                    for (int x = 0; x < 8; x++) {
                        int bit = 7 - x;
                        int v = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
                        int px = (q & 1) * 8 + x, py = (q >> 1) * 8 + y;
                        out->px[f][py * SMK_COIN_PX + px] |=
                            (uint8_t)(v << (pair * 2));
                    }
                }
            }
        }
    }
    out->ok = true;
    return true;
}

/* OURS (S29): thrown up and back, fanned out, and they bounce once or
 * twice before giving up.  Deterministic - the fan comes from the coin's
 * index, not from a random number, so a replay stays a replay. */
void smk_coinfx_spawn(smk_coin *c, int n, int32_t x, int32_t y,
                      uint16_t heading, int16_t kvx, int16_t kvy, int count)
{
    for (int i = 0; i < count; i++) {
        int slot = -1;
        for (int s = 0; s < n; s++) if (!c[s].live) { slot = s; break; }
        if (slot < 0) return;                     /* all busy: drop it */
        smk_coin *k = &c[slot];
        memset(k, 0, sizeof *k);
        k->live = 1;
        k->x = x; k->y = y; k->z = 0;
        /* FORWARD and fanned, not backwards.
         *
         * The recorded loss shows the coin appearing high on screen
         * (y 67) and coming DOWN toward the camera (y 114) while sliding
         * left - so it is thrown ahead of the kart and the kart then
         * drives past it.  Thrown backwards it lands behind a camera that
         * looks forward, and is never drawn at all, which is exactly what
         * the first version of this did (NOTES 183). */
        /* UP and BACK, and it does NOT ride along with the kart.  The
         * first version added the kart's own velocity, so a coin knocked
         * out of a kart flew above it at the kart's speed for its whole
         * arc - "a coin on its side on top of Mario's head" in every
         * picture (the user).  A coin is left behind: it goes up, drifts
         * back a little, and the kart drives on without it. */
        /* WITH the kart, and up.  The recorded loss (NOTES 183) has the
         * coin holding its place on screen through its arc - it rides
         * along at the kart's speed and only rises and falls - which is
         * also why one sits "on top of Mario's head" in a picture taken
         * eight frames after a bump.  Two other launches were tried and
         * were worse: backward is behind the camera and never drawn;
         * slower-than-the-kart drops below the screen's bottom edge.  What
         * made it look permanent was the BOUNCE, which is gone: a coin
         * lands once and is done. */
        uint16_t a = (uint16_t)(heading + (uint16_t)((i - count / 2) * 0x0C00));
        int16_t sx, cy;
        smk_dsp_sincos(a, 60, &sx, &cy);
        k->vx = (int16_t)(kvx + sx);
        k->vy = (int16_t)(kvy - cy);
        k->vz = (int16_t)(SMK_COIN_RISE - (i & 1) * 40);
    }
}

void smk_coinfx_step(smk_coin *c, int n)
{
    for (int i = 0; i < n; i++) {
        smk_coin *k = &c[i];
        if (!k->live) continue;
        k->x += (int32_t)k->vx << (SMK_POS_SHIFT - 8);
        k->y += (int32_t)k->vy << (SMK_POS_SHIFT - 8);
        k->z += (int32_t)k->vz << 8;
        k->vz = (int16_t)(k->vz - SMK_COIN_GRAV);
        if (k->z <= 0 && k->t > 2) { k->live = 0; continue; }   /* landed: gone */
        if (++k->t > SMK_COIN_LIFE) k->live = 0;
    }
}
