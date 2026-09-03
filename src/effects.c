/* The kart's ground effects - tyre smoke on a slide, dust off-road - DECODED
 * (NOTES 109).
 *
 * The game runs one effect OBJECT per player ($1E00/$1E20).  Every frame
 * $80CF7B..$80D4A3 decides what it shows: a grounded kart dispatches on the
 * surface class under it through the jump table at $80D31A; each class
 * handler returns an effect KIND (an offset into the record table at
 * $80D1CE) or switches the object off.  A record is [template block (WRAM),
 * script list (ROM), attribute XOR]: the script list has one animation
 * script per kart SPRITE FRAME (the puffs sit under the wheels of the pose
 * that is drawn), a script is a list of [duration, template] entries with
 * an `$80 lo hi` jump, and a template is [count][x, y, tile, attr]... - the
 * OAM entries themselves, relative to the kart sprite, copied by $80BFC8
 * with the whole group mirrored when the kart sprite is (x ^ $FF, attr ^
 * $40) and the record's XOR applied (palette 5 = white smoke on road,
 * palette 7 = tan dust).
 *
 * Data, all read from the ROM here:
 *   templates  - the stream at $C5:EE00, which decompresses to WRAM $2000;
 *                the record's block address is an offset into it
 *   puff tiles - the stream at $C4:9C1A: 8x8 4bpp subtiles for VRAM $101..
 *                at 20 + 32k (row-major, 16 per row); VRAM $100 is the 12
 *                bytes before it plus its 20-byte header (what the game
 *                really displays), VRAM $110 is subtile 15 with its two
 *                junk rows masked (LABELLED: the live tile differs in one
 *                more pixel, from a source not found in any stream)
 *   scripts, records, wobble table - ROM bank $80
 */
#include "smk.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define FX_TEMPLATE_STREAM 0xC5EE00u
#define FX_TILE_STREAM     0xC49C1Au
#define FX_WRAM_BASE       0x2000u
#define FX_RECORDS         0x80D1CEu
#define FX_WOBBLE          0x80D46Fu
#define FX_LO_STREAM       0xC00903u   /* sprite tiles $000-$03F (NOTES 197) */

static uint8_t rd8(const smk_rom *rom, uint32_t snes)
{
    uint32_t pc = smk_snes_to_pc(rom, snes);
    return pc < rom->size ? rom->data[pc] : 0;
}
static uint16_t rd16(const smk_rom *rom, uint32_t snes)
{
    return (uint16_t)(rd8(rom, snes) | (rd8(rom, snes + 1) << 8));
}

static void decode_tile(const uint8_t *src, uint8_t *dst)
{
    memset(dst, 0, 64);
    for (int pair = 0; pair < 2; pair++) {
        const uint8_t *q = src + pair * 16;
        for (int y = 0; y < 8; y++) {
            uint8_t lo = q[y * 2], hi = q[y * 2 + 1];
            for (int x = 0; x < 8; x++) {
                int bit = 7 - x;
                int v = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
                dst[y * 8 + x] |= (uint8_t)(v << (pair * 2));
            }
        }
    }
}

bool smk_effects_load(const smk_rom *rom, smk_effects *fx)
{
    static uint8_t tpl[0x4000], tiles[0x1000];
    memset(fx, 0, sizeof *fx);
    long nt = smk_decompress_into(rom->data, rom->size,
                                  smk_snes_to_pc(rom, FX_TEMPLATE_STREAM),
                                  tpl, sizeof tpl, 0, NULL);
    long ng = smk_decompress_into(rom->data, rom->size,
                                  smk_snes_to_pc(rom, FX_TILE_STREAM),
                                  tiles, sizeof tiles, 0, NULL);
    if (nt < 0x1400 || ng < 20 + 32 * 22) return false;

    /* the 32 subtiles VRAM $100..$11F */
    uint8_t raw[32];
    for (int t = 0; t < 32; t++) {
        int k = (t & 0xF) - 1 + ((t >> 4) & 1) * 16;     /* stream subtile */
        if ((t & 0xF) == 0) {
            memset(raw, 0, 12);
            memcpy(raw + 12, tiles, 20);                    /* VRAM $100 */
            if (t == 0x10) {
                memcpy(raw, tiles + 20 + 32 * 15, 32);      /* VRAM $110 */
                for (int y = 0; y < 8; y++) {               /* mask the junk */
                    uint8_t keep = (uint8_t)(raw[16 + y * 2] | raw[17 + y * 2]);
                    raw[y * 2] &= keep; raw[y * 2 + 1] &= keep;
                }
            }
        } else
            memcpy(raw, tiles + 20 + 32 * k, 32);
        decode_tile(raw, fx->tiles[t]);
    }

    /* sprite tiles $000-$03F: one stream, 64 tiles, tile n at n*32 - the
     * game's own upload to VRAM $4000 (tools/labs/dmalist.py) */
    {
        static uint8_t lo[0x800];
        long nl = smk_decompress_into(rom->data, rom->size,
                                      smk_snes_to_pc(rom, FX_LO_STREAM), lo, sizeof lo, 0, NULL);
        if (nl >= 0x800)
            for (int t = 0; t < 64; t++) decode_tile(lo + t * 32, fx->lo[t]);
    }
    /* the wobble table ($80D460: x += table[frame & 7]) */
    for (int i = 0; i < 8; i++) fx->wobble[i] = (int16_t)rd16(rom, FX_WOBBLE + (uint32_t)i * 2u);

    /* records for the kinds the handlers below can return */
    static const int kinds[] = { 0x00, 0x06, 0x0C, 0x12, 0x18, 0x1E, 0x24, 0x2A, 0x30, 0x36, 0x3C };
    for (size_t i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
        int kind = kinds[i];
        smk_effect_kind *k = &fx->kind[kind / 6];
        uint32_t rec = FX_RECORDS + (uint32_t)kind;
        uint16_t block = rd16(rom, rec), scripts = rd16(rom, rec + 2);
        k->attr_xor = rd8(rom, rec + 5);   /* the flags word's high byte, ORed onto the attribute (NOTES 268) */
        k->valid = true;
        /* the template pointer table at the block, offsets from the block */
        uint32_t boff = block - FX_WRAM_BASE;
        for (int f = 0; f < 12; f++) {
            uint32_t sc = 0x800000u | rd16(rom, 0x800000u | (scripts + (uint32_t)f * 2u));
            /* parse the script: [dur, tpl] entries up to the $80 jump */
            smk_effect_script *s = &k->script[f];
            s->n = 0;
            for (int e = 0; e < 8; e++) {
                uint8_t dur = rd8(rom, sc + (uint32_t)e * 2u);
                if (dur & 0x80) break;
                s->dur[s->n] = dur;
                s->tpl[s->n] = rd8(rom, sc + (uint32_t)e * 2u + 1u);
                s->n++;
            }
            for (int e = 0; e < s->n; e++) {
                smk_effect_template *t = &s->t[e];
                t->n = 0;
                uint32_t pe = boff + (uint32_t)s->tpl[e] * 2u;
                if (pe + 1 >= (uint32_t)nt) continue;
                uint32_t p = boff + (uint32_t)(tpl[pe] | (tpl[pe + 1] << 8));
                if (p >= (uint32_t)nt) continue;
                t->n = tpl[p];
                if (t->n > 8) t->n = 8;
                if (p + 1 + (uint32_t)t->n * 4u > (uint32_t)nt) { t->n = 0; continue; }
                for (int j = 0; j < t->n; j++) {
                    const uint8_t *q = tpl + p + 1 + (uint32_t)j * 4u;
                    t->x[j] = (int8_t)q[0]; t->y[j] = (int8_t)q[1];
                    t->tile[j] = q[2]; t->attr[j] = q[3];
                }
            }
        }
    }
    fx->ok = true;
    return true;
}

/* $80D4A3 + the class handlers: which effect a grounded kart shows.
 * Returns the kind, or -1 for none.  `spinning` is $80D44E: $E4 >= $400 or
 * $E2 bit 3. */
int smk_effects_pick(uint8_t surf, bool grounded, bool spinning, bool deep_drift,
                     int speed)
{
    if (!grounded) return -1;
    int cls = surf & 0x7E;
    if (cls <= 0x1E || (cls >= 0x40 && cls <= 0x52)) {         /* $80D37A */
        if (spinning) return 0x18;
        return deep_drift ? 0x24 : -1;
    }
    if (cls >= 0x54 && cls <= 0x58) {                          /* $80D3B6 */
        if (spinning) return 0x1E;
        if (deep_drift) return 0x2A;
        return speed >= 0x80 ? 0x00 : -1;
    }
    /* MEASURED on the running game (tools/labs/surffx.py, NOTES 197): $5A
     * shows kind $12's tiles ($024-$02C) every frame, $5C/$5E kind $06's
     * ($000-$004); the handlers $80:D3F3 / $80:D3D2 pick $3C / $36 only
     * when nearly stopped (speed < $10 / $20) */
    if (cls == 0x5A) { if (spinning) return 0x12; return speed < 0x10 ? 0x3C : 0x12; }
    if (cls == 0x5C || cls == 0x5E) { if (speed < 0x20 && !spinning) return 0x36; return 0x06; }
    /* $22/$24, the shallow water ($80:D418 -> $D437): kind $0C, the water
     * spray, from the same cloud set (its tiles 32-34 / 49-51 are sprite
     * $020-$022 / $031-$033).  $20 ($80:D40B) is the deep water's own
     * business - not ported, LABELLED */
    if (cls == 0x22 || cls == 0x24) return speed >= 0x80 ? 0x0C : -1;
    return -1;
}

/* the object's script interpreter ($80D530): a new request restarts the
 * script and shows its SECOND entry first (the pointer is advanced before
 * the first read), then one entry per expired duration, looping. */
void smk_effects_step(smk_effect_state *st, int kind, int frame_idx)
{
    if (kind < 0) { st->kind = -1; return; }
    if (st->kind != kind || st->frame_idx != frame_idx) {
        st->kind = kind; st->frame_idx = frame_idx;
        st->pos = 0; st->dur = 0;
    }
    if (--st->dur < 0) {
        st->pos++;
        st->dur = 0;     /* set from the entry's duration when drawing */
    }
}

static void draw_subtile(const uint8_t *px, bool hflip, int sx, int sy, int scale,
                         const uint32_t *pal, uint32_t *fb, int w, int h)
{
    for (int y = 0; y < 8 * scale; y++) {
        int yy = sy + y;
        if (yy < 0 || yy >= h) continue;
        for (int x = 0; x < 8 * scale; x++) {
            int xx = sx + x;
            if (xx < 0 || xx >= w) continue;
            int tx = x / scale, ty = y / scale;
            uint8_t v = px[ty * 8 + (hflip ? 7 - tx : tx)];
            if (v) fb[(size_t)yy * (size_t)w + xx] = pal[v];
        }
    }
}

void smk_effects_draw(const smk_effects *fx, const smk_effect_state *st, bool mirror,
                      unsigned frame_counter, int base_x, int base_y, int scale,
                      const uint32_t *palette, uint32_t *fb, int w, int h)
{
    if (!fx->ok || st->kind < 0) return;
    const smk_effect_kind *k = &fx->kind[st->kind / 6];
    if (!k->valid) return;
    int f = st->frame_idx;
    if (f < 0) f = 0;
    if (f > 11) f = 11;
    const smk_effect_script *s = &k->script[f];
    if (s->n == 0) return;
    const smk_effect_template *t = &s->t[st->pos % s->n];
    int wob = fx->wobble[frame_counter & 7];
    for (int j = 0; j < t->n; j++) {
        int tx = t->x[j] & 0xFF;
        int dx = mirror ? (int8_t)(uint8_t)(tx ^ 0xFF) : (int8_t)(uint8_t)tx;
        int dy = t->y[j];
        /* The record's flags byte is ORed onto the attribute, not XORed.
         * MEASURED (NOTES 268): the game's drift puff on Ghost Valley is
         * OBJ palette 7 - its pixels are $AD9C52 and $8C7B31, which are
         * that palette's entries 7 and 6 - and kind $24's templates carry
         * attr $3E with a flags byte of $05.  `| $05` keeps palette 7 and
         * sets the theme's tile bank; `^ $05` clears palette bit 2 and
         * gives palette 5, which on this theme is reds, white and greys:
         * the grey smoke the user reported ("it is dust coming off the
         * wheels, not grey smoke").
         *
         * The change is confined to that one kind.  Over all eleven kinds
         * the two rules give the SAME palette everywhere else: no
         * template has attr bit 0 set, so `|$01` and `^$01` agree, and
         * kind $1E's templates have bit 2 clear so its $05 agrees too.
         * $24 is the only place they differ, and the capture says which
         * one is right. */
        uint8_t attr = (uint8_t)((t->attr[j] | k->attr_xor) ^ (mirror ? 0x40 : 0));
        bool hf = (attr & 0x40) != 0;
        int pal = (attr >> 1) & 7;
        int tile = t->tile[j] & 0xFF;
        bool hi = (attr & 1) != 0;                 /* attr bit 0 selects $1xx */
        int sx = base_x + (dx + wob) * scale, sy = base_y + dy * scale;
        const uint32_t *pl = palette + 128 + pal * 16;
        /* $1xx: the theme's puffs (32 tiles); $0xx: the cloud puffs (64, NOTES 197) */
        const uint8_t *ta, *tb, *tc, *td;
        if (hi) { ta = fx->tiles[tile & 0x1F]; tb = fx->tiles[(tile + 1) & 0x1F]; tc = fx->tiles[(tile + 16) & 0x1F]; td = fx->tiles[(tile + 17) & 0x1F]; }
        else    { ta = fx->lo[tile & 0x3F];    tb = fx->lo[(tile + 1) & 0x3F];    tc = fx->lo[(tile + 16) & 0x3F];    td = fx->lo[(tile + 17) & 0x3F]; }
        draw_subtile(hf ? tb : ta, hf, sx, sy, scale, pl, fb, w, h);
        draw_subtile(hf ? ta : tb, hf, sx + 8 * scale, sy, scale, pl, fb, w, h);
        draw_subtile(hf ? td : tc, hf, sx, sy + 8 * scale, scale, pl, fb, w, h);
        draw_subtile(hf ? tc : td, hf, sx + 8 * scale, sy + 8 * scale, scale, pl, fb, w, h);
    }
}
