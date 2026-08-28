/* The menus' own text font and palette, read from the ROM (NOTES 147).
 *
 * DECODED by booting the Python oracle to the title screen and reading its
 * VRAM: the alphabet sits at VRAM tile $400 (4bpp), and only bitplanes 0
 * and 1 are ever set - the glyphs are 2bpp art uploaded into a 4bpp
 * region, which is why searching the ROM for the 32-byte form found
 * nothing.  Tracing the transfer back gives both halves:
 *
 *   $C7:0000   the font itself, 4096 bytes = 256 tiles of 2bpp.  The game
 *              decompresses it to WRAM $7F:4400, expands 2bpp -> 4bpp into
 *              $7F:A000 and DMAs 8192 bytes to VRAM word $4000.  Matched
 *              byte-for-byte against the running game's WRAM (4096/4096).
 *   $C7:1996   the menu screen, whose output's last 1024 bytes ($4000..)
 *              are TWO complete CGRAM sets, BGR555: 16 background
 *              palettes for a screen whose backdrop (colour 0) is the
 *              cream $FFEE94, then 16 more whose backdrop is the navy
 *              $000031.  The first set is the one the oracle's CGRAM
 *              holds at the title screen.
 *
 * Which pen is which, read off the sheet: pen 3 draws the glyph's
 * OUTLINE - both the outer edge and the ring around a counter, so an
 * eight-pixel '0' is outline, body, outline, body, outline across - and
 * pens 1 and 2 are the body, 1 over the top half and 2 over the bottom.
 * An outline the same colour as the body therefore does not merely look
 * flat: it floods the counters and the letters close up into blocks.
 *
 * Glyph order, read straight off the sheet:  0-9 at 0, A-Z at 10, then
 * ? . , ! ' " and a "cc" ligature (the game draws 50cc/100cc/150cc with
 * it), a couple of box corners, a solid block, and ':' at 46.  Everything
 * past 48 is whole WORDS baked as tiles ("LAP", "TIME", "COURSE SELECT")
 * which we compose from letters instead.
 */
#include "smk.h"
#include <string.h>

#define FONT_STREAM  0xC70000u
#define MENU_STREAM  0xC71996u
#define MENU_PAL_OFF 0x4000      /* palettes at the end of the menu stream */
#define MENU_PAL_LEN (SMK_FONT_PALS * 32)

static uint32_t bgr555(uint16_t v)
{
    unsigned r = v & 0x1F, g = (v >> 5) & 0x1F, b = (v >> 10) & 0x1F;
    r = (r * 255 + 15) / 31; g = (g * 255 + 15) / 31; b = (b * 255 + 15) / 31;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

bool smk_font_load(const smk_rom *rom, smk_font *f)
{
    static uint8_t buf[0x8000];
    memset(f, 0, sizeof *f);

    long n = smk_decompress_into(rom->data, rom->size,
                                 smk_snes_to_pc(rom, FONT_STREAM),
                                 buf, sizeof buf, 0, NULL);
    if (n < SMK_FONT_TILES * 16) return false;
    for (int t = 0; t < SMK_FONT_TILES; t++) {
        const uint8_t *s = buf + (size_t)t * 16u;
        for (int y = 0; y < 8; y++) {
            uint8_t lo = s[y * 2], hi = s[y * 2 + 1];
            for (int x = 0; x < 8; x++) {
                int b = 7 - x;
                f->px[t][y * 8 + x] =
                    (uint8_t)(((lo >> b) & 1) | (((hi >> b) & 1) << 1));
            }
        }
    }

    /* the menu's own palettes, from the tail of the screen's stream */
    long m = smk_decompress_into(rom->data, rom->size,
                                 smk_snes_to_pc(rom, MENU_STREAM),
                                 buf, sizeof buf, 0, NULL);
    if (m >= MENU_PAL_OFF + MENU_PAL_LEN) {
        for (int p = 0; p < SMK_FONT_PALS; p++)
            for (int c = 0; c < 16; c++) {
                const uint8_t *q = buf + MENU_PAL_OFF + p * 32 + c * 2;
                f->pal[p][c] = bgr555((uint16_t)(q[0] | (q[1] << 8)));
            }
        f->has_pal = true;
    }
    f->ok = true;
    return true;
}

int smk_font_glyph(int ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
    if (ch >= 'A' && ch <= 'Z') return 10 + (ch - 'A');
    switch (ch) {
    case '?':  return 36;
    case '.':  return 37;
    case ',':  return 38;
    case '!':  return 39;
    case '\'': return 40;
    case '"':  return 41;
    case ':':  return 46;
    default:   return -1;          /* space, and anything the sheet lacks */
    }
}

int smk_font_text_w(const char *s, int scale)
{
    return (int)strlen(s) * 8 * scale;
}

void smk_font_draw(const smk_font *f, uint32_t *fb, int w, int h,
                   int x, int y, const char *s, int scale, const uint32_t col[4])
{
    if (!f->ok || scale < 1) return;
    for (; *s; s++, x += 8 * scale) {
        int g = smk_font_glyph((unsigned char)*s);
        if (g < 0) continue;
        const uint8_t *px = f->px[g];
        for (int ty = 0; ty < 8 * scale; ty++) {
            int sy = y + ty;
            if (sy < 0 || sy >= h) continue;
            for (int tx = 0; tx < 8 * scale; tx++) {
                int sx = x + tx;
                if (sx < 0 || sx >= w) continue;
                uint8_t v = px[(ty / scale) * 8 + (tx / scale)];
                if (v) fb[(size_t)sy * (size_t)w + sx] = col[v];
            }
        }
    }
}
