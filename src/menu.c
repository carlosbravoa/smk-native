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

/* WHERE THE 256x224 SCREEN SITS IN THE WINDOW.
 *
 * Every screen below is laid out in the SNES's own 256x224 and scaled up.
 * The scale used to come from the width alone and the origin was always
 * (0, 0), which is right for a 8:7 window and wrong for anything else: on
 * a widescreen fullscreen the menu hugged the left edge (the user), and a
 * scale taken from the width can also run off the bottom.  So: the
 * largest whole scale that fits BOTH ways, and the result centred.
 *
 * The in-race panels (the splits) are NOT centred - they belong to the
 * race view's own top-left corner, beside the HUD - so they set the
 * origin to zero through layout(..., false). */
static int ui_sc = 1, ui_ox, ui_oy;

static int scale_for(int w, int h)
{
    int s = w / VW, t = h / VH;
    if (t < s) s = t;
    return s < 1 ? 1 : s;
}

static void layout(int w, int h, bool centred)
{
    if (!centred) {
        /* an in-race panel shares the HUD's own scale, which main.c takes
         * from the width alone - so it stays beside it whatever shape the
         * window is */
        ui_sc = w / VW;
        if (ui_sc < 1) ui_sc = 1;
        ui_ox = ui_oy = 0;
        return;
    }
    ui_sc = scale_for(w, h);
    ui_ox = (w - VW * ui_sc) / 2;
    ui_oy = (h - VH * ui_sc) / 2;
    if (ui_ox < 0) ui_ox = 0;
    if (ui_oy < 0) ui_oy = 0;
}

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
    smk_font_draw(f, fb, w, h, ui_ox + vx * ui_sc, ui_oy + vy * ui_sc,
                  s, ui_sc, col);
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
    unsigned a = (c >> 24) & 255;
    unsigned cr = (c >> 16) & 255, cg = (c >> 8) & 255, cb = c & 255;
    for (int y = ui_oy + vy * ui_sc; y < ui_oy + (vy + vh) * ui_sc; y++) {
        if (y < 0 || y >= h) continue;
        for (int x = ui_ox + vx * ui_sc; x < ui_ox + (vx + vw) * ui_sc; x++) {
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
/* ---- the dressing (OURS, S20) --------------------------------------
 *
 * The user: "everything that is menus and outside racing is our own.
 * While it should feel 16-bit Mario Kart themed, we can still add a lot
 * of fun and glare."  So: the navy field the ROM's pens were drawn for,
 * with slow diagonal bands drifting across it, a scrolling checkered
 * ribbon top and bottom, sparkles, gold that shimmers, medal pens for the
 * podium, confetti and a trophy.  All of it is computed from the frame
 * counter - nothing here keeps state - and none of it is ROM art except
 * the font and the kart sprites. */
static const smk_sprites *driver_art(const smk_rom *rom, int who);
static void kart_side_clipped(const smk_rom *rom, const uint32_t *palette, int who,
                              int vx, int vy, int cx0, int cx1,
                              uint32_t *fb, int w, int h);

static uint32_t hash32(uint32_t x)
{
    x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
    return x;
}

/* 0..1 ease-out, for things that arrive and settle */
static float ease(int t, int len)
{
    if (t <= 0) return 0.0f;
    if (t >= len) return 1.0f;
    float u = 1.0f - (float)t / (float)len;
    return 1.0f - u * u * u;
}

static uint32_t mix(uint32_t a, uint32_t b, int k)      /* k 0..255 toward b */
{
    unsigned r = (((a >> 16) & 255) * (255 - k) + ((b >> 16) & 255) * k) / 255;
    unsigned g = (((a >> 8) & 255) * (255 - k) + ((b >> 8) & 255) * k) / 255;
    unsigned bl = ((a & 255) * (255 - k) + (b & 255) * k) / 255;
    return 0xFF000000u | (r << 16) | (g << 8) | bl;
}

static void backdrop(uint32_t *fb, int w, int h, const smk_font *f, unsigned tick)
{
    (void)f;
    /* the field: navy, darker at the top, with soft diagonal bands that
     * drift down-right - a 16-bit "parallax" without a second layer */
    int band = 24 * ui_sc;
    int drift = (int)(tick / 2) % (2 * band);
    for (int y = 0; y < h; y++) {
        unsigned k = 100 + (unsigned)(60 * y / (h ? h : 1));
        unsigned r = 12 * k / 100, g = 16 * k / 100, b = 40 * k / 100;
        uint32_t c0 = 0xFF000000u | (r << 16) | (g << 8) | b;
        uint32_t c1 = 0xFF000000u | ((r + 3) << 16) | ((g + 4) << 8) | (b + 9);
        uint32_t *row = fb + (size_t)y * (size_t)w;
        int phase = (y - drift) % (2 * band);
        if (phase < 0) phase += 2 * band;
        for (int x = 0; x < w; x++) {
            int p = phase + x;
            row[x] = ((p / band) & 1) ? c1 : c0;
        }
    }
    /* the checkered ribbon, top and bottom, rolling to the left */
    {
        int sq = 6;
        int shift = (int)(tick / 4) % (2 * sq);
        for (int rowi = 0; rowi < 2; rowi++) {
            int vy = rowi ? VH - sq : 0;
            for (int vx = -2 * sq; vx < VW + 2 * sq; vx += sq) {
                int cell = ((vx + 2 * sq) / sq + rowi) & 1;
                fill(fb, w, h, vx - shift, vy, sq, sq, cell ? 0xFFF0F0F0 : 0xFF202030);
            }
        }
    }
    /* sparkles: fixed places, each on its own blink */
    for (unsigned i = 0; i < 40; i++) {
        uint32_t hv = hash32(i * 2654435761u);
        int vx = (int)(hv % VW), vy = 8 + (int)((hv >> 9) % (VH - 16));
        unsigned period = 70 + (hv >> 20) % 50;
        unsigned ph = (tick + (hv >> 13)) % period;
        if (ph > 12) continue;
        int k = ph < 6 ? (int)ph : 12 - (int)ph;         /* 0..6..0 */
        uint32_t c = mix(0xFF404880, 0xFFFFFFFF, k * 42);
        fill(fb, w, h, vx, vy, 1, 1, c);
        if (k >= 3) {
            fill(fb, w, h, vx - 1, vy, 1, 1, c); fill(fb, w, h, vx + 1, vy, 1, 1, c);
            fill(fb, w, h, vx, vy - 1, 1, 1, c); fill(fb, w, h, vx, vy + 1, 1, 1, c);
        }
    }
}

/* gold whose body pens breathe between white and yellow: the ROM's TEXT_HI
 * outline, our shimmer */
static void shimmer(const smk_font *f, uint32_t out[4], unsigned tick)
{
    ramp(f, TEXT_HI, out, 0xFFFFFFFF, 0xFF7A5A18);
    unsigned ph = (tick * 5) & 255;
    int k = ph < 128 ? (int)ph * 2 : (255 - (int)ph) * 2;
    out[1] = mix(0xFFFFFFFF, 0xFFFFE070, k);
    out[2] = mix(0xFFFFF4C0, 0xFFFFC020, k);
}

/* the podium pens: gold is the ROM's own TEXT_HI; silver and bronze are
 * ours, cut to the same shape (body, body, outline) */
static void medal(const smk_font *f, int rank, uint32_t out[4])
{
    switch (rank) {
    case 0: ramp(f, TEXT_HI, out, 0xFFFFFFFF, 0xFF7A5A18); break;
    case 1: out[0] = 0; out[1] = 0xFFFFFFFF; out[2] = 0xFFC8D0E0; out[3] = 0xFF505A78; break;
    case 2: out[0] = 0; out[1] = 0xFFF8D0A0; out[2] = 0xFFD08848; out[3] = 0xFF5A3010; break;
    default: ramp(f, TEXT_PAL, out, 0xFFFFFFFF, 0xFF2A3E78); break;
    }
}

/* player 2's pen, so two humans can tell their rows apart */
static void p2_pen(uint32_t out[4])
{
    out[0] = 0; out[1] = 0xFFC0E8FF; out[2] = 0xFF70C0FF; out[3] = 0xFF104880;
}

static void text_big(const smk_font *f, uint32_t *fb, int w, int h,
                     int vx, int vy, const char *s, const uint32_t col[4], int mult)
{
    smk_font_draw(f, fb, w, h, ui_ox + vx * ui_sc, ui_oy + vy * ui_sc,
                  s, ui_sc * mult, col);
}

/* the font has no '+' (src/font.c), so one is drawn: 5x5, centred */
static void plus(uint32_t *fb, int w, int h, int vx, int vy, uint32_t c, int mult)
{
    fill(fb, w, h, vx, vy + 2 * mult, 5 * mult, mult, c);
    fill(fb, w, h, vx + 2 * mult, vy, mult, 5 * mult, c);
}

/* A trophy, 16x16 pixel art (OURS): '#' body, 'o' the shine, ' ' clear. */
static const char *const TROPHY[16] = {
    "  ############  ",
    " #o###########  ",
    "# o##########  #",
    "# o##########  #",
    " #o##########  #",
    "  #o########  # ",
    "    ########    ",
    "     ######     ",
    "      ####      ",
    "       ##       ",
    "       ##       ",
    "      ####      ",
    "     ######     ",
    "    ########    ",
    "   ##########   ",
    "   ##########   ",
};

static void trophy(uint32_t *fb, int w, int h, int vx, int vy, int rank, int mult,
                   unsigned tick)
{
    uint32_t body, shine, dark;
    if (rank == 0)      { body = 0xFFF0C030; shine = 0xFFFFF0A0; dark = 0xFF906010; }
    else if (rank == 1) { body = 0xFFC8D0E0; shine = 0xFFFFFFFF; dark = 0xFF707A90; }
    else                { body = 0xFFD08848; shine = 0xFFF8D0A0; dark = 0xFF7A4818; }
    /* a glint that walks down the bowl */
    int glint = (int)(tick / 6) % 24;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            char ch = TROPHY[y][x];
            if (ch == ' ') continue;
            uint32_t c = ch == 'o' ? shine : body;
            if (ch == '#' && y >= 6) c = (x >= 6 && x <= 9) ? body : dark;
            if (ch == '#' && y < 6 && x + y == glint) c = shine;
            fill(fb, w, h, vx + x * mult, vy + y * mult, mult, mult, c);
        }
}

/* confetti: a shower that never repeats itself in any way anyone would
 * notice, from the tick alone */
static void confetti(uint32_t *fb, int w, int h, unsigned tick, int count)
{
    static const uint32_t C[6] = { 0xFFFF4040, 0xFFFFD040, 0xFF40E060,
                                   0xFF40A0FF, 0xFFFF70D0, 0xFFFFFFFF };
    for (int i = 0; i < count; i++) {
        uint32_t hv = hash32((uint32_t)i * 40503u + 7u);
        int speed = 1 + (int)((hv >> 4) % 3);
        int x0 = (int)(hv % VW);
        int y = (int)(((hv >> 12) % 260u + tick * (unsigned)speed / 2u) % 260u) - 20;
        int sway = (int)(((tick + (hv >> 8)) / 8) % 4);
        int x = x0 + (sway == 3 ? 1 : sway) - 1;
        int wobble = (int)(((tick + i) / 6) & 1);
        fill(fb, w, h, x, y, 2 + wobble, 2, C[(hv >> 16) % 6]);
    }
}

/* the driver's portrait: the FRONT view, drawn as the game draws it -
 * its left half and that half's mirror (NOTES 199) - or the rear view */
static void portrait(const smk_rom *rom, const uint32_t *palette, int who,
                     int vx, int vy, int mult, bool front, bool arms_up,
                     uint32_t *fb, int w, int h)
{
    const smk_sprites *s = driver_art(rom, who);
    if (!s || !palette) return;
    int cx = ui_ox + vx * ui_sc, cy = ui_oy + vy * ui_sc;
    if (front)
        smk_draw_sprite_mirror(s, arms_up ? SMK_SPR_WIN_FRAME : SMK_SPR_FRONT,
                               palette, SMK_DRIVERS[who].pal, cx, cy,
                               ui_sc * mult, fb, w, h, w);
    else
        smk_draw_sprite(s, SMK_SPR_REAR, palette, SMK_DRIVERS[who].pal,
                        cx, cy, ui_sc * mult, false, fb, w, h, w);
}

/* the half-size portrait for a list row (the far-tier sampler, sprite.c) */
static void portrait_mini(const smk_rom *rom, const uint32_t *palette, int who,
                          int vx, int vy, uint32_t *fb, int w, int h)
{
    const smk_sprites *s = driver_art(rom, who);
    if (!s || !palette) return;
    smk_draw_sprite_mirror2(s, SMK_SPR_FRONT, palette, SMK_DRIVERS[who].pal,
                            ui_ox + vx * ui_sc, ui_oy + vy * ui_sc, ui_sc, true,
                            fb, w, h, w);
}

static void upper(char *s)
{
    for (; *s; s++) if (*s >= 'a' && *s <= 'z') *s -= 'a' - 'A';
}

static const char *ordinal(int place)
{
    static const char *const ORD[SMK_CHARACTERS + 1] = {
        "", "1ST", "2ND", "3RD", "4TH", "5TH", "6TH", "7TH", "8TH" };
    return ORD[place < 0 ? 0 : place > SMK_CHARACTERS ? SMK_CHARACTERS : place];
}

void smk_ui_init(smk_ui *ui)
{
    memset(ui, 0, sizeof *ui);
    ui->screen = SMK_UI_TITLE;
    ui->mode_sel = SMK_UI_MODE_RACE;
    ui->players = SMK_PLAYERS_1;
    ui->player2_sel = 1;         /* Luigi, until it is chosen */
    ui->engine_class = 0;        /* 50cc */
    ui->track = -1;
}

/* ---- navigation ---------------------------------------------------- */

/* How many rows the mode screen shows.  TIME TRIAL is last and is only
 * there for one player, so this is all it takes to hide it. */
int smk_ui_mode_rows(const smk_ui *ui)
{
    return ui->players == SMK_PLAYERS_1 ? SMK_UI_MODES : SMK_UI_MODES - 1;
}

/* How long the two cup screens take to play out.  Enter before the end
 * jumps to the end; Enter at the end moves on. */
#define POINTS_DONE     104
#define STANDINGS_DONE  112

bool smk_ui_step(smk_ui *ui, const smk_rom *rom, const smk_ui_input *in)
{
    ui->tick++;
    if (ui->denied_t > 0) ui->denied_t--;
    /* a screen's clock starts when it is entered, however it was entered
     * (main.c jumps to RESULT itself) */
    if (ui->screen != ui->last_screen) { ui->last_screen = ui->screen; ui->screen_t = 0; }
    else ui->screen_t++;

    switch (ui->screen) {
    case SMK_UI_TITLE:
        if (in->confirm) ui->screen = SMK_UI_PLAYERS;
        break;

    case SMK_UI_PLAYERS: {
        /* HOW MANY, first - the order the original asks in, and it has to
         * be first because it decides what the mode screen may offer. */
        int n = SMK_PLAYERS_MODES;
        if (in->up)   ui->players = (ui->players + n - 1) % n;
        if (in->down) ui->players = (ui->players + 1) % n;
        /* two humans need a controller: with none attached there is
         * nothing for the second player to drive with, so that row is
         * stepped over rather than offered and then refused */
        if (ui->players == SMK_PLAYERS_2 && ui->pads < 1)
            ui->players = in->up ? SMK_PLAYERS_CPU : SMK_PLAYERS_1;
        if (in->back)    ui->screen = SMK_UI_TITLE;
        if (in->confirm) {
            /* a time trial is a solo thing: with a second driver on the
             * track the row is not there to land on */
            if (ui->players != SMK_PLAYERS_1 && ui->mode_sel == SMK_UI_MODE_TT)
                ui->mode_sel = SMK_UI_MODE_RACE;
            ui->screen = SMK_UI_MODE;
        }
        break;
    }

    case SMK_UI_MODE: {
        int rows = smk_ui_mode_rows(ui);
        if (ui->mode_sel >= rows) ui->mode_sel = rows - 1;
        if (in->up)   ui->mode_sel = (ui->mode_sel + rows - 1) % rows;
        if (in->down) ui->mode_sel = (ui->mode_sel + 1) % rows;
        if (in->back) ui->screen = SMK_UI_PLAYERS;
        if (in->confirm) {
            ui->gp = (ui->mode_sel == SMK_UI_MODE_GP);
            ui->picking_p2 = false;
            ui->screen = SMK_UI_CLASS;
        }
        break;
    }

    case SMK_UI_CLASS:
        /* THE CLASS ON ITS OWN SCREEN, before the driver - the original's
         * order, and the user's complaint about the combined screen:
         * "selecting kart and difficulty is super counter intuitive". */
        if (in->up)   ui->engine_class = (ui->engine_class + 2) % 3;
        if (in->down) ui->engine_class = (ui->engine_class + 1) % 3;
        if (in->back) ui->screen = SMK_UI_MODE;
        if (in->confirm) { ui->picking_p2 = false; ui->screen = SMK_UI_PLAYER; }
        break;

    case SMK_UI_PLAYER: {
        /* The same grid picks player 1 and then, in a two-view race,
         * player 2 (or the CPU the second camera follows).  `sel` is
         * whichever of the two the cursor belongs to, so the navigation
         * below is written once. */
        int *sel = ui->picking_p2 ? &ui->player2_sel : &ui->player_sel;
        int p = *sel & 7;                   /* a 4x2 grid, wrapping both ways */
        if (in->left)  *sel = (p & 4) | ((p + 3) & 3);
        if (in->right) *sel = (p & 4) | ((p + 1) & 3);
        if (in->up || in->down) *sel = p ^ 4;
        if (in->back) {
            if (ui->picking_p2) ui->picking_p2 = false;
            else ui->screen = SMK_UI_CLASS;
        }
        if (in->confirm) {
            if (ui->players != SMK_PLAYERS_1 && !ui->picking_p2) {
                ui->picking_p2 = true;
                /* two karts cannot be the same driver */
                if (ui->player2_sel == ui->player_sel)
                    ui->player2_sel = (ui->player_sel + 1) % SMK_CHARACTERS;
            } else {
                ui->picking_p2 = false;
                ui->screen = SMK_UI_COURSE;
            }
        }
        /* and the cursor may not land on the other player's driver */
        if (ui->players != SMK_PLAYERS_1 && ui->picking_p2
            && ui->player2_sel == ui->player_sel)
            ui->player2_sel = (ui->player2_sel + 1) % SMK_CHARACTERS;
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
        if (in->confirm || in->back) ui->screen = ui->gp ? SMK_UI_POINTS : SMK_UI_COURSE;
        break;

    case SMK_UI_POINTS:
        if (in->confirm || in->back) {
            if (ui->screen_t < POINTS_DONE) ui->screen_t = POINTS_DONE;
            else ui->screen = SMK_UI_STANDINGS;
        }
        break;

    case SMK_UI_STANDINGS:
        if (in->back) { ui->gp = false; ui->screen = SMK_UI_COURSE; break; }
        if (in->confirm && ui->screen_t < STANDINGS_DONE) { ui->screen_t = STANDINGS_DONE; break; }
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
    for (int i = 0; i < SMK_CHARACTERS; i++) {
        ui->gp_prev_points[i] = ui->gp_points[i];
        ui->gp_prev_place[i] = ui->gp_place[i];
        ui->gp_award[i] = 0;
    }
    for (int p = 0; p < res->entries && p < SMK_CHARACTERS; p++) {
        int ch = res->field[p].character % SMK_CHARACTERS;
        ui->gp_place[ch] = p + 1;
        if (!ui->ranked_out && p < 4) {
            ui->gp_award[ch] = ui->gp_pts_table[p];
            ui->gp_points[ch] += ui->gp_pts_table[p];
        }
    }
}

/* by points, then by the given race's place, then by driver index */
static void order_by(const int *points, const int *place, int order[SMK_CHARACTERS])
{
    for (int i = 0; i < SMK_CHARACTERS; i++) order[i] = i;
    for (int i = 1; i < SMK_CHARACTERS; i++) {
        int v = order[i], j = i - 1;
        for (; j >= 0; j--) {
            int a = order[j];
            bool worse = points[a] < points[v]
                      || (points[a] == points[v] && place[a] > place[v]);
            if (!worse) break;
            order[j + 1] = order[j];
        }
        order[j + 1] = v;
    }
}

void smk_ui_gp_order(const smk_ui *ui, int order[SMK_CHARACTERS])
{
    order_by(ui->gp_points, ui->gp_place, order);
}

void smk_ui_grid_slots(const smk_ui *ui, const int grid[SMK_CHARACTERS],
                       int slots[SMK_CHARACTERS])
{
    bool raced = false;
    for (int i = 0; ui->gp && i < SMK_CHARACTERS; i++)
        if (ui->gp_place[i] > 0) raced = true;
    if (!raced) {
        for (int i = 0; i < SMK_CHARACTERS; i++) slots[i] = SMK_GRID_SLOT(i);
        return;
    }
    /* the last race's finishing order, winner on pole; a driver with no
     * place (the field was short) keeps its block's own row, after the
     * placed ones */
    int order[SMK_CHARACTERS], n = 0;
    for (int p = 1; p <= SMK_CHARACTERS; p++)
        for (int ch = 0; ch < SMK_CHARACTERS; ch++)
            if (ui->gp_place[ch] == p) order[n++] = ch;
    for (int i = 0; i < SMK_CHARACTERS && n < SMK_CHARACTERS; i++) {
        int ch = grid[i] % SMK_CHARACTERS;
        bool in = false;
        for (int k = 0; k < n; k++) in |= order[k] == ch;
        if (!in) order[n++] = ch;
    }
    for (int i = 0; i < SMK_CHARACTERS; i++) {
        slots[i] = SMK_GRID_SLOT(i);
        for (int r = 0; r < SMK_CHARACTERS; r++)
            if (order[r] == grid[i] % SMK_CHARACTERS) { slots[i] = r; break; }
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

static void draw_title(const smk_ui *ui, const smk_rom *rom, const smk_font *f,
                       const uint32_t *palette, uint32_t *fb, int w, int h)
{
    uint32_t hi[4], lo[4], gold[4];
    ramp(f, TEXT_HI, hi, 0xFFFFFFFF, 0xFF7A5A18);
    ramp(f, TEXT_PAL, lo, 0xFFFFFFFF, 0xFF2A3E78);
    shimmer(f, gold, ui->tick);
    /* the name, twice the font's size, with a drop shadow under it */
    {
        const char *s = "SUPER MARIO KART";
        int x = (VW - (int)strlen(s) * 16) / 2;
        uint32_t shadow[4] = { 0, 0xFF101830, 0xFF101830, 0xFF101830 };
        text_big(f, fb, w, h, x + 2, 50, s, shadow, 2);
        text_big(f, fb, w, h, x, 48, s, gold, 2);
    }
    text_c(f, fb, w, h, 76, "A NATIVE PORT", lo);
    /* the field drives past along the bottom: the eight drivers' rear
     * views on a road strip, in a loop */
    {
        /* the road is a box with ends, and the karts drive THROUGH it -
         * clipped to it, in at the left edge and out at the right, the
         * class screen's effect (the user asked for the same here) */
        const int RX = 8, RW = VW - 16, road_y = 150;
        fill(fb, w, h, RX, road_y - 34, RW, 42, 0x50000000);
        fill(fb, w, h, RX, road_y + 4, RW, 2, 0xFFE0E0E0);
        for (int i = 0; i < SMK_CHARACTERS; i++) {
            int span = RW + SMK_SPR_PX + 7 * 40;
            int x = RX - SMK_SPR_PX / 2
                  + (int)((ui->tick * 3u / 2u + (unsigned)i * 40u) % (unsigned)span);
            int bounce = ((ui->tick / 4 + (unsigned)i) & 1) ? 1 : 0;
            kart_side_clipped(rom, palette, i, x, road_y + bounce, RX, RX + RW, fb, w, h);
        }
    }
    if ((ui->tick / 30) & 1)
        text_c(f, fb, w, h, 176, "PRESS ENTER", hi);
    text_c(f, fb, w, h, 204, "READS YOUR OWN ROM", lo);
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
    /* TIME TRIAL is simply not there with a second driver on the track */
    int rows = smk_ui_mode_rows(ui);
    for (int i = 0; i < rows; i++) {
        int y = 84 + i * 24;
        const uint32_t *c = ui->mode_sel == i ? sel : lo;
        int x = (VW - (int)strlen(row[i]) * 8) / 2;
        if (ui->mode_sel == i && ((ui->tick / 12) & 1) == 0)
            fill(fb, w, h, x - 12, y - 2, 8, 12, 0xFFFFC040);
        text(f, fb, w, h, x, y, row[i], c);
    }
    {   /* what was chosen on the screen before, so it can be seen */
        const char *pl[SMK_PLAYERS_MODES] = { "1 PLAYER", "1 PLAYER VS CPU",
                                              "2 PLAYERS" };
        text_c(f, fb, w, h, 176, pl[ui->players % SMK_PLAYERS_MODES], off);
    }
    text_c(f, fb, w, h, 200, "ENTER SELECT   ESC BACK", lo);
}

/* HOW MANY PLAYERS, asked first - the original's order, and it decides
 * what the mode screen may offer. */
static void draw_players(const smk_ui *ui, const smk_font *f,
                         uint32_t *fb, int w, int h)
{
    uint32_t hi[4], lo[4], off[4], sel[4];
    ramp(f, TEXT_HI, hi, 0xFFFFFFFF, 0xFF7A5A18);
    ramp(f, TEXT_PAL, lo, 0xFFFFFFFF, 0xFF2A3E78);
    ramp(f, TEXT_PAL, off, 0xFFFFFFFF, 0xFF2A3E78); dim(off);
    ramp(f, TEXT_HI, sel, 0xFFFFFFFF, 0xFF7A5A18);

    text_c(f, fb, w, h, 40, "HOW MANY PLAYERS", hi);
    /* the ROM font has no '+' or '-' (src/font.c), so these are spelled
     * in letters rather than drawn with a hole in them */
    const char *pl[SMK_PLAYERS_MODES] = { "1 PLAYER", "1 PLAYER VS CPU",
                                          "2 PLAYERS" };
    const char *sub[SMK_PLAYERS_MODES] = { "ONE SCREEN",
                                           "SPLIT SCREEN, CPU ON THE RIGHT",
                                           "SPLIT SCREEN, SIDE BY SIDE" };
    for (int i = 0; i < SMK_PLAYERS_MODES; i++) {
        int y = 84 + i * 26;
        /* a second DRIVER needs something to drive with: with no pad
         * attached that row is drawn dim and cannot be reached */
        bool can = !(i == SMK_PLAYERS_2 && ui->pads < 1);
        int x = (VW - (int)strlen(pl[i]) * 8) / 2;
        if (ui->players == i && ((ui->tick / 12) & 1) == 0)
            fill(fb, w, h, x - 12, y - 2, 8, 12, 0xFFFFC040);
        text(f, fb, w, h, x, y, pl[i], !can ? off : (ui->players == i ? sel : lo));
    }
    text_c(f, fb, w, h, 168, sub[ui->players % SMK_PLAYERS_MODES], off);
    const char *note = ui->pads < 1
        ? "NO CONTROLLER: 2 PLAYERS NEEDS ONE"
        : (ui->pads < 2 ? "P1 CONTROLLER   P2 KEYBOARD"
                        : "P1 PAD 1   P2 PAD 2");
    text_c(f, fb, w, h, 184, note, off);
    text_c(f, fb, w, h, 200, "ENTER SELECT   ESC BACK", lo);
}

/* a kart seen from the side, facing right: the measured rotation rule at
 * a quarter turn (NOTES 041) */
static void kart_side(const smk_rom *rom, const uint32_t *palette, int who,
                      int vx, int vy, int mult, uint32_t *fb, int w, int h)
{
    const smk_sprites *s = driver_art(rom, who);
    if (!s || !palette) return;
    bool hf = false;
    int fr = smk_sprite_for_heading(SMK_SPR_TIER0, 0x4000, &hf);
    smk_draw_sprite(s, fr, palette, SMK_DRIVERS[who].pal,
                    ui_ox + vx * ui_sc, ui_oy + vy * ui_sc, ui_sc * mult, hf,
                    fb, w, h, w);
}

/* the same kart, clipped to a card: only the pixels inside [cx0, cx1) in
 * virtual x are drawn, so it can slide in through one edge and out the
 * other instead of appearing and vanishing whole */
static void kart_side_clipped(const smk_rom *rom, const uint32_t *palette, int who,
                              int vx, int vy, int cx0, int cx1,
                              uint32_t *fb, int w, int h)
{
    const smk_sprites *s = driver_art(rom, who);
    if (!s || !palette) return;
    bool hf = false;
    int fr = smk_sprite_for_heading(SMK_SPR_TIER0, 0x4000, &hf);
    if (fr < 0 || fr >= s->frames) return;
    const uint8_t *src = s->px[fr];
    int x0 = vx - SMK_SPR_PX / 2, y0 = vy - SMK_SPR_PX;   /* at the wheels */
    for (int y = 0; y < SMK_SPR_PX; y++)
        for (int x = 0; x < SMK_SPR_PX; x++) {
            int px = x0 + x;
            if (px < cx0 || px >= cx1) continue;
            uint8_t v = src[y * SMK_SPR_PX + (hf ? SMK_SPR_PX - 1 - x : x)];
            if (!v) continue;
            fill(fb, w, h, px, y0 + y, 1, 1,
                 0xFF000000u | palette[(SMK_DRIVERS[who].pal + v) & 0xFF]);   /* fill reads alpha */
        }
}

/* a 2-px frame around a card */
static void frame_box(uint32_t *fb, int w, int h, int vx, int vy, int vw, int vh,
                      uint32_t c)
{
    fill(fb, w, h, vx, vy, vw, 2, c);
    fill(fb, w, h, vx, vy + vh - 2, vw, 2, c);
    fill(fb, w, h, vx, vy, 2, vh, c);
    fill(fb, w, h, vx + vw - 2, vy, 2, vh, c);
}

/* THE CLASS, on its own screen (OURS).  Three rows, and beside each a
 * kart running at that class's pace, so the choice can be seen rather
 * than read: the port's own top speeds per class are the ROM's
 * ($80A4E1's tables), and 50/100/150 are in that proportion. */
static void draw_class(const smk_ui *ui, const smk_rom *rom, const smk_font *f,
                       const uint32_t *palette, uint32_t *fb, int w, int h)
{
    uint32_t hi[4], lo[4], off[4], glow[4];
    ramp(f, TEXT_HI, hi, 0xFFFFFFFF, 0xFF7A5A18);
    ramp(f, TEXT_PAL, lo, 0xFFFFFFFF, 0xFF2A3E78);
    ramp(f, TEXT_PAL, off, 0xFFFFFFFF, 0xFF2A3E78); dim(off);
    shimmer(f, glow, ui->tick);
    text_c(f, fb, w, h, 16, "SELECT CLASS", glow);
    static const char *const TAG[3] = { "EASY", "NORMAL", "HARD" };
    for (int c = 0; c < 3; c++) {
        int vy = 44 + c * 50;
        bool on = ui->engine_class == c;
        fill(fb, w, h, 16, vy - 6, 224, 44, on ? 0x50FFC040 : 0x20FFFFFF);
        if (on) frame_box(fb, w, h, 16, vy - 6, 224, 44, 0xFFFFD040);
        char nm[8];
        snprintf(nm, sizeof nm, "%s", class_name(c)); upper(nm);
        text_big(f, fb, w, h, 28, vy, nm, on ? hi : lo, 2);
        text(f, fb, w, h, 28, vy + 20, TAG[c], on ? lo : off);
        /* the road strip and its kart, at the class's pace: 2, 3 and 4
         * pixels a tick */
        /* the kart stays INSIDE its card: it runs the strip's length,
         * a sprite's width in from each end, and reappears at the left.
         * The user's twist: a driver per class - Toad, Mario, Bowser -
         * light, medium and heavy, in the ROM's own weight order. */
        static const int WHO[3] = { 7, 0, 2 };
        const int RX = 118, RW = 112;
        fill(fb, w, h, RX, vy + 28, RW, 2, 0xFFE0E0E0);
        /* it drives THROUGH the card: in at the left edge, out at the
         * right, clipped to the card's own width */
        int span = RW + SMK_SPR_PX;
        int x = RX - SMK_SPR_PX / 2 + (int)((ui->tick * (unsigned)(c + 2) / 2u) % (unsigned)span);
        int bounce = on && ((ui->tick / 4) & 1) ? 1 : 0;
        kart_side_clipped(rom, palette, WHO[c], x, vy + 28 - bounce, RX, RX + RW, fb, w, h);
        if (on && ((ui->tick / 12) & 1) == 0)
            fill(fb, w, h, 6, vy + 2, 6, 10, 0xFFFFC040);
    }
    text_c(f, fb, w, h, 202, "ENTER SELECT   ESC BACK", lo);
}

/* THE DRIVER.  A 4x2 grid of cards, each big enough to hold its kart
 * whole - the user: "the selection block doesn't even cover the kart" -
 * the karts seen from the side, the chosen one framed in gold and
 * bouncing, and its name and weight class (the ROM's own $81:9277,
 * NOTES 166) written large underneath. */
static void draw_player(const smk_ui *ui, const smk_rom *rom, const smk_font *f,
                        const uint32_t *palette, uint32_t *fb, int w, int h)
{
    uint32_t hi[4], lo[4], off[4], p2[4], glow[4];
    ramp(f, TEXT_HI, hi, 0xFFFFFFFF, 0xFF7A5A18);
    ramp(f, TEXT_PAL, lo, 0xFFFFFFFF, 0xFF2A3E78);
    ramp(f, TEXT_PAL, off, 0xFFFFFFFF, 0xFF2A3E78); dim(off);
    p2_pen(p2);
    shimmer(f, glow, ui->tick);
    const char *who = ui->players == SMK_PLAYERS_1 ? "SELECT DRIVER"
                    : ui->picking_p2
                      ? (ui->players == SMK_PLAYERS_CPU ? "SELECT THE CPU"
                                                        : "PLAYER 2, SELECT DRIVER")
                      : "PLAYER 1, SELECT DRIVER";
    text_c(f, fb, w, h, 10, who, glow);
    int cursor = (ui->picking_p2 ? ui->player2_sel : ui->player_sel) & 7;
    int other  = ui->players == SMK_PLAYERS_1 ? -1
               : (ui->picking_p2 ? ui->player_sel : ui->player2_sel);
    const int CW = 54, CH = 60, GAP = 6;
    int x0 = (VW - (4 * CW + 3 * GAP)) / 2;
    for (int i = 0; i < SMK_CHARACTERS; i++) {
        int col = i & 3, row = i >> 2;
        int vx = x0 + col * (CW + GAP), vy = 26 + row * (CH + GAP);
        bool on = cursor == i, taken = i == other;
        /* the card: plain, the other player's in their blue, the chosen
         * one gold with a frame */
        uint32_t card = taken ? 0x4870C0FF : 0x28FFFFFF;
        if (on) {
            unsigned ph = (ui->tick * 6) & 255;
            unsigned k = ph < 128 ? ph : 255 - ph;
            card = ((0x48u + k / 4u) << 24) | 0x00FFC040u;
        }
        fill(fb, w, h, vx, vy, CW, CH, card);
        if (on) frame_box(fb, w, h, vx, vy, CW, CH, 0xFFFFD040);
        int bounce = on && ((ui->tick / 4) & 1) ? 1 : 0;
        kart_side(rom, palette, i, vx + CW / 2, vy + 44 - bounce, 1, fb, w, h);
        char nm[16];
        snprintf(nm, sizeof nm, "%s", SMK_DRIVERS[i].name); upper(nm);
        text(f, fb, w, h, vx + (CW - (int)strlen(nm) * 8) / 2, vy + 48, nm,
             on ? hi : taken ? p2 : lo);
        if (taken)
            text(f, fb, w, h, vx + 4, vy + 4, ui->picking_p2 ? "P1" : "P2", p2);
    }
    /* the chosen one, large, with what the ROM knows about it */
    {
        char nm[16];
        snprintf(nm, sizeof nm, "%s", SMK_DRIVERS[cursor].name); upper(nm);
        int tw = (int)strlen(nm) * 16;
        fill(fb, w, h, (VW - tw) / 2 - 12, 158, tw + 24, 22, 0x30000000);
        text_big(f, fb, w, h, (VW - tw) / 2, 161, nm, hi, 2);
        uint8_t wt = SMK_KART_WEIGHT[cursor];
        const char *cls = wt >= 0x1B ? "HEAVY   HARD TO BUMP"
                        : wt == 0x1A ? "MEDIUM"
                                     : "LIGHT   EASY TO BUMP";
        text_c(f, fb, w, h, 184, cls, lo);
    }
    char foot[48];
    snprintf(foot, sizeof foot, "%s   ENTER SELECT   ESC BACK", class_name(ui->engine_class));
    upper(foot);
    text_c(f, fb, w, h, 202, foot, off);
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
    layout(w, h, true);
    backdrop(fb, w, h, f, ui->tick);
    switch (ui->screen) {
    case SMK_UI_TITLE:   draw_title(ui, rom, f, palette, fb, w, h); break;
    case SMK_UI_PLAYERS: draw_players(ui, f, fb, w, h); break;
    case SMK_UI_MODE:    draw_mode(ui, f, fb, w, h); break;
    case SMK_UI_CLASS:   draw_class(ui, rom, f, palette, fb, w, h); break;
    case SMK_UI_PLAYER: draw_player(ui, rom, f, palette, fb, w, h); break;
    case SMK_UI_COURSE: draw_course(ui, rom, f, rec, fb, w, h); break;
    default: break;
    }
}

void smk_ui_draw_result(const smk_ui *ui, const smk_rom *rom, const smk_font *f,
                        const smk_records *rec, const smk_ui_result *res,
                        const uint32_t *palette, uint32_t *fb, int w, int h)
{
    layout(w, h, true);
    if (!f->ok) return;
    backdrop(fb, w, h, f, ui->tick);
    uint32_t hi[4], lo[4], gold[4], off[4], glow[4];
    ramp(f, TEXT_HI, hi, 0xFFFFFFFF, 0xFF7A5A18);
    ramp(f, TEXT_PAL, lo, 0xFFFFFFFF, 0xFF2A3E78);
    ramp(f, TEXT_HI, gold, 0xFFFFFFFF, 0xFF7A5A18);
    ramp(f, TEXT_PAL, off, 0xFFFFFFFF, 0xFF2A3E78); dim(off);
    shimmer(f, glow, ui->tick);

    bool race = res->position > 0;
    if (ui->gp) {
        char line[48];
        snprintf(line, sizeof line, "%s   RACE %d OF %d", SMK_CUP_NAMES[ui->cup_sel % SMK_CUPS],
                 ui->gp_race + 1, SMK_CUP_COURSES);
        text_c(f, fb, w, h, 10, line, glow);
    } else {
        text_c(f, fb, w, h, 10, race ? "SINGLE RACE" : "TIME TRIAL", glow);
    }
    text_c(f, fb, w, h, 24, smk_track_name(rom, ui->track), gold);

    if (race && res->entries > 0) {
        /* the player's own kart beside the table, waving if it won */
        {
            int me = ui->player_sel % SMK_CHARACTERS;
            bool won = res->position == 1;
            bool arms = won && ((ui->screen_t / SMK_WIN_TOGGLE) & 1) == 0;
            fill(fb, w, h, 212, 56, 40, 48, 0x30FFFFFF);
            portrait(rom, palette, me, 232, 100, 1, true, arms, fb, w, h);
            if (won) confetti(fb, w, h, ui->tick, 40);
        }
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
            /* gold is the DRIVER's own row, and only his: a CPU-driven
             * view (--cpu-policy, --autodrive) is another racer as far as
             * this table is concerned, however it is being steered.  It
             * used to take the gold as well, because `player` was a bool
             * meaning "someone drives this slot". */
            const uint32_t *col = res->field[i].player == 1 ? gold
                                : res->field[i].player == 2 ? hi
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

/* ---- the two cup screens (NOTES 274) --------------------------------
 *
 * After the times: THE POINTS - the race's finishing order in two
 * columns of four, each driver's portrait, place and what the ROM's
 * table paid him, arriving one by one; then THE CHAMPIONSHIP - every
 * driver at the totals they HAD, the new points counting in, and the
 * rows re-sorting themselves into the new order.  After the fifth race
 * the champion takes the floor with a trophy and the confetti.  Enter
 * during the animation jumps to its end; Enter at the end moves on.
 * The layout and the motion are ours (S20); the numbers are the cup's. */

/* who the human players are, for the pens */
static bool is_p1(const smk_ui *ui, int ch) { return ch == ui->player_sel % SMK_CHARACTERS; }
static bool is_p2(const smk_ui *ui, int ch)
{
    return ui->players == SMK_PLAYERS_2 && ch == ui->player2_sel % SMK_CHARACTERS;
}

static void cup_header(const smk_ui *ui, const smk_rom *rom, const smk_font *f,
                       const char *what, uint32_t *fb, int w, int h)
{
    uint32_t glow[4], gold[4];
    shimmer(f, glow, ui->tick);
    ramp(f, TEXT_HI, gold, 0xFFFFFFFF, 0xFF7A5A18);
    char line[48];
    snprintf(line, sizeof line, "%s", SMK_CUP_NAMES[ui->cup_sel % SMK_CUPS]);
    text_c(f, fb, w, h, 9, line, glow);
    snprintf(line, sizeof line, "RACE %d OF %d   %s", ui->gp_race + 1, SMK_CUP_COURSES,
             smk_track_name(rom, ui->track));
    text_c(f, fb, w, h, 21, line, gold);
    if (what) {
        /* a banner behind the title */
        int tw = (int)strlen(what) * 8;
        fill(fb, w, h, (VW - tw) / 2 - 10, 33, tw + 20, 12, 0x60FFC040);
        text_c(f, fb, w, h, 35, what, glow);
    }
}

void smk_ui_draw_points(const smk_ui *ui, const smk_rom *rom, const smk_font *f,
                        const uint32_t *palette, uint32_t *fb, int w, int h)
{
    layout(w, h, true);
    if (!f->ok) return;
    backdrop(fb, w, h, f, ui->tick);
    uint32_t lo[4], off[4], p2[4], pen[4];
    ramp(f, TEXT_PAL, lo, 0xFFFFFFFF, 0xFF2A3E78);
    ramp(f, TEXT_PAL, off, 0xFFFFFFFF, 0xFF2A3E78); dim(off);
    p2_pen(p2);
    cup_header(ui, rom, f, ui->ranked_out ? "RANKED OUT" : "RACE POINTS", fb, w, h);

    /* the finishing order: by the place each driver took */
    int order[SMK_CHARACTERS], n = 0;
    for (int p = 1; p <= SMK_CHARACTERS; p++)
        for (int ch = 0; ch < SMK_CHARACTERS; ch++)
            if (ui->gp_place[ch] == p) order[n++] = ch;
    for (int ch = 0; ch < SMK_CHARACTERS && n < SMK_CHARACTERS; ch++) {
        bool in = false;
        for (int i = 0; i < n; i++) in |= order[i] == ch;
        if (!in) order[n++] = ch;
    }

    int t = (int)ui->screen_t;
    for (int i = 0; i < SMK_CHARACTERS; i++) {
        int ch = order[i];
        int col = i / 4, row = i % 4;
        int t0 = 6 + i * 10;                 /* when this cell arrives */
        if (t < t0) continue;
        float e = ease(t - t0, 12);
        int vx = 8 + col * 124 + (int)((1.0f - e) * 70.0f);
        int vy = 48 + row * 35;
        bool me = is_p1(ui, ch), other = is_p2(ui, ch);
        int rank = i;                        /* 0..3 get the medal pens */
        uint32_t medalpen[4]; medal(f, rank, medalpen);
        const uint32_t *c = rank < 4 ? medalpen : off;
        /* the cell: a pale card, the human's pulsing */
        uint32_t card = other ? 0x5070C0FF : 0x28FFFFFF;
        if (me) {                            /* pulsing gold */
            unsigned ph = (ui->tick * 6) & 255;
            unsigned k = ph < 128 ? ph : 255 - ph;
            card = ((0x40u + k / 4u) << 24) | 0x00FFC040u;
        }
        fill(fb, w, h, vx, vy, 118, 34, card);
        fill(fb, w, h, vx, vy + 33, 118, 1, 0x60000000);
        portrait(rom, palette, ch, vx + 18, vy + 32, 1, true, rank == 0, fb, w, h);
        char nm[16];
        snprintf(nm, sizeof nm, "%s", SMK_DRIVERS[ch].name); upper(nm);
        text(f, fb, w, h, vx + 38, vy + 4, ordinal(i + 1), c);
        text(f, fb, w, h, vx + 38, vy + 16, nm, me ? medalpen : other ? p2 : rank < 4 ? lo : off);
        if (me)    text(f, fb, w, h, vx + 66, vy + 4, "P1", medalpen);
        if (other) text(f, fb, w, h, vx + 66, vy + 4, "P2", p2);
        /* the points: pop in big, settle to double size.  Ranked out,
         * nothing is paid: the table's values show dimmed, what was at
         * stake, and the footer says so. */
        int pts = ui->ranked_out ? (rank < 4 ? ui->gp_pts_table[rank] : 0) : ui->gp_award[ch];
        int pop_at = t0 + 8;
        if (t >= pop_at) {
            char num[8];
            snprintf(num, sizeof num, "%d", pts);
            int mult = t < pop_at + 5 ? 3 : 2;
            int px = vx + 118 - 8 * mult - 4, py = vy + 17 - 4 * mult;
            memcpy(pen, pts > 0 && !ui->ranked_out ? c : off, sizeof pen);
            if (pts > 0 && !ui->ranked_out) plus(fb, w, h, px - 7, py + 4 * mult - 2, pen[1], 1);
            text_big(f, fb, w, h, px, py, num, pen, mult);
            /* the winner's cell throws sparks for a while */
            if (rank == 0 && pts > 0 && !ui->ranked_out && t < pop_at + 40) {
                for (int k = 0; k < 6; k++) {
                    unsigned a = (unsigned)(t - pop_at) * 7u + (unsigned)k * 43u;
                    int r = 6 + (t - pop_at) / 2;
                    int sx = px + 8 + (int)(r * ((int)(a % 24) - 12) / 12);
                    int sy = py + 8 + (int)(r * ((int)((a / 24) % 24) - 12) / 12);
                    fill(fb, w, h, sx, sy, 2, 2, 0xFFFFF080);
                }
            }
        }
    }
    if (t >= POINTS_DONE) {
        uint32_t glow[4]; shimmer(f, glow, ui->tick);
        if (ui->ranked_out) {
            text_c(f, fb, w, h, 194, "NO POINTS   TOP FOUR ONLY", off);
            if ((ui->tick / 20) & 1) text_c(f, fb, w, h, 208, "ENTER RUN IT AGAIN", glow);
        } else if ((ui->tick / 20) & 1) {
            text_c(f, fb, w, h, 208, "ENTER STANDINGS", glow);
        }
    }
}

void smk_ui_draw_standings(const smk_ui *ui, const smk_rom *rom, const smk_font *f,
                           const uint32_t *palette, uint32_t *fb, int w, int h)
{
    layout(w, h, true);
    if (!f->ok) return;
    backdrop(fb, w, h, f, ui->tick);
    uint32_t hi[4], lo[4], gold[4], off[4], p2[4], glow[4];
    ramp(f, TEXT_HI, hi, 0xFFFFFFFF, 0xFF7A5A18);
    ramp(f, TEXT_PAL, lo, 0xFFFFFFFF, 0xFF2A3E78);
    ramp(f, TEXT_HI, gold, 0xFFFFFFFF, 0xFF7A5A18);
    ramp(f, TEXT_PAL, off, 0xFFFFFFFF, 0xFF2A3E78); dim(off);
    p2_pen(p2);
    shimmer(f, glow, ui->tick);
    bool final = !ui->ranked_out && ui->gp_race + 1 >= SMK_CUP_COURSES;
    cup_header(ui, rom, f, final ? "FINAL STANDINGS" : "CHAMPIONSHIP", fb, w, h);

    /* the order before the race and the order after it */
    int before[SMK_CHARACTERS], after[SMK_CHARACTERS];
    smk_ui_gp_order(ui, after);
    bool had_order = false;              /* the first race has nothing to re-sort from */
    for (int i = 0; i < SMK_CHARACTERS; i++) had_order |= ui->gp_prev_place[i] > 0;
    if (had_order) order_by(ui->gp_prev_points, ui->gp_prev_place, before);
    else memcpy(before, after, sizeof before);
    int rank_before[SMK_CHARACTERS], rank_after[SMK_CHARACTERS];
    for (int r = 0; r < SMK_CHARACTERS; r++) { rank_before[before[r]] = r; rank_after[after[r]] = r; }

    /* the timeline */
    int t = (int)ui->screen_t;
    const int T_IN = 4, T_COUNT = 34, T_COUNT_LEN = 40, T_SORT = 80, T_SORT_LEN = 24;
    int row_y0 = 50, row_h = 17;
    int shown_pts[SMK_CHARACTERS];
    for (int ch = 0; ch < SMK_CHARACTERS; ch++) {
        /* a point every few frames, so the total is seen to climb */
        int k = t - T_COUNT;
        if (k < 0) k = 0;
        if (k > T_COUNT_LEN) k = T_COUNT_LEN;
        shown_pts[ch] = ui->gp_prev_points[ch] + ui->gp_award[ch] * k / T_COUNT_LEN;
    }
    float sort_k = ease(t - T_SORT, T_SORT_LEN);

    /* the podium band, so the top three read as the top three */
    int row_w = final ? 172 : 224;       /* the champion needs the right */
    fill(fb, w, h, 16, row_y0 - 2, row_w, row_h * 3, 0x18FFFFFF);
    fill(fb, w, h, 16, row_y0 - 2 + row_h * 3, row_w, 1, 0x50FFFFFF);

    /* rows, drawn back to front so a row moving up passes over the others */
    for (int pass = SMK_CHARACTERS - 1; pass >= 0; pass--) {
        int ch = before[pass];
        int t0 = T_IN + rank_before[ch] * 5;
        if (t < t0) continue;
        float e = ease(t - t0, 12);
        float fy = (float)rank_before[ch] + sort_k * (float)(rank_after[ch] - rank_before[ch]);
        int vy = row_y0 + (int)(fy * (float)row_h + 0.5f);
        int vx = 16 - (int)((1.0f - e) * 90.0f);
        int rank = t >= T_SORT ? rank_after[ch] : rank_before[ch];
        bool me = is_p1(ui, ch), other = is_p2(ui, ch);
        uint32_t medalpen[4]; medal(f, rank, medalpen);
        const uint32_t *c = rank < 3 ? medalpen : lo;
        /* the human's row is lit */
        if (me)    fill(fb, w, h, vx, vy - 1, row_w, row_h - 1, 0x48FFC040);
        if (other) fill(fb, w, h, vx, vy - 1, row_w, row_h - 1, 0x4070C0FF);
        char line[32], nm[16];
        snprintf(line, sizeof line, "%d", rank + 1);
        text(f, fb, w, h, vx + 6, vy + 3, line, c);
        portrait_mini(rom, palette, ch, vx + 30, vy + 16, fb, w, h);
        snprintf(nm, sizeof nm, "%s", SMK_DRIVERS[ch].name); upper(nm);
        text(f, fb, w, h, vx + 44, vy + 3, nm, me ? gold : other ? p2 : c);
        if (me)    text(f, fb, w, h, vx + 44 + (int)strlen(nm) * 8 + 4, vy + 3, "P1", gold);
        if (other) text(f, fb, w, h, vx + 44 + (int)strlen(nm) * 8 + 4, vy + 3, "P2", p2);
        snprintf(line, sizeof line, "%2d PTS", shown_pts[ch]);
        /* the total flashes while it counts */
        bool counting = t >= T_COUNT && t < T_COUNT + T_COUNT_LEN && ui->gp_award[ch] > 0;
        text(f, fb, w, h, vx + 132, vy + 3, line, counting && ((t / 3) & 1) ? glow : c);
        if (ui->gp_place[ch] > 0 && !final)
            text(f, fb, w, h, vx + 194, vy + 3, ordinal(ui->gp_place[ch]),
                 ui->gp_place[ch] <= 4 ? c : off);
    }

    /* the closing line */
    if (t >= STANDINGS_DONE) {
        if (ui->ranked_out) {
            text_c(f, fb, w, h, 190, "RANKED OUT   THE COURSE AGAIN", off);
            if ((ui->tick / 20) & 1) text_c(f, fb, w, h, 208, "ENTER RETRY", glow);
        } else if (!final) {
            /* the next grid is this race's order (NOTES 275), so the
             * winner of THIS race takes pole, not the points leader */
            int lead = after[0];
            for (int ch = 0; ch < SMK_CHARACTERS; ch++) if (ui->gp_place[ch] == 1) lead = ch;
            char line[48], nm[16];
            snprintf(nm, sizeof nm, "%s", SMK_DRIVERS[lead].name); upper(nm);
            snprintf(line, sizeof line, "%s ON POLE FOR THE NEXT RACE", nm);
            text_c(f, fb, w, h, 190, line, lo);
            if ((ui->tick / 20) & 1) text_c(f, fb, w, h, 208, "ENTER NEXT COURSE", glow);
        } else {
            /* the cup is decided: the champion, and what the human took home */
            int champ = after[0];
            int my_rank = rank_after[ui->player_sel % SMK_CHARACTERS];
            char line[48], nm[16];
            snprintf(nm, sizeof nm, "%s", SMK_DRIVERS[champ].name); upper(nm);
            if (is_p1(ui, champ))      snprintf(line, sizeof line, "YOU ARE THE CHAMPION!");
            else if (is_p2(ui, champ)) snprintf(line, sizeof line, "PLAYER 2 IS THE CHAMPION!");
            else                       snprintf(line, sizeof line, "%s TAKES THE CUP", nm);
            text_c(f, fb, w, h, 190, line, glow);
            const char *mine = my_rank == 0 ? "GOLD TROPHY" : my_rank == 1 ? "SILVER TROPHY"
                             : my_rank == 2 ? "BRONZE TROPHY" : "NO TROPHY   TRY 100CC NEXT";
            if ((ui->tick / 20) & 1) text_c(f, fb, w, h, 208, "ENTER CONTINUE", lo);
            else text_c(f, fb, w, h, 208, mine, my_rank < 3 ? gold : off);
            /* the champion, large, waving, with the cup */
            int burst = t - STANDINGS_DONE;
            float e = ease(burst, 20);
            int cx = 218, cy = 60 + (int)(e * 100.0f);
            bool arms = ((burst / SMK_WIN_TOGGLE) & 1) == 0;
            fill(fb, w, h, cx - 30, cy - 66, 60, 74, 0x60000000);
            portrait(rom, palette, champ, cx, cy, 2, true, arms, fb, w, h);
            if (my_rank < 3) trophy(fb, w, h, cx - 16, cy - 108, my_rank, 2, ui->tick);
            confetti(fb, w, h, ui->tick, 70);
        }
    }
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
    layout(w, h, false);
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
