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
const smk_coin_step SMK_COIN_PATH[] =
#include "coin_path.inc"
;
const int SMK_COIN_PATH_LEN = (int)(sizeof SMK_COIN_PATH / sizeof SMK_COIN_PATH[0]);
const smk_coin_step SMK_COINUP_PATH[] =
#include "coinup_path.inc"
;
const int SMK_COINUP_PATH_LEN = (int)(sizeof SMK_COINUP_PATH / sizeof SMK_COINUP_PATH[0]);

/* the coin you just drove over hops straight up off the road (NOTES 189) */
/* The 2-coin item: "the same as picking up one coin from the floor, but
 * doubled" (the user) - two hops, one either side of the kart and the
 * second a frame late.  Kind 2, not 1, because the captured hop carries
 * the lateral offset of the road coin it was captured from (-9 px): for a
 * pair that has to go, or they land on top of each other, off to the
 * left of the kart. */
void smk_coinfx_pickup2(smk_coin *c, int n)
{
    int got = 0;
    for (int s = 0; s < n && got < 2; s++) if (!c[s].live) {
        memset(&c[s], 0, sizeof c[s]);
        c[s].live = 1; c[s].kind = 2; c[s].side = got ? 1 : -1; c[s].t = -got;
        got++;
    }
}

void smk_coinfx_pickup(smk_coin *c, int n)
{
    for (int s = 0; s < n; s++) if (!c[s].live) {
        memset(&c[s], 0, sizeof c[s]);
        c[s].live = 1; c[s].kind = 1; c[s].side = 1; c[s].t = 0;
        return;
    }
}

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
    (void)heading; (void)kvx; (void)kvy;
    for (int i = 0; i < count; i++) {
        int slot = -1;
        for (int s = 0; s < n; s++) if (!c[s].live) { slot = s; break; }
        if (slot < 0) return;
        smk_coin *k = &c[slot];
        memset(k, 0, sizeof *k);
        k->live = 1;
        k->x = x; k->y = y;
        k->side = (i & 1) ? -1 : 1;                /* fan: alternate sides */
        k->t = -i;                                 /* and stagger by a frame */
    }
}

void smk_coinfx_step(smk_coin *c, int n)
{
    for (int i = 0; i < n; i++) {
        smk_coin *k = &c[i];
        if (!k->live) continue;
        k->t++;
        int end = k->kind ? SMK_COINUP_DELAY + SMK_COINUP_PATH_LEN
                          : SMK_COIN_DELAY + SMK_COIN_PATH_LEN;
        if (k->t >= end) k->live = 0;
    }
}
