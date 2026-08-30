/* The game shell: title, mode, driver, course, results.
 *
 * Everything the player READS here is the ROM's own font ($C7:0000, see
 * src/font.c) in the ROM's own menu palette, and every course name and cup
 * line-up comes from the ROM's tables (src/cups.c).  What is OURS, and
 * says so in the ledger, is the LAYOUT: the real screens are BG tilemaps
 * with Lakitu, the map preview and the animated cursor, none of which is
 * decoded.  So this draws the same information in the same words, not the
 * same picture.
 */
#include "smk.h"
#include <stdio.h>
#include <string.h>

/* the SNES screen is 256x224; everything below is placed in those units
 * and scaled to whatever the window is */
#define VW 256
#define VH 224

static int scale_for(int w) { int s = w / VW; return s < 1 ? 1 : s; }

/* Colour ramps for the 2bpp font.  Index 1/2 are the glyph's body, index
 * 3 its outline (src/font.c); the ROM's menu palettes supply all three.
 *
 * WHICH palette matters.  The stream carries two CGRAM sets: the first
 * sixteen belong to a cream-backdrop screen and their text pens are all
 * white or a hair off it - palette 0 is white body over a $F6F6FF
 * outline - so drawing with those makes the outline as bright as the
 * body, and because the outline is also the ring around every counter
 * the letters flood solid and stop being letters.  The second sixteen
 * are the navy-backdrop set, and there the outline is a real colour
 * against a white body: TEXT_PAL 16 is white edged in blue, TEXT_HI 1 is
 * the one light-set palette that does keep a distinct (gold) outline.
 * Our backdrop below is that same navy, so this pairs the pens with the
 * field they were drawn for. */
#define TEXT_PAL 16              /* white body, $0073FF outline  */
#define TEXT_HI   1              /* white body, $F6CD83 outline  */

static void ramp(const smk_font *f, int pal, uint32_t out[4],
                 uint32_t fallback1, uint32_t fallback3)
{
    out[0] = 0;
    if (f->has_pal) {
        out[1] = f->pal[pal & (SMK_FONT_PALS - 1)][1];
        out[2] = f->pal[pal & (SMK_FONT_PALS - 1)][2];
        out[3] = f->pal[pal & (SMK_FONT_PALS - 1)][3];
    } else {
        out[1] = fallback1; out[2] = fallback1; out[3] = fallback3;
    }
}

/* An unavailable row still has to be readable, so this pulls the pen
 * toward the background rather than toward black. */
static void dim(uint32_t c[4])
{
    for (int i = 1; i < 4; i++) {
        uint32_t v = c[i];
        unsigned r = (v >> 16) & 255, g = (v >> 8) & 255, b = v & 255;
        r = r * 42 / 100 + 26; g = g * 42 / 100 + 26; b = b * 42 / 100 + 34;
        c[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
}

static void text(const smk_font *f, uint32_t *fb, int w, int h,
                 int vx, int vy, const char *s, const uint32_t col[4])
{
    int sc = scale_for(w);
    smk_font_draw(f, fb, w, h, vx * sc, vy * sc, s, sc, col);
}

static void text_c(const smk_font *f, uint32_t *fb, int w, int h,
                   int vy, const char *s, const uint32_t col[4])
{
    text(f, fb, w, h, (VW - (int)strlen(s) * 8) / 2, vy, s, col);
}

/* A translucent panel: the alpha byte is the mix, done here because the
 * framebuffer is opaque ARGB and nothing downstream blends. */
static void fill(uint32_t *fb, int w, int h, int vx, int vy, int vw, int vh,
                 uint32_t c)
{
    int sc = scale_for(w);
    unsigned a = (c >> 24) & 255;
    unsigned cr = (c >> 16) & 255, cg = (c >> 8) & 255, cb = c & 255;
    for (int y = vy * sc; y < (vy + vh) * sc; y++) {
        if (y < 0 || y >= h) continue;
        for (int x = vx * sc; x < (vx + vw) * sc; x++) {
            if (x < 0 || x >= w) continue;
            uint32_t d = fb[(size_t)y * (size_t)w + x];
            unsigned r = ((d >> 16) & 255) * (255 - a) / 255 + cr * a / 255;
            unsigned g = ((d >> 8) & 255) * (255 - a) / 255 + cg * a / 255;
            unsigned b = (d & 255) * (255 - a) / 255 + cb * a / 255;
            fb[(size_t)y * (size_t)w + x] =
                0xFF000000u | (r << 16) | (g << 8) | b;
        }
    }
}

/* The field behind the text.  LABELLED: ours.  It is the navy the second
 * CGRAM set was drawn against (colour 0 there is $000031), which is what
 * TEXT_PAL's white body and blue outline expect behind them; the actual
 * backdrop the game draws is a tilemap we have not decoded. */
static void backdrop(uint32_t *fb, int w, int h, const smk_font *f)
{
    (void)f;
    for (int y = 0; y < h; y++) {
        unsigned k = 100 + (unsigned)(60 * y / (h ? h : 1));
        unsigned r = 12 * k / 100, g = 16 * k / 100, b = 40 * k / 100;
        uint32_t c = 0xFF000000u | (r << 16) | (g << 8) | b;
        for (int x = 0; x < w; x++) fb[(size_t)y * (size_t)w + x] = c;
    }
}

void smk_ui_init(smk_ui *ui)
{
    memset(ui, 0, sizeof *ui);
    ui->screen = SMK_UI_TITLE;
    ui->mode_sel = SMK_UI_MODE_RACE;
    ui->engine_class = 0;        /* 50cc */
    ui->track = -1;
}

/* ---- navigation ---------------------------------------------------- */

#define PLAYER_CLASS_ROW 8       /* cursor value for the engine-class row */

bool smk_ui_step(smk_ui *ui, const smk_rom *rom, const smk_ui_input *in)
{
    ui->tick++;
    if (ui->denied_t > 0) ui->denied_t--;

    switch (ui->screen) {
    case SMK_UI_TITLE:
        if (in->confirm) ui->screen = SMK_UI_MODE;
        break;

    case SMK_UI_MODE:
        if (in->up)   ui->mode_sel = (ui->mode_sel + SMK_UI_MODES - 1) % SMK_UI_MODES;
        if (in->down) ui->mode_sel = (ui->mode_sel + 1) % SMK_UI_MODES;
        if (in->back) ui->screen = SMK_UI_TITLE;
        if (in->confirm) {
            ui->gp = (ui->mode_sel == SMK_UI_MODE_GP);
            ui->screen = SMK_UI_PLAYER;
        }
        break;

    case SMK_UI_PLAYER: {
        int p = ui->player_sel;
        if (p == PLAYER_CLASS_ROW) {
            if (in->left)  ui->engine_class = (ui->engine_class + 2) % 3;
            if (in->right) ui->engine_class = (ui->engine_class + 1) % 3;
            if (in->up)    ui->player_sel = 4;
        } else {
            if (in->left)  ui->player_sel = (p & 4) | ((p + 3) & 3);
            if (in->right) ui->player_sel = (p & 4) | ((p + 1) & 3);
            if (in->up)    ui->player_sel = p < 4 ? p : p - 4;
            if (in->down)  ui->player_sel = p < 4 ? p + 4 : PLAYER_CLASS_ROW;
        }
        if (in->back)    ui->screen = SMK_UI_MODE;
        if (in->confirm) ui->screen = SMK_UI_COURSE;
        break;
    }

    case SMK_UI_COURSE:
        if (in->left)  ui->cup_sel = (ui->cup_sel + SMK_CUPS - 1) % SMK_CUPS;
        if (in->right) ui->cup_sel = (ui->cup_sel + 1) % SMK_CUPS;
        if (ui->gp) ui->course_sel = 0;              /* a cup starts at its first course */
        else {
            if (in->up)    ui->course_sel = (ui->course_sel + SMK_CUP_COURSES - 1) % SMK_CUP_COURSES;
            if (in->down)  ui->course_sel = (ui->course_sel + 1) % SMK_CUP_COURSES;
        }
        if (in->back)  ui->screen = SMK_UI_PLAYER;
        if (in->confirm) {
            if (ui->gp) {
                ui->gp_race = 0; ui->ranked_out = false;
                for (int i = 0; i < SMK_CHARACTERS; i++) { ui->gp_points[i] = 0; ui->gp_place[i] = 0; }
                for (int i = 0; i < 4; i++) {
                    uint32_t pc = smk_snes_to_pc(rom, 0x85BEB4u + (uint32_t)i * 2u);
                    ui->gp_pts_table[i] = pc + 1 < rom->size ? (rom->data[pc] | rom->data[pc + 1] << 8) : 0;
                }
            }
            ui->track = smk_cup_track(rom, ui->cup_sel, ui->course_sel);
            ui->screen = SMK_UI_RACE;
            return true;
        }
        break;

    case SMK_UI_RACE:
        break;

    case SMK_UI_RESULT:
        if (in->confirm || in->back) ui->screen = ui->gp ? SMK_UI_STANDINGS : SMK_UI_COURSE;
        break;

    case SMK_UI_STANDINGS:
        if (in->back) { ui->gp = false; ui->screen = SMK_UI_COURSE; break; }
        if (in->confirm) {
            if (ui->ranked_out) {                       /* the same course again */
                ui->ranked_out = false;
                ui->screen = SMK_UI_RACE;
                return true;
            }
            if (ui->gp_race + 1 < SMK_CUP_COURSES) {    /* the next course */
                ui->gp_race++;
                ui->track = smk_cup_track(rom, ui->cup_sel, ui->gp_race);
                ui->screen = SMK_UI_RACE;
                return true;
            }
            ui->gp = false;                             /* the cup is done */
            ui->screen = SMK_UI_COURSE;
        }
        break;
    }
    return false;
}

/* After a race in a cup: the ROM's points to the top four, by driver, and
 * whether the player must run the course again. */
void smk_ui_gp_award(smk_ui *ui, const smk_ui_result *res)
{
    if (!ui->gp) return;
    ui->ranked_out = res->position > 4;
    for (int p = 0; p < res->entries && p < SMK_CHARACTERS; p++) {
        int ch = res->field[p].character % SMK_CHARACTERS;
        ui->gp_place[ch] = p + 1;
        if (!ui->ranked_out && p < 4) ui->gp_points[ch] += ui->gp_pts_table[p];
    }
}

/* ---- the driver line-up -------------------------------------------- */

static const smk_sprites *driver_art(const smk_rom *rom, int who)
{
    static smk_sprites cache[SMK_CHARACTERS];
    static int loaded[SMK_CHARACTERS];
    if (who < 0 || who >= SMK_CHARACTERS) return NULL;
    if (!loaded[who])
        loaded[who] = smk_sprites_load(rom, SMK_DRIVERS[who].sheet, &cache[who]) ? 1 : -1;
    return loaded[who] == 1 ? &cache[who] : NULL;
}

static const char *class_name(int c)
{
    static const char *const N[3] = { "50cc", "100cc", "150cc" };
    return N[c % 3];
}

/* ---- screens -------------------------------------------------------- */

static void draw_title(const smk_ui *ui, const smk_font *f,
                       uint32_t *fb, int w, int h)
{
    uint32_t hi[4], lo[4];
    ramp(f, TEXT_HI, hi, 0xFFFFFFFF, 0xFF7A5A18);
    ramp(f, TEXT_PAL, lo, 0xFFFFFFFF, 0xFF2A3E78);
    text_c(f, fb, w, h, 60, "SUPER MARIO KART", hi);
    text_c(f, fb, w, h, 76, "A NATIVE PORT", lo);
    if ((ui->tick / 30) & 1)
        text_c(f, fb, w, h, 140, "PRESS ENTER", hi);
    text_c(f, fb, w, h, 200, "READS YOUR OWN ROM", lo);
}

static void draw_mode(const smk_ui *ui, const smk_font *f,
                      uint32_t *fb, int w, int h)
{
    uint32_t hi[4], lo[4], off[4], sel[4];
    ramp(f, TEXT_HI, hi, 0xFFFFFFFF, 0xFF7A5A18);
    ramp(f, TEXT_PAL, lo, 0xFFFFFFFF, 0xFF2A3E78);
    ramp(f, TEXT_PAL, off, 0xFFFFFFFF, 0xFF2A3E78); dim(off);
    ramp(f, TEXT_HI, sel, 0xFFFFFFFF, 0xFF7A5A18);

    text_c(f, fb, w, h, 40, "SELECT MODE", hi);
    const char *row[SMK_UI_MODES] = { "GRAND PRIX", "SINGLE RACE", "TIME TRIAL" };
    for (int i = 0; i < SMK_UI_MODES; i++) {
        int y = 80 + i * 24;
        const uint32_t *c = (i == SMK_UI_MODE_GP) ? off
                          : (ui->mode_sel == i ? sel : lo);
        int x = (VW - (int)strlen(row[i]) * 8) / 2;
        if (ui->mode_sel == i && ((ui->tick / 12) & 1) == 0)
            fill(fb, w, h, x - 12, y - 2, 8, 12, 0xFFFFC040);
        text(f, fb, w, h, x, y, row[i], c);
    }
    text_c(f, fb, w, h, 200, "ENTER SELECT   ESC BACK", lo);
}

static void draw_player(const smk_ui *ui, const smk_rom *rom, const smk_font *f,
                        const uint32_t *palette, uint32_t *fb, int w, int h)
{
    uint32_t hi[4], lo[4], sel[4];
    ramp(f, TEXT_HI, hi, 0xFFFFFFFF, 0xFF7A5A18);
    ramp(f, TEXT_PAL, lo, 0xFFFFFFFF, 0xFF2A3E78);
    ramp(f, TEXT_HI, sel, 0xFFFFFFFF, 0xFF7A5A18);
    int sc = scale_for(w);

    text_c(f, fb, w, h, 16, "SELECT DRIVER", hi);
    for (int i = 0; i < SMK_CHARACTERS; i++) {
        int col = i & 3, row = i >> 2;
        int vx = 24 + col * 54, vy = 44 + row * 62;
        bool on = ui->player_sel == i;
        if (on) fill(fb, w, h, vx - 4, vy - 4, 48, 56, 0x40FFFFFF);
        const smk_sprites *s = driver_art(rom, i);
        if (s && palette)
            smk_draw_sprite(s, SMK_SPR_REAR, palette, SMK_DRIVERS[i].pal,
                            (vx + 20) * sc, (vy + 20) * sc, sc, false,
                            fb, w, h, w);
        char nm[16];
        snprintf(nm, sizeof nm, "%s", SMK_DRIVERS[i].name);
        for (char *p = nm; *p; p++)
            if (*p >= 'a' && *p <= 'z') *p -= 'a' - 'A';
        text(f, fb, w, h, vx + 20 - (int)strlen(nm) * 4, vy + 40, nm,
             on ? sel : lo);
    }
    /* the engine class - the ROM's own "cc" ligature is glyph 42, but we
     * spell it with letters so the row reads at any scale */
    bool on = ui->player_sel == PLAYER_CLASS_ROW;
    text(f, fb, w, h, 40, 176, "CLASS", on ? hi : lo);
    for (int c = 0; c < 3; c++) {
        const char *n = class_name(c);
        int vx = 96 + c * 44;
        bool cur = ui->engine_class == c;
        if (cur) fill(fb, w, h, vx - 3, 174, (int)strlen(n) * 8 + 6, 12,
                      on ? 0x60FFC040 : 0x30FFFFFF);
        text(f, fb, w, h, vx, 176, n, cur ? sel : lo);
    }
    text_c(f, fb, w, h, 202, "ENTER SELECT   ESC BACK", lo);
}

static void draw_course(const smk_ui *ui, const smk_rom *rom, const smk_font *f,
                        const smk_records *rec, uint32_t *fb, int w, int h)
{
    uint32_t hi[4], lo[4], sel[4], off[4];
    ramp(f, TEXT_HI, hi, 0xFFFFFFFF, 0xFF7A5A18);
    ramp(f, TEXT_PAL, lo, 0xFFFFFFFF, 0xFF2A3E78);
    ramp(f, TEXT_HI, sel, 0xFFFFFFFF, 0xFF7A5A18);
    ramp(f, TEXT_PAL, off, 0xFFFFFFFF, 0xFF2A3E78); dim(off);

    text_c(f, fb, w, h, 10, "COURSE SELECT", hi);

    /* the four cups down the left */
    for (int c = 0; c < SMK_CUPS; c++) {
        char nm[24];
        snprintf(nm, sizeof nm, "%s", SMK_CUP_NAMES[c]);
        char *sp = strchr(nm, ' ');
        if (sp) *sp = 0;                     /* just "MUSHROOM", "FLOWER" .. */
        int vy = 40 + c * 18;
        if (c == ui->cup_sel) fill(fb, w, h, 6, vy - 2, 76, 12, 0x40FFFFFF);
        text(f, fb, w, h, 10, vy, nm, c == ui->cup_sel ? sel : off);
    }

    /* the selected cup's five courses */
    for (int i = 0; i < SMK_CUP_COURSES; i++) {
        int t = smk_cup_track(rom, ui->cup_sel, i);
        int vy = 40 + i * 16;
        bool on = i == ui->course_sel;
        if (on) fill(fb, w, h, 92, vy - 2, 158, 12, 0x40FFC040);
        char line[40];
        snprintf(line, sizeof line, "%d %s", i + 1, smk_track_name(rom, t));
        text(f, fb, w, h, 96, vy, line, on ? sel : lo);
    }

    /* the top five laps for whatever is highlighted */
    int t = smk_cup_track(rom, ui->cup_sel, ui->course_sel);
    text(f, fb, w, h, 20, 132, "BEST LAPS", hi);
    for (int s = 0; s < SMK_RECORD_SLOTS; s++) {
        char line[48], tm[16];
        const smk_record *r = &rec->best[t < 0 ? 0 : t][s];
        smk_time_text(r->frames, tm, sizeof tm);
        if (r->frames > 0) {
            char nm[16];
            snprintf(nm, sizeof nm, "%s", SMK_DRIVERS[r->character % SMK_CHARACTERS].name);
            for (char *p = nm; *p; p++)
                if (*p >= 'a' && *p <= 'z') *p -= 'a' - 'A';
            snprintf(line, sizeof line, "%d  %s  %s", s + 1, tm, nm);
        } else {
            snprintf(line, sizeof line, "%d  %s", s + 1, tm);
        }
        text(f, fb, w, h, 24, 148 + s * 12, line, r->frames > 0 ? lo : off);
    }
    char foot[48];
    snprintf(foot, sizeof foot, "%s   %s   %d LAPS",
             SMK_DRIVERS[ui->player_sel % SMK_CHARACTERS].name,
             class_name(ui->engine_class), SMK_RACE_LAPS);
    for (char *p = foot; *p; p++)
        if (*p >= 'a' && *p <= 'z') *p -= 'a' - 'A';
    text_c(f, fb, w, h, 210, foot, hi);
}

void smk_ui_draw(const smk_ui *ui, const smk_rom *rom, const smk_font *f,
                 const smk_records *rec, const uint32_t *palette,
                 uint32_t *fb, int w, int h)
{
    if (!f->ok) return;
    backdrop(fb, w, h, f);
    switch (ui->screen) {
    case SMK_UI_TITLE:  draw_title(ui, f, fb, w, h); break;
    case SMK_UI_MODE:   draw_mode(ui, f, fb, w, h); break;
    case SMK_UI_PLAYER: draw_player(ui, rom, f, palette, fb, w, h); break;
    case SMK_UI_COURSE: draw_course(ui, rom, f, rec, fb, w, h); break;
    default: break;
    }
}

void smk_ui_draw_result(const smk_ui *ui, const smk_rom *rom, const smk_font *f,
                        const smk_records *rec, const smk_ui_result *res,
                        uint32_t *fb, int w, int h)
{
    if (!f->ok) return;
    backdrop(fb, w, h, f);
    uint32_t hi[4], lo[4], gold[4], off[4];
    ramp(f, TEXT_HI, hi, 0xFFFFFFFF, 0xFF7A5A18);
    ramp(f, TEXT_PAL, lo, 0xFFFFFFFF, 0xFF2A3E78);
    ramp(f, TEXT_HI, gold, 0xFFFFFFFF, 0xFF7A5A18);
    ramp(f, TEXT_PAL, off, 0xFFFFFFFF, 0xFF2A3E78); dim(off);

    bool race = res->position > 0;
    text_c(f, fb, w, h, 10, race ? (ui->gp ? "GRAND PRIX" : "SINGLE RACE") : "TIME TRIAL", hi);
    text_c(f, fb, w, h, 24, smk_track_name(rom, ui->track), gold);

    if (race && res->entries > 0) {
        /* THE FIELD, which is what the user asked for: "you get times:
         * your times, and the AI's total times and positions after the
         * race".  Laid out for the room we have rather than for the
         * original's - "faithful is for driving experience, not for hud,
         * menus, and things that can be better without constraints".
         * The ART is still the ROM's: its font, its palettes, its
         * character names and its own time formatting. */
        static const char *const ORD[SMK_CHARACTERS] = {
            "1ST", "2ND", "3RD", "4TH", "5TH", "6TH", "7TH", "8TH" };
        int p = res->position - 1;
        if (p < 0) p = 0;
        if (p >= SMK_CHARACTERS) p = SMK_CHARACTERS - 1;
        char line[40];
        snprintf(line, sizeof line, "%s OF %d", ORD[p], SMK_CHARACTERS);
        text_c(f, fb, w, h, 38, line, p == 0 ? gold : hi);

        for (int i = 0; i < res->entries && i < SMK_CHARACTERS; i++) {
            int y = 58 + i * 12;
            const uint32_t *col = res->field[i].player ? gold
                                : i < 4 ? lo : off;
            char nm[16], tm[16];
            snprintf(nm, sizeof nm, "%s",
                     SMK_DRIVERS[res->field[i].character % SMK_CHARACTERS].name);
            for (char *q = nm; *q; q++)
                if (*q >= 'a' && *q <= 'z') *q -= 32;
            snprintf(line, sizeof line, "%d", i + 1);
            text(f, fb, w, h, 36, y, line, col);
            text(f, fb, w, h, 56, y, nm, col);
            if (res->field[i].total >= 0) {
                smk_time_text(res->field[i].total, tm, sizeof tm);
                text(f, fb, w, h, 152, y, tm, col);
            } else {
                text(f, fb, w, h, 152, y, "DNF", off);
            }
        }

        /* and the player's own laps underneath, in two columns */
        for (int i = 0; i < SMK_RACE_LAPS + 1; i++) {
            int col_ = i / 3, row = i % 3;
            int x = 24 + col_ * 116, y = 160 + row * 12;
            char tm[16];
            long v = i < SMK_RACE_LAPS ? res->lap[i] : res->total;
            smk_time_text(v, tm, sizeof tm);
            if (i < SMK_RACE_LAPS) snprintf(line, sizeof line, "L%d %s", i + 1, tm);
            else                   snprintf(line, sizeof line, "TOTAL %s", tm);
            bool best = i < SMK_RACE_LAPS && res->lap[i] > 0
                        && res->lap[i] == res->best_lap;
            text(f, fb, w, h, x, y, line,
                 i == SMK_RACE_LAPS ? hi : v > 0 ? (best ? gold : lo) : off);
        }
    } else {
        int y0 = 52;
        for (int i = 0; i < SMK_RACE_LAPS; i++) {
            char line[40], tm[16];
            smk_time_text(res->lap[i], tm, sizeof tm);
            snprintf(line, sizeof line, "LAP %d   %s", i + 1, tm);
            bool best = res->lap[i] > 0 && res->lap[i] == res->best_lap;
            text(f, fb, w, h, 60, y0 + i * 14, line,
                 res->lap[i] > 0 ? (best ? gold : lo) : off);
        }
        {
            char line[40], tm[16];
            smk_time_text(res->total, tm, sizeof tm);
            snprintf(line, sizeof line, "TOTAL   %s", tm);
            text(f, fb, w, h, 60, y0 + SMK_RACE_LAPS * 14 + 8, line, hi);
        }
    }
    if (race) {
        /* a race banks nothing: the lap table is the time trial's */
    } else if (res->best_slot >= 0) {
        char line[40];
        snprintf(line, sizeof line, "NEW RECORD   NO %d", res->best_slot + 1);
        if ((ui->tick / 15) & 1) text_c(f, fb, w, h, 156, line, gold);
    } else {
        text_c(f, fb, w, h, 156, "NO NEW RECORD", off);
    }
    (void)rec;
    text_c(f, fb, w, h, 200, "ENTER CONTINUE", lo);
}

void smk_ui_draw_standings(const smk_ui *ui, const smk_rom *rom, const smk_font *f,
                           uint32_t *fb, int w, int h)
{
    if (!f->ok) return;
    backdrop(fb, w, h, f);
    uint32_t hi[4], lo[4], gold[4], off[4];
    ramp(f, TEXT_HI, hi, 0xFFFFFFFF, 0xFF7A5A18);
    ramp(f, TEXT_PAL, lo, 0xFFFFFFFF, 0xFF2A3E78);
    ramp(f, TEXT_HI, gold, 0xFFFFFFFF, 0xFF7A5A18);
    ramp(f, TEXT_PAL, off, 0xFFFFFFFF, 0xFF2A3E78); dim(off);
    char line[48];
    snprintf(line, sizeof line, "GRAND PRIX   RACE %d OF %d", ui->gp_race + 1, SMK_CUP_COURSES);
    text_c(f, fb, w, h, 10, line, hi);
    text_c(f, fb, w, h, 24, smk_track_name(rom, ui->track), gold);
    text_c(f, fb, w, h, 38, ui->ranked_out ? "RANKED OUT   TRY AGAIN"
                          : ui->gp_race + 1 < SMK_CUP_COURSES ? "STANDINGS" : "FINAL STANDINGS",
           ui->ranked_out ? off : hi);
    /* by points, then by this race's place */
    int order[SMK_CHARACTERS];
    for (int i = 0; i < SMK_CHARACTERS; i++) order[i] = i;
    for (int i = 1; i < SMK_CHARACTERS; i++) {
        int v = order[i], j = i - 1;
        for (; j >= 0; j--) {
            int a = order[j];
            bool worse = ui->gp_points[a] < ui->gp_points[v]
                      || (ui->gp_points[a] == ui->gp_points[v] && ui->gp_place[a] > ui->gp_place[v]);
            if (!worse) break;
            order[j + 1] = order[j];
        }
        order[j + 1] = v;
    }
    for (int i = 0; i < SMK_CHARACTERS; i++) {
        int ch = order[i]; int y = 58 + i * 12;
        bool me = ch == ui->player_sel;
        const uint32_t *col = me ? gold : lo;
        char nm[16];
        snprintf(nm, sizeof nm, "%s", SMK_DRIVERS[ch].name);
        for (char *q = nm; *q; q++) if (*q >= 'a' && *q <= 'z') *q -= 32;
        snprintf(line, sizeof line, "%d", i + 1);           text(f, fb, w, h, 36, y, line, col);
        text(f, fb, w, h, 56, y, nm, col);
        snprintf(line, sizeof line, "%2d PTS", ui->gp_points[ch]); text(f, fb, w, h, 132, y, line, col);
        if (ui->gp_place[ch] > 0) { snprintf(line, sizeof line, "%dTH", ui->gp_place[ch]);
            if (ui->gp_place[ch] == 1) snprintf(line, sizeof line, "1ST"); else if (ui->gp_place[ch] == 2) snprintf(line, sizeof line, "2ND"); else if (ui->gp_place[ch] == 3) snprintf(line, sizeof line, "3RD");
            text(f, fb, w, h, 196, y, line, i < 4 ? col : off); }
    }
    text_c(f, fb, w, h, 200, ui->ranked_out ? "ENTER RETRY" : ui->gp_race + 1 < SMK_CUP_COURSES ? "ENTER NEXT COURSE" : "ENTER CONTINUE", lo);
}

bool smk_tt_crossing(smk_ui_result *res, int *crossings, long *lap_start,
                     long now)
{
    (*crossings)++;
    if (*crossings <= 1) return false;         /* leaving the grid */
    int lap = *crossings - 2;
    if (lap >= 0 && lap < SMK_RACE_LAPS) {
        long t = now - *lap_start;
        res->lap[lap] = t;
        res->laps_done = lap + 1;
        if (res->best_lap == 0 || t < res->best_lap) res->best_lap = t;
    }
    *lap_start = now;
    if (*crossings >= SMK_RACE_LAPS + 1) {
        res->total = now;
        return true;
    }
    return false;
}

void smk_ui_draw_splits(const smk_font *f, const smk_ui_result *res,
                        long cur_lap_frames, int lap, bool mushroom,
                        uint32_t *fb, int w, int h)
{
    if (!f->ok) return;
    uint32_t hi[4], lo[4], gold[4];
    ramp(f, TEXT_HI, hi, 0xFFFFFFFF, 0xFF7A5A18);
    ramp(f, TEXT_PAL, lo, 0xFFFFFFFF, 0xFF2A3E78);
    ramp(f, TEXT_HI, gold, 0xFFFFFFFF, 0xFF7A5A18);
    char tm[16], line[40];

    /* the running lap, under the race clock the HUD already draws */
    smk_time_text(cur_lap_frames, tm, sizeof tm);
    snprintf(line, sizeof line, "LAP %s", tm);
    text(f, fb, w, h, 8, 26, line, hi);

    /* The item.  LABELLED: the game draws its item BOX at the top centre
     * with the mushroom's own sprite; that art is not decoded here, so
     * the word stands in for it. */
    if (mushroom) text(f, fb, w, h, VW - 8 - 8 * 8, 44, "MUSHROOM", gold);

    /* the splits so far */
    for (int i = 0; i < SMK_RACE_LAPS && i < lap; i++) {
        if (res->lap[i] <= 0) continue;
        smk_time_text(res->lap[i], tm, sizeof tm);
        snprintf(line, sizeof line, "%d %s", i + 1, tm);
        bool best = res->lap[i] == res->best_lap;
        text(f, fb, w, h, 8, 40 + i * 10, line, best ? gold : lo);
    }
}
