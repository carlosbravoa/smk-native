/* The start: Lakitu, his traffic light, and the 336 frames they run for.
 *
 * The timing half was already measured (NOTES 145): $809FE1 loads $0146
 * with -336, $80A1F8 counts it up one a frame, and the field is released
 * on the frame it reaches 0.  What was missing - and what NOTES 145a
 * left as the next step for whoever picked it up - is everything the
 * player SEES, because MAME exposes neither OAM nor VRAM to Lua.
 *
 * The Python oracle does.  tools/labs/lakitu.py boots a race and records
 * the whole 544-byte OAM every frame of the countdown, which turns the
 * question into a reading:
 *
 *   OAM 11-14   Lakitu, four 16x16 sprites in a 32x32 block at a FIXED
 *               screen x of 36, every one H-flipped, palette 5 ($D0).
 *               Tiles $42/$40 over $46/$44; the two left quadrants
 *               become $4A/$4C when he cheers.
 *   OAM 8-10    the light, three 8x8 sprites at x 63 hanging 16, 24 and
 *               32 px below his block's top, palette 4 ($C0).  Two red
 *               lamps over one green: $FD/$FE red off/on, $FB/$FC green
 *               off/on.
 *
 * and the sequence, in frames from the arm:
 *
 *      1   he drops in from y = -48
 *    113   settled at y = 5, having overshot to 7 and come back
 *    179   the first red
 *    244   the second red
 *    309   the green, and he changes to the cheering pose
 *    336   THE FIELD IS RELEASED
 *    377   he starts climbing back out
 *    439   parked at y = -40, clear of the screen
 *
 * The 27 frames between the green and the release are the game's, not a
 * rounding: the AI field and $3A (4 -> 6) both move on 336, and NOTES
 * 145's human runs first move on 339.  So the green is an anticipation
 * cue and not the release itself.
 *
 * MEASURED, NOT DERIVED.  The trajectory below is the one the game
 * produced, frame by frame.  Its generator is not decoded: the only
 * WRAM word that tracked the sprite turned out to be the OAM shadow
 * buffer at $0220, so there was nothing to read.  That is the same
 * choice NOTES 152 made for the movers - port the cycle, not the machine
 * that makes it - and tools/labs/lakitu_full.py re-derives the fixture
 * so the next reader can check it rather than trust it.
 */
#include "smk.h"
#include <string.h>

/* Frames at which he changes rows, as (frame, y).  y is the top of his
 * 32x32 block in SNES pixels; negative is above the screen.  Generated
 * from the capture - do not hand-edit; re-run the lab. */
static const struct { short t; short y; } TRACK[] = {
#include "lakitu_track.inc"
};
#define TRACK_N ((int)(sizeof TRACK / sizeof TRACK[0]))

/* the lamp events, as (frame, top, middle, bottom) */
static const struct { short t; unsigned char lamp[3]; } LAMPS[] = {
    {   1, { SMK_LAMP_RED_OFF, SMK_LAMP_RED_OFF, SMK_LAMP_GREEN_OFF } },
    { 179, { SMK_LAMP_RED_ON,  SMK_LAMP_RED_OFF, SMK_LAMP_GREEN_OFF } },
    { 244, { SMK_LAMP_RED_ON,  SMK_LAMP_RED_ON,  SMK_LAMP_GREEN_OFF } },
    { 309, { SMK_LAMP_RED_ON,  SMK_LAMP_RED_ON,  SMK_LAMP_GREEN_ON  } },
};
#define LAMPS_N ((int)(sizeof LAMPS / sizeof LAMPS[0]))

#define CHEER_T 309              /* the pose arrives with the green */

void smk_start_frame(int t, smk_start *out)
{
    out->on = false;
    out->x = SMK_START_X;
    out->y = 0;
    out->cheer = false;
    out->lit = 0;
    out->lamp[0] = out->lamp[1] = out->lamp[2] = SMK_LAMP_RED_OFF;
    for (int i = 0; i < 4; i++) { out->quad[i].dx = out->quad[i].dy = 0;
                                  out->quad[i].tile = 0; }
    if (t < TRACK[0].t) return;

    int y = TRACK[TRACK_N - 1].y;
    for (int i = 0; i < TRACK_N; i++) {
        if (TRACK[i].t > t) { y = TRACK[i - 1].y; break; }
        if (i == TRACK_N - 1) y = TRACK[i].y;
    }
    /* No clipping here.  The game keeps him in OAM at y = -48 on the way
     * in and parks him at -40 on the way out, both of them entirely
     * above the screen, and the blit clips - so `on` means he is part of
     * the sequence, not that a pixel of him lands. */
    out->on = true;
    out->y = y;
    out->cheer = t >= CHEER_T;
    for (int i = 0; i < LAMPS_N; i++)
        if (t >= LAMPS[i].t) {
            out->lamp[0] = LAMPS[i].lamp[0];
            out->lamp[1] = LAMPS[i].lamp[1];
            out->lamp[2] = LAMPS[i].lamp[2];
            out->lit = i;
        }
    /* $42/$40 over $46/$44, the left pair swapped when he cheers */
    out->quad[0].dx =  0; out->quad[0].dy =  0;
    out->quad[0].tile = (uint8_t)(out->cheer ? 0x4A : 0x42);
    out->quad[1].dx = 16; out->quad[1].dy =  0; out->quad[1].tile = 0x40;
    out->quad[2].dx =  0; out->quad[2].dy = 16;
    out->quad[2].tile = (uint8_t)(out->cheer ? 0x4C : 0x46);
    out->quad[3].dx = 16; out->quad[3].dy = 16; out->quad[3].tile = 0x44;
}


/* ---- The lap sign (NOTES 168) -----------------------------------------
 *
 * Read out of the game's own OAM at a lap-COMPLETING crossing - the
 * first crossing, $7F -> $80, is the grid leaving the line and shows
 * nothing (NOTES 052), which cost one four-minute capture.
 *
 * The plate's path, frame by frame from the crossing: he arrives from
 * off the top-left at (5, -39), arcs down and right to (93, 44) around
 * frame 88, and leaves the way he came, gone by frame 164.  MEASURED,
 * not derived - the same standing as the start sequence above. */
static const struct { short x, y; } LAPSIGN[] = {
#include "lapsign_path.inc"
};
#define LAPSIGN_N ((int)(sizeof LAPSIGN / sizeof LAPSIGN[0]))

void smk_lapsign_frame(int t, int lap, int laps, smk_lapsign *out)
{
    out->on = false;
    out->x = out->y = 0;
    out->plate = SMK_LAPSIGN_PLATE;
    out->digit = -1;
    out->final_lap = false;
    if (t < 2 || t >= LAPSIGN_N) return;

    out->on = true;
    out->x = LAPSIGN[t].x;
    out->y = LAPSIGN[t].y;
    /* The numeral is ONE tile column.  The game draws it as the right
     * half of a 16x16 whose left half is the plate's edge bar, which for
     * lap 2 reads as "$A3 + n" and for lap 4 draws "34" (NOTES 168b). */
    if (lap >= laps) {
        out->final_lap = true;              /* its own 32x16 plate */
        out->plate = SMK_LAPSIGN_FINAL_L;
    } else {
        int n = lap - 2;
        if (n < 0) n = 0;
        if (n > 2) n = 2;                   /* $A4/$A5/$A6 are 2/3/4 */
        out->digit = SMK_LAPSIGN_DIGIT + n;
    }
}


/* ---- Lakitu lowering a rescued kart (NOTES 168a/169a) ------------------
 *
 * MEASURED, frame by frame, from the game's own OAM.  He does not simply
 * track the kart down: he holds high, rises a little further, and then
 * descends - which a ramp from the kart's height cannot produce. */
static const short RESCUE_Y[] = {
#include "rescue_path.inc"
};
#define RESCUE_N ((int)(sizeof RESCUE_Y / sizeof RESCUE_Y[0]))

int smk_rescue_y(int t)
{
    if (t < 0) t = 0;
    if (t >= RESCUE_N) t = RESCUE_N - 1;
    return RESCUE_Y[t];
}

/* ---- the chequered flag (NOTES 184) ---------------------------------- */

/* The three wave poses, in the order the capture shows them cycling. */
static const struct { int hi, lo; } FLAG_WAVE[3] = {
    { 0x6E, 0x6C }, { 0x82, 0x80 }, { 0x8E, 0x8C },
};

static const struct { int16_t x, y; } FLAG_PATH[] =
#include "flag_path.inc"
;

void smk_flag_frame(int t, smk_flag *out)
{
    memset(out, 0, sizeof *out);
    int n = (int)(sizeof FLAG_PATH / sizeof FLAG_PATH[0]);
    if (t < 0 || t >= n) return;
    out->on = true;
    out->x = FLAG_PATH[t].x;
    out->y = FLAG_PATH[t].y;
    out->pose = (t / SMK_FLAG_WAVE) % 3;
}

/* The flag's six 16x16 drawings, from the shared blob rather than the HUD
 * set - see smk_flagart.  A VRAM tile's bytes live at (tile - 48) * 32. */
bool smk_flag_load(const smk_rom *rom, smk_flagart *out)
{
    static uint8_t buf[131072];
    memset(out, 0, sizeof *out);
    long n = smk_decompress_into(rom->data, rom->size,
                                 smk_snes_to_pc(rom, SMK_SHADOW_SRC),
                                 buf, sizeof buf, 0, NULL);
    if (n <= 0) return false;
    for (int w = 0; w < 3; w++) {
        int half[2] = { FLAG_WAVE[w].hi, FLAG_WAVE[w].lo };
        for (int h = 0; h < 2; h++) {
            for (int q = 0; q < 4; q++) {
                int tn = half[h] + (q >> 1) * 16 + (q & 1);
                long off = (long)(tn - 48) * 32;
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
                            out->px[w][h][py * 16 + px] |= (uint8_t)(v << (pair * 2));
                        }
                    }
                }
            }
        }
    }
    /* his head and cloud from the SAME source, so the whole group is
     * consistent: taking them from the HUD set instead left the flag
     * eight pixels adrift, because those indices are not VRAM tiles. */
    {
        const int extra[3] = { SMK_FLAG_HEAD, SMK_FLAG_CLOUD_L, SMK_FLAG_CLOUD_R };
        uint8_t *dst[3] = { out->head, out->cloud[0], out->cloud[1] };
        for (int e = 0; e < 3; e++)
            for (int q = 0; q < 4; q++) {
                int tn = extra[e] + (q >> 1) * 16 + (q & 1);
                long off = (long)(tn - 48) * 32;
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
                            dst[e][py * 16 + px] |= (uint8_t)(v << (pair * 2));
                        }
                    }
                }
            }
    }
    out->ok = true;
    return true;
}
