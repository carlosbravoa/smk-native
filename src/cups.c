/* Cups, and what each course is called - both derived from the ROM (NOTES 148).
 *
 * Two adjacent tables the project already had (NOTES 009):
 *
 *   $81EC1B  20 bytes, cup*5 + course -> track index.  This is the game's
 *            own indirection: $81EC47 computes $0124 = map[$0150*5+$0152],
 *            so selecting "Mushroom Cup, course 2" is exactly writing
 *            $0150/$0152 and letting this table pick the track.
 *   $81EC2F  24 bytes, track -> theme*2.
 *
 * The NAME of a course needs no third table.  Walk the cup order and group
 * by theme: each theme is one course FAMILY, and a family's courses are
 * numbered in the order the cups present them.  That reproduces the
 * printed line-up exactly and is checked in the selftest - every family
 * lands on the slot the real cup listing puts it in, and the count per
 * theme matches (Mario Circuit 4, Ghost Valley / Donut Plains / Bowser
 * Castle 3, Choco Island / Koopa Beach / Vanilla Lake 2, Rainbow Road 1).
 *
 * LABELLED: the eight family WORDS are English text, not ROM data - the
 * ROM's own name art is a tilemap of the word-tiles at font index 48+,
 * which we do not compose.  Their assignment to themes is not guesswork
 * though: it is forced by the cup order above, and theme 0 = Ghost Valley
 * is independently asserted by the breakable-block sequence in
 * src/blocks.c ($80FC70, the theme-0-only rail tiles).
 */
#include "smk.h"
#include <stdio.h>
#include <string.h>

#define T_CUP    0x81EC1Bu
#define T_THEME  0x81EC2Fu

const char *const SMK_CUP_NAMES[SMK_CUPS] = {
    "MUSHROOM CUP", "FLOWER CUP", "STAR CUP", "SPECIAL CUP"
};

/* theme -> course family.  Order forced by the cup table; see the header
 * comment.  These are the only invented STRINGS in this file. */
static const char *const FAMILY[SMK_THEME_COUNT] = {
    "GHOST VALLEY",   /* 0 */
    "MARIO CIRCUIT",  /* 1 */
    "DONUT PLAINS",   /* 2 */
    "CHOCO ISLAND",   /* 3 */
    "VANILLA LAKE",   /* 4 */
    "KOOPA BEACH",    /* 5 */
    "BOWSER CASTLE",  /* 6 */
    "RAINBOW ROAD",   /* 7 */
};

static uint8_t rd8(const smk_rom *rom, uint32_t snes)
{
    uint32_t pc = smk_snes_to_pc(rom, snes);
    return pc < rom->size ? rom->data[pc] : 0;
}

int smk_cup_track(const smk_rom *rom, int cup, int course)
{
    if (cup < 0 || cup >= SMK_CUPS || course < 0 || course >= SMK_CUP_COURSES)
        return -1;
    return rd8(rom, T_CUP + (uint32_t)(cup * SMK_CUP_COURSES + course));
}

const char *smk_track_name(const smk_rom *rom, int track)
{
    static char buf[32];
    if (track < 0 || track >= SMK_TRACK_COUNT) return "?";

    /* walk the cup order, numbering each theme's courses as they appear */
    int seen[SMK_THEME_COUNT] = { 0 };
    int total[SMK_THEME_COUNT] = { 0 };
    int ordinal = 0, theme = -1;
    for (int i = 0; i < SMK_CUPS * SMK_CUP_COURSES; i++)
        total[smk_track_theme(rom, rd8(rom, T_CUP + (uint32_t)i)) % SMK_THEME_COUNT]++;
    for (int i = 0; i < SMK_CUPS * SMK_CUP_COURSES; i++) {
        int t = rd8(rom, T_CUP + (uint32_t)i);
        int th = smk_track_theme(rom, t) % SMK_THEME_COUNT;
        seen[th]++;
        if (t == track) { ordinal = seen[th]; theme = th; break; }
    }
    if (theme < 0) {
        /* not in any cup: the four battle arenas, in track order.
         * LABELLED - the battle course order is not decoded here. */
        snprintf(buf, sizeof buf, "BATTLE COURSE %d",
                 track - (SMK_TRACK_COUNT - 4) + 1);
        return buf;
    }
    if (total[theme] <= 1) {
        snprintf(buf, sizeof buf, "%s", FAMILY[theme]);
        return buf;
    }
    snprintf(buf, sizeof buf, "%s %d", FAMILY[theme], ordinal);
    return buf;
}
