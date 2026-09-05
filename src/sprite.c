/* Kart sprites: uncompressed 4bpp frames read straight out of the ROM.
 *
 * Found by logging DMA during a race: the game streams 128-byte quarters
 * from banks $C0/$C2/$C4/$C5 at addresses $200 apart, so a frame is 512
 * bytes = 16 tiles = 32x32 pixels.  The tiles are stored in PPU order, i.e.
 * a 4x4 sprite with a 16-tile row stride.
 */
#include "smk.h"
#include <string.h>

#define KART_SHEET_DEFAULT  0xC02000u   /* observed in the DMA log */

/* Sheets found by rendering a frame from every bank at $2000; the sheet and
 * palette pairing below was then read off a grid of every sheet under every
 * sprite palette.
 *
 * NOT decoded: the game's own character table, which is what really binds a
 * driver to a sheet and a palette, has not been found.  Note also that the
 * sprite half of the palette differs between themes (37-53 of 256 bytes),
 * so the game re-tints drivers per course - these indices are right, the
 * exact colours follow whichever track is loaded. */
const smk_driver SMK_DRIVERS[SMK_CHARACTERS] = {
    { "Mario",   0xC02000u, 0x90 },
    { "Luigi",   0xC02000u, 0xA0 },   /* same sheet, different palette */
    { "Bowser",  0xC12000u, 0x80 },
    { "Peach",   0xC22000u, 0xB0 },
    { "DK Jr",   0xC32000u, 0xB0 },
    { "Yoshi",   0xC42000u, 0x80 },
    { "Koopa",   0xC52000u, 0x90 },
    { "Toad",    0xC62000u, 0x90 },
};

/* One 8x8 4bpp tile -> 64 palette indices. */
static void tile_px(const uint8_t *src, uint8_t out[64])
{
    for (int pair = 0; pair < 2; pair++) {
        const uint8_t *p = src + pair * 16;
        for (int y = 0; y < 8; y++) {
            uint8_t lo = p[y * 2], hi = p[y * 2 + 1];
            for (int x = 0; x < 8; x++) {
                int bit = 7 - x;
                int v = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
                out[y * 8 + x] |= (uint8_t)(v << (pair * 2));
            }
        }
    }
}

bool smk_sprites_load(const smk_rom *rom, uint32_t base, smk_sprites *out)
{
    memset(out, 0, sizeof *out);
    if (base == 0) base = KART_SHEET_DEFAULT;
    uint32_t pc = smk_snes_to_pc(rom, base);

    out->frames = 0;
    for (int f = 0; f < 48; f++) {                 /* the sheet's 48; frame 48 is built below */
        /* frames advance across, then down, in the PPU's tile grid */
        int n0 = (f % 4) * 4 + (f / 4) * 64;
        for (int tr = 0; tr < 4; tr++) {
            for (int tc = 0; tc < 4; tc++) {
                uint32_t off = pc + (uint32_t)(n0 + tr * 16 + tc) * 32u;
                if (off + 32 > rom->size) return out->frames > 0;
                uint8_t t[64];
                memset(t, 0, sizeof t);
                tile_px(rom->data + off, t);
                for (int y = 0; y < 8; y++)
                    for (int x = 0; x < 8; x++)
                        out->px[f][(tr * 8 + y) * SMK_SPR_PX + tc * 8 + x] =
                            t[y * 8 + x];
            }
        }
        out->frames = f + 1;
    }
    /* THE CELEBRATION (NOTES 199, from a real finish in the oracle): the
     * front view, frame 46, with five tiles of its LEFT half replaced by
     * sheet tiles 3, 16, 19, 34, 35 - the driver's head and raised arms -
     * and drawn, like every front/rear pose, as that half and its mirror. */
    if (out->frames == 48) {
        static const struct { int r, c, tile; } SWAP[5] = {
            { 0, 1, 3 }, { 1, 0, 16 }, { 1, 1, 19 }, { 2, 0, 34 }, { 2, 1, 35 } };
        memcpy(out->px[48], out->px[46], sizeof out->px[48]);
        for (int i = 0; i < 5; i++) {
            uint32_t off = pc + (uint32_t)SWAP[i].tile * 32u;
            if (off + 32 > rom->size) break;
            uint8_t t[64]; memset(t, 0, sizeof t); tile_px(rom->data + off, t);
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++)
                    out->px[48][(SWAP[i].r * 8 + y) * SMK_SPR_PX + SWAP[i].c * 8 + x] = t[y * 8 + x];
        }
        out->frames = 49;
    }
    return true;
}

/* Sprite priority against the Mode 7 plane (NOTES 128).
 *
 * The SNES gives a sprite a priority BELOW BG1 when the kart has dropped
 * under the plane, so the track hides it - and a hole does not, because
 * colour 0 of the plane is transparent.  Ours is the same rule made
 * explicit: while a clip mask is set, a sprite pixel is dropped wherever
 * the mask says the plane is opaque.  Set it around a falling kart and
 * clear it after. */
static const uint8_t *clip_mask;
static int clip_pitch;
void smk_draw_set_clip_mask(const uint8_t *mask, int pitch)
{
    clip_mask = mask;
    clip_pitch = pitch;
}
static inline bool clipped(int sx, int sy)
{
    return clip_mask && clip_mask[(size_t)sy * (size_t)clip_pitch + sx];
}

void smk_draw_sprite(const smk_sprites *s, int frame, const uint32_t *palette,
                     int pal_base, int cx, int cy, int scale, bool hflip,
                     uint32_t *pixels, int w, int h, int pitch_px)
{
    if (frame < 0 || frame >= s->frames || scale < 1) return;
    const uint8_t *src = s->px[frame];
    int size = SMK_SPR_PX * scale;
    int x0 = cx - size / 2, y0 = cy - size;      /* anchored at the wheels */

    for (int y = 0; y < size; y++) {
        int sy = y0 + y;
        if (sy < 0 || sy >= h) continue;
        const uint8_t *row = src + (y / scale) * SMK_SPR_PX;
        uint32_t *dst = pixels + (size_t)sy * (size_t)pitch_px;
        for (int x = 0; x < size; x++) {
            int sx = x0 + x;
            if (sx < 0 || sx >= w) continue;
            int col = x / scale;
            uint8_t v = row[hflip ? SMK_SPR_PX - 1 - col : col];
            if (v == 0) continue;                /* index 0 is transparent */
            if (clipped(sx, sy)) continue;   /* behind the plane */
            dst[sx] = palette[(pal_base + v) & 0xFF];
        }
    }
}


/* Draw a kart frame at a CONTINUOUS scale (NOTES 100).
 *
 * Distant karts really are smaller in the original - three opponents up
 * the road are a third of the player's height in a reference shot - so
 * the size follows the projection, anchored so that a kart at the
 * player's own distance is the SNES's 32 px.  (The hardware quantises
 * this to a few art sizes; ours is continuous, and that divergence is
 * in the ledger.) */
void smk_draw_sprite_scaled(const smk_sprites *s, int frame,
                            const uint32_t *palette, int pal_base,
                            int cx, int cy, float scale, bool hflip,
                            bool mirror_half,
                            uint32_t *pixels, int w, int h, int pitch_px)
{
    if (frame < 0 || frame >= s->frames || scale <= 0.01f) return;
    const uint8_t *src = s->px[frame];
    int size = (int)(SMK_SPR_PX * scale + 0.5f);
    if (size < 2) return;
    int x0 = cx - size / 2, y0 = cy - size;
    for (int y = 0; y < size; y++) {
        int sy = y0 + y;
        if (sy < 0 || sy >= h) continue;
        const uint8_t *row = src + (y * SMK_SPR_PX / size) * SMK_SPR_PX;
        uint32_t *dst = pixels + (size_t)sy * (size_t)pitch_px;
        for (int x = 0; x < size; x++) {
            int sx = x0 + x;
            if (sx < 0 || sx >= w) continue;
            int col = x * SMK_SPR_PX / size;
            if (mirror_half && col >= SMK_SPR_PX / 2)
                col = SMK_SPR_PX - 1 - col;
            else if (hflip)
                col = SMK_SPR_PX - 1 - col;
            uint8_t v = row[col];
            if (v == 0) continue;
            if (clipped(sx, sy)) continue;   /* behind the plane */
            dst[sx] = palette[(pal_base + v) & 0xFF];
        }
    }
}

/* The straight rear view: the game stores only the LEFT HALF (frame 0's
 * left 16 columns) and mirrors it - measured pixel-exact against the
 * live P1 sprite (NOTES 080).  Drawing any full rotation frame as
 * "straight" gives the head-leaning look, because none is symmetric. */
void smk_draw_sprite_mirror2(const smk_sprites *s, int frame,
                             const uint32_t *palette, int pal_base,
                             int cx, int cy, int scale, bool mini,
                             uint32_t *pixels, int w, int h, int pitch_px)
{
    if (frame < 0 || frame >= s->frames || scale < 1) return;
    const uint8_t *src = s->px[frame];
    int px_sz = mini ? SMK_SPR_PX / 2 : SMK_SPR_PX;   /* art pixels shown */
    int step  = mini ? 2 : 1;                         /* source sampling  */
    int size = px_sz * scale;
    int x0 = cx - size / 2, y0 = cy - size;

    for (int y = 0; y < size; y++) {
        int sy = y0 + y;
        if (sy < 0 || sy >= h) continue;
        const uint8_t *row = src + ((y / scale) * step) * SMK_SPR_PX;
        uint32_t *dst = pixels + (size_t)sy * (size_t)pitch_px;
        for (int x = 0; x < size; x++) {
            int sx = x0 + x;
            if (sx < 0 || sx >= w) continue;
            int col = (x / scale) * step;
            if (col >= SMK_SPR_PX / 2) col = SMK_SPR_PX - 1 - col;
            uint8_t v = row[col];
            if (v == 0 && mini) {             /* keep the outline */
                int c2 = col + 1;
                if (c2 >= SMK_SPR_PX / 2) c2 = SMK_SPR_PX - 1 - c2;
                v = row[c2];
            }
            if (v == 0) continue;
            if (clipped(sx, sy)) continue;   /* behind the plane */
            dst[sx] = palette[(pal_base + v) & 0xFF];
        }
    }
}

void smk_draw_sprite_mirror(const smk_sprites *s, int frame,
                            const uint32_t *palette, int pal_base,
                            int cx, int cy, int scale,
                            uint32_t *pixels, int w, int h, int pitch_px)
{
    smk_draw_sprite_mirror2(s, frame, palette, pal_base, cx, cy, scale,
                            false, pixels, w, h, pitch_px);
}

/* Far karts: the game COMPOSES a ~16px sprite at runtime (the small art
 * is in no ROM bank - a software minifier builds it in WRAM, NOTES 076).
 * Until that composer is decoded, sample the full frame 2:1 - a labelled
 * approximation of the real minifier, sizes and switch depth measured. */
/* One 16x16 QUADRANT of a packed frame, at any scale.
 *
 * Frames 33-43 are not single drawings: each holds four 16x16 sprites in a
 * 2x2 arrangement (the far-distance variants).  The victory pose lives in
 * one of them - frame 40, upper right - and the game draws it at DOUBLE
 * the normal art scale, which is why the pixels in it are visibly chunkier
 * than a driving kart's (NOTES 180).  Nothing else could draw a quadrant,
 * so nothing else could reach the pose.
 */
void smk_draw_sprite_quad(const smk_sprites *s, int frame, int quad,
                          const uint32_t *palette, int pal_base,
                          int cx, int cy, int scale, bool hflip,
                          uint32_t *pixels, int w, int h, int pitch_px)
{
    if (frame < 0 || frame >= s->frames || scale < 1) return;
    if (quad < 0 || quad > 3) return;
    const int Q = SMK_SPR_PX / 2;                 /* 16 */
    int qx = (quad & 1) * Q, qy = (quad >> 1) * Q;
    const uint8_t *src = s->px[frame];
    int size = Q * scale;
    int x0 = cx - size / 2, y0 = cy - size;

    for (int y = 0; y < size; y++) {
        int sy = y0 + y;
        if (sy < 0 || sy >= h) continue;
        const uint8_t *row = src + (qy + y / scale) * SMK_SPR_PX + qx;
        uint32_t *dst = pixels + (size_t)sy * (size_t)pitch_px;
        for (int x = 0; x < size; x++) {
            int sx = x0 + x;
            if (sx < 0 || sx >= w) continue;
            int col = x / scale;
            uint8_t v = row[hflip ? Q - 1 - col : col];
            if (v == 0) continue;
            dst[sx] = palette[(pal_base + v) & 0xFF];
        }
    }
}

void smk_draw_sprite_mini(const smk_sprites *s, int frame,
                          const uint32_t *palette, int pal_base,
                          int cx, int cy, int scale, bool hflip,
                          uint32_t *pixels, int w, int h, int pitch_px)
{
    if (frame < 0 || frame >= s->frames || scale < 1) return;
    const uint8_t *src = s->px[frame];
    int size = (SMK_SPR_PX / 2) * scale;
    int x0 = cx - size / 2, y0 = cy - size;

    for (int y = 0; y < size; y++) {
        int sy = y0 + y;
        if (sy < 0 || sy >= h) continue;
        const uint8_t *row = src + ((y / scale) * 2) * SMK_SPR_PX;
        uint32_t *dst = pixels + (size_t)sy * (size_t)pitch_px;
        for (int x = 0; x < size; x++) {
            int sx = x0 + x;
            if (sx < 0 || sx >= w) continue;
            int col = (x / scale) * 2;
            uint8_t v = row[hflip ? SMK_SPR_PX - 1 - col : col];
            if (v == 0) {          /* keep the outline: check the partner */
                uint8_t v2 = row[hflip ? SMK_SPR_PX - 2 - col : col + 1];
                if (v2 == 0) continue;
                v = v2;
            }
            if (clipped(sx, sy)) continue;   /* behind the plane */
            dst[sx] = palette[(pal_base + v) & 0xFF];
        }
    }
}

/* The measured frame-selection rule (NOTES 041).
 *
 * Obtained by force-spinning a kart in the running game and logging the
 * frame each upload came from: boundaries sit at 22.5 + 11.25n degrees for
 * frames 1..7 and 22.5-degree steps beyond, frame 10 covering the frontal
 * arc.  In angle units ($10000 = full turn):
 *
 *     |rel| <  $1000  frame 1   (squarely from behind)
 *           <  $1800  frame 2
 *           <  $2000  frame 3
 *           <  $2800  frame 4
 *           <  $3000  frame 5
 *           <  $3800  frame 6
 *           <  $4800  frame 7
 *           <  $5800  frame 8
 *           <  $6800  frame 9
 *           else      frame 10  (front, through 180 degrees)
 *
 * The other half of the circle is the same frames mirrored.  The game adds
 * about $280 of hysteresis per boundary, omitted here.
 */
static const uint16_t FRAME_BOUNDS[] = {
    0x1000, 0x1800, 0x2000, 0x2800, 0x3000, 0x3800, 0x4800, 0x5800, 0x6800,
};

int smk_sprite_for_heading(int tier, uint16_t rel, bool *hflip)
{
    unsigned r = rel;
    bool mirror = r > 0x8000;
    if (mirror)
        r = 0x10000 - r;
    if (hflip)
        *hflip = mirror;
    int f = 1;
    for (size_t i = 0; i < sizeof FRAME_BOUNDS / sizeof *FRAME_BOUNDS; i++)
        if (r >= FRAME_BOUNDS[i])
            f = (int)i + 2;
    f += tier;
    if (f >= SMK_SPR_FRAMES) f = SMK_SPR_FRAMES - 1;
    return f;
}

/* ---- the finishing list's faces (NOTES 282) --------------------------- */

/* SMK_DRIVERS runs Mario Luigi Bowser Peach DK Yoshi Koopa Toad; the sheet
 * runs the game's own Mario Luigi Bowser Peach DK Koopa Toad Yoshi. */
int smk_face_of(int driver)
{
    static const int COL[SMK_CHARACTERS] = { 0, 1, 2, 3, 4, 7, 5, 6 };
    return COL[((driver % SMK_CHARACTERS) + SMK_CHARACTERS) % SMK_CHARACTERS];
}

bool smk_faces_load(const smk_rom *rom, smk_faces *out)
{
    memset(out, 0, sizeof *out);
    uint8_t buf[120 * 32];
    uint32_t pc = smk_snes_to_pc(rom, SMK_FACES_SRC);
    if (pc >= rom->size) return false;
    long n = smk_decompress(rom->data, rom->size, pc, buf, sizeof buf, NULL);
    if (n < (long)sizeof buf) return false;
    uint8_t t[64];
    /* tile (r, c) of the 16-wide sheet -> a 16x16 face's quarter */
    for (int row = 0; row < SMK_FACE_ROWS; row++)
        for (int c = 0; c < SMK_CHARACTERS; c++)
            for (int q = 0; q < 4; q++) {
                int tile = (row * 2 + (q >> 1)) * 16 + c * 2 + (q & 1);
                memset(t, 0, sizeof t);
                tile_px(buf + tile * 32, t);
                for (int y = 0; y < 8; y++)
                    for (int x = 0; x < 8; x++)
                        out->face[row][c][((q >> 1) * 8 + y) * SMK_FACE_PX + (q & 1) * 8 + x]
                            = t[y * 8 + x];
            }
    /* the cutouts: flood the index-1 cell in from the edges */
    for (int row = 0; row < SMK_FACE_ROWS; row++)
        for (int c = 0; c < SMK_CHARACTERS; c++) {
            uint8_t *f = out->face[row][c], *k = out->cut[row][c];
            memcpy(k, f, SMK_FACE_PX * SMK_FACE_PX);
            uint8_t seen[SMK_FACE_PX * SMK_FACE_PX];
            memset(seen, 0, sizeof seen);
            int stack[SMK_FACE_PX * SMK_FACE_PX], sp = 0;
            for (int i = 0; i < SMK_FACE_PX; i++) {
                int e[4] = { i, (SMK_FACE_PX - 1) * SMK_FACE_PX + i, i * SMK_FACE_PX, i * SMK_FACE_PX + SMK_FACE_PX - 1 };
                for (int q = 0; q < 4; q++)
                    if (!seen[e[q]] && (f[e[q]] == 1 || f[e[q]] == 0)) { seen[e[q]] = 1; stack[sp++] = e[q]; }
            }
            while (sp > 0) {
                int i = stack[--sp];
                k[i] = 0;
                int x = i % SMK_FACE_PX, y = i / SMK_FACE_PX;
                int nb[4] = { x > 0 ? i - 1 : -1, x < SMK_FACE_PX - 1 ? i + 1 : -1,
                              y > 0 ? i - SMK_FACE_PX : -1, y < SMK_FACE_PX - 1 ? i + SMK_FACE_PX : -1 };
                for (int q = 0; q < 4; q++) {
                    int j = nb[q];
                    if (j < 0 || seen[j] || (f[j] != 1 && f[j] != 0)) continue;
                    seen[j] = 1; stack[sp++] = j;
                }
            }
        }
    for (int d = 0; d < SMK_CHARACTERS; d++)
        for (int half = 0; half < 2; half++) {
            int tile = (6 + half) * 16 + d;
            memset(t, 0, sizeof t);
            tile_px(buf + tile * 32, t);
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++)
                    out->digit[d][(half * 8 + y) * 8 + x] = t[y * 8 + x];
        }
    out->ok = true;
    return true;
}
