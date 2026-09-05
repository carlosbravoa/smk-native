/* Headless verification of the C asset pipeline.
 *
 * Checks the same facts the Python suite checks, but through the code the
 * game actually runs, so the two implementations cannot silently diverge.
 */
#include <math.h>
#include "smk.h"
#include <stdio.h>
#include <string.h>

static int pass = 0, fail = 0;

static void check(const char *name, int ok, const char *detail)
{
    printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name,
           detail && *detail ? "  - " : "", detail ? detail : "");
    ok ? pass++ : fail++;
}


/* The decoded player control (src/player.c) replayed against the game.
 * Each fixture is a per-frame log of the attract race's P1 - a human-driven
 * kart - captured field by field from MAME.  We feed the same pad words in
 * and require the ROM's own values back. */
typedef struct { uint16_t c4, ea, e8, a4, a8, aa, fa, b2, a6, e2; } replay_row;

static void replay(const char *name, smk_player *p, const replay_row *F, int n,
                   int32_t accel32, int speed_tol)
{
    static smk_track road;
    memset(&road, 0, sizeof road);
    road.surface[0] = 0x40;                 /* every cell: plain road */
    smk_kart k;
    memset(&k, 0, sizeof k);
    k.x = 512 << SMK_POS_SHIFT; k.y = 512 << SMK_POS_SHIFT;
    smk_player_reset(p, F[0].a4);
    p->vlag = (int16_t)F[0].a8; p->plag = (int16_t)F[0].aa; p->spin = (int16_t)F[0].fa;
    p->turn = (int16_t)F[0].b2; p->state = F[0].a6;
    p->flags = F[0].e2 & 0x8008;
    p->vel_angle = (uint16_t)(p->heading + p->vlag); p->pose = (uint16_t)(p->heading - p->plag);
    k.speed = (int16_t)F[0].ea; k.speed_frac = F[0].e8;
    k.airborne = (F[0].e2 & 0x8000) != 0;
    p->accel32 = accel32;
    int bad = 0, first = -1, maxds = 0;
    for (int i = 1; i < n; i++) {
        uint16_t c4 = F[i].c4;
        uint16_t held = (uint16_t)(c4 & 0xFFF0);
        uint16_t pressed = (uint16_t)(((c4 & 3) << 8) | ((c4 & 0xC) << 2));
        smk_player_step(p, &k, &road, held, pressed);
        int ds = k.speed - (int16_t)F[i].ea; if (ds < 0) ds = -ds;
        if (ds > maxds) maxds = ds;
        bool ok = p->heading == F[i].a4 && p->vlag == (int16_t)F[i].a8
               && p->plag == (int16_t)F[i].aa && p->spin == (int16_t)F[i].fa
               && p->turn == (int16_t)F[i].b2 && p->state == (int)F[i].a6
               && ((p->flags ^ F[i].e2) & 0x8000) == 0;
        if (!ok) {
            bad++;
            if (first < 0) {
                first = i;
                fprintf(stderr, "    replay miss at %d: c4 %04X got a4 %u a8 %d aa %d fa %d b2 %d a6 %02X spd %d | want %u %d %d %d %d %02X %d\n",
                        i, c4, p->heading, p->vlag, p->plag, p->spin, p->turn, p->state, k.speed,
                        F[i].a4, (int16_t)F[i].a8, (int16_t)F[i].aa, (int16_t)F[i].fa,
                        (int16_t)F[i].b2, F[i].a6, (int16_t)F[i].ea);
            }
        }
    }
    char d[128];
    snprintf(d, sizeof d, "%d/%d frames exact, first miss %d, speed within %d",
             n - 1 - bad, n - 1, first, maxds);
    check(name, bad == 0 && maxds <= speed_tol, d);
}

static void test_player_replay(const smk_rom *rom)
{
    static const replay_row SLIDE[] = {
#include "selftest_slide.inc"
    };
    static const replay_row SPIN[] = {
#include "selftest_spin.inc"
    };
    static smk_player p;
    check("player tables load (Mario, 100cc)", smk_player_setup(rom, 0, 1, &p), NULL);
    char d[160];
    snprintf(d, sizeof d, "top %d accel[4] %d cap[10] %d steer[0] %d/%d/%d/%d row2[2] %d",
             p.base_top, p.accel[4], p.cap[10], p.steer[0][0], p.steer[0][1],
             p.steer[0][2], p.steer[0][3], p.drift[2][2]);
    check("player tables match the live $0710 block / $B4 / $80AC36",
          p.base_top == 912 && p.accel[4] == 0x0C00 && p.cap[10] == 0x250
          && p.steer[0][0] == 0x995 && p.steer[0][1] == 0x98 && p.steer[0][2] == 0x68
          && p.steer[0][3] == 0x70 && p.drift[2][2] == 0x1800 && p.drift[7][7] == 0x2900, d);
    /* The two human races pin the class row from the other end: their
     * speed parked at 864 on 50cc and 992 on 100cc, and $D6 = $B4 +
     * 8*min(coins,10) makes those 784+80 and 912+80 (NOTES 173). */
    {
        static smk_player p50, p150;
        int ok = smk_player_setup(rom, 0, 0, &p50)
              && smk_player_setup(rom, 0, 2, &p150);
        snprintf(d, sizeof d, "50cc %d (race parked at %d), 100cc %d (%d), 150cc %d",
                 p50.base_top, p50.base_top + 80, p.base_top, p.base_top + 80,
                 p150.base_top);
        check("$81F026 class row: Mario's two recorded races park at $B4+80",
              ok && p50.base_top + 80 == 864 && p.base_top + 80 == 992
                 && p150.base_top == p.base_top + 0xA0, d);
    }
    /* A kart at a STANDSTILL does not turn.  The user: "When stopped
     * (speed=0) and you press left or right, the cart doesn't turn, the
     * player only leans their head left or right.  Nothing else.  This
     * can be tested easily during count down."
     *
     * It holds because $80A9B8, the turn-per-frame table for speeds under
     * $80, begins 0, 16, 32, 48 and the index is (speed>>4)&7 - so entry
     * ZERO covers speeds 0..15.  The bug was never here: main.c threw the
     * steering away during the countdown, so the driver could not even
     * lean (NOTES 175).  This pins the half that must NOT move. */
    {
        static smk_track trk1; static smk_course crs1;
        char terr1[128];
        /* on the real grid: a kart memset to (0,0) is off the map, falls,
         * and Lakitu resets its heading - which reads as "it turned" */
        if (smk_track_load(rom, 0, -1, &trk1, terr1, sizeof terr1)
            && smk_course_load(rom, 0, &crs1)) {
            static smk_player pl; static smk_kart kk;
            float gx, gy; uint16_t gh;
            smk_course_start(&crs1, SMK_GRID_SLOT(0), &gx, &gy, &gh);
            smk_player_setup(rom, 0, 0, &pl);
            #define PUTKART() do {                                       \
                memset(&kk, 0, sizeof kk);                               \
                kk.x = (int32_t)(gx * SMK_POS_ONE);                      \
                kk.y = (int32_t)(gy * SMK_POS_ONE);                      \
                kk.angle = gh; smk_player_reset(&pl, gh);                \
            } while (0)
            PUTKART();
            for (int f = 0; f < 90; f++)          /* LEFT only, no throttle */
                smk_player_step(&pl, &kk, &trk1, 0x0200, f ? 0 : 0x0200);
            int still = pl.heading, sspd = kk.speed;
            /* the same input WITH throttle does turn it, or this check is
             * passing on a kart that simply never steers at all */
            PUTKART();
            for (int f = 0; f < 90; f++)
                smk_player_step(&pl, &kk, &trk1, 0x8200, f ? 0 : 0x8200);
            #undef PUTKART
            snprintf(d, sizeof d, "at rest $%04X (grid $%04X, speed %d), "
                     "driving $%04X", still, gh, sspd, pl.heading);
            check("$80A9B8[0] = 0: LEFT held at a standstill turns nothing",
                  still == gh && sspd == 0 && pl.heading != gh, d);
        }
    }
    p.coins = 10;                           /* $D6 was 992 in both captures */
    /* the slide capture's fraction steps $4000 per frame (accel $0040 << 8) */
    replay("player replay: hop-into-left power slide, release, plain slide",
           &p, SLIDE, (int)(sizeof SLIDE / sizeof SLIDE[0]), 0x4000, 0);
    /* the spin capture starts inside the spin: accel -16 with the same stale
     * low word; $E8 was not logged, so the speed may differ by one */
    replay("player replay: spin-out, its end, and the $1C settle",
           &p, SPIN, (int)(sizeof SPIN / sizeof SPIN[0]), (int32_t)(-16 * 65536) | 0x4000, 1);
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "rom/smk_usa.sfc";
    char err[512] = {0}, det[256];
    smk_rom rom;

    if (!smk_rom_load(&rom, path, err, sizeof err)) {
        printf("cannot load ROM: %s\n(skipping: supply one with argv[1])\n", err);
        return 77;                /* CTest "skipped" */
    }
    smk_ai_catchup_load(&rom);
    printf("ROM\n");
    check("recognised as Super Mario Kart (USA)", rom.recognised, rom.title);

    printf("\naddress mapping\n");
    check("HiROM $80803A maps to file $0803A",
          smk_snes_to_pc(&rom, 0x80803A) == 0x0803A, NULL);
    check("mirrors collapse ($08 == $80 == $C0)",
          smk_snes_to_pc(&rom, 0x088AED) == smk_snes_to_pc(&rom, 0x808AED) &&
          smk_snes_to_pc(&rom, 0x808AED) == smk_snes_to_pc(&rom, 0xC08AED), NULL);

    printf("\ntracks\n");
    int good = 0, maps_differ = 1;
    static smk_track a, b;
    for (int t = 0; t < SMK_TRACK_COUNT; t++) {
        static smk_track trk;
        if (smk_track_load(&rom, t, -1, &trk, err, sizeof err)) good++;
        else printf("       track %d: %s\n", t, err);
        if (t == 0) memcpy(&a, &trk, sizeof a);
        if (t == 1) memcpy(&b, &trk, sizeof b);
    }
    snprintf(det, sizeof det, "%d/%d", good, SMK_TRACK_COUNT);
    check("all 24 tracks decode to a 128x128 tilemap", good == SMK_TRACK_COUNT, det);

    maps_differ = memcmp(a.map, b.map, SMK_MAP_BYTES) != 0;
    check("different tracks give different tilemaps", maps_differ, NULL);

    /* A track is mostly a handful of surface tiles; a broken decode looks
     * like uniform noise across all 256 possible indices. */
    int hist[256] = {0};
    for (int i = 0; i < SMK_MAP_BYTES; i++) hist[a.map[i]]++;
    int used = 0, top = 0;
    for (int i = 0; i < 256; i++) { if (hist[i]) used++; if (hist[i] > top) top = hist[i]; }
    snprintf(det, sizeof det, "%d distinct tiles, most common covers %d%%",
             used, top * 100 / SMK_MAP_BYTES);
    check("tilemap looks like a track, not noise",
          used < 200 && top > SMK_MAP_BYTES / 20, det);

    printf("\npalette\n");
    int inrange = 1;
    for (int i = 0; i < 256; i++) if (a.palette[i] > 0xFFFFFF) inrange = 0;
    check("256 colours in range", inrange, NULL);

    printf("\nheight and gravity\n");
    {
        /* the exact arc captured from the running game (NOTES 045):
         * launch $0080 from z=$0100 peaks at z=64768 and lands on frame 8 */
        static const int32_t ARC[] = { 26368, 45824, 58624, 64768,
                                       64256, 57088, 43264, 22784, 0 };
        smk_kart k;
        memset(&k, 0, sizeof k);
        k.z = 0x0100;
        smk_kart_launch(&k, SMK_BOUNCE_VEL);   /* the BOUNCE, not the hop */
        int ok = 1, landed_at = -1;
        for (int i = 0; i < (int)(sizeof ARC / sizeof *ARC); i++) {
            smk_kart_gravity(&k);
            if (k.z != ARC[i]) ok = 0;
            if (!k.airborne && landed_at < 0) landed_at = i;
        }
        snprintf(det, sizeof det, "landed frame %d", landed_at);
        check("gravity reproduces the game's arc exactly ($0080 launch)",
              ok && landed_at == 8, det);

        /* the taller arc: $0180 peaks at 10.34 px, landing on frame 29 (the
         * capture logged 31 rows because it ran on past the landing) */
        memset(&k, 0, sizeof k);
        k.z = 0x0100;
        smk_kart_launch(&k, 0x0180);
        int32_t peak = 0;
        int frames = 0;
        while (k.airborne && frames < 200) {
            smk_kart_gravity(&k);
            if (k.z > peak) peak = k.z;
            frames++;
        }
        snprintf(det, sizeof det, "peak %d (%.2f px) over %d frames",
                 (int)peak, peak / 65536.0, frames);
        check("the taller arc matches too ($0180 launch)",
              peak == 677632 && frames == 29, det);
    }

    printf("\ncourse data\n");
    {
        int good = 0, wp_total = 0;
        static smk_course crs;
        for (int tr = 0; tr < SMK_TRACK_COUNT; tr++) {
            if (!smk_course_load(&rom, tr, &crs)) continue;
            int painted = 0;
            for (int i = 0; i < SMK_SECT_CELLS; i++)
                if (crs.map[i] & SMK_SECT_OFF) painted++;
            if (crs.sectors >= 10 && crs.sectors <= 120 && painted > 200
                && crs.wx[0] == crs.wx[crs.sectors]
                && crs.wy[0] == crs.wy[crs.sectors])
                good++;
            wp_total += crs.sectors;
        }
        snprintf(det, sizeof det, "%d/%d, %d waypoints", good,
                 SMK_TRACK_COUNT, wp_total);
        check("every course loads sectors and a closed racing line",
              good == SMK_TRACK_COUNT, det);

        /* values confirmed against the running game (NOTES 042) */
        smk_course_load(&rom, 7, &crs);
        check("track 7 matches the live game",
              crs.sectors == 30 && crs.wx[0] == 896 && crs.wy[0] == 424
              && crs.wx[1] == 832 && crs.wy[1] == 360, NULL);
        int strip = 0;
        for (int i = 0; i < SMK_SECT_CELLS; i++)
            if ((crs.map[i] & SMK_SECT_FINISH) && (crs.map[i] & SMK_SECT_OFF)
                && (crs.map[i] & SMK_SECT_OFF) != SMK_SECT_OFF) strip++;
        snprintf(det, sizeof det, "%d cells", strip);
        check("track 7 finish strip covers 65 on-track cells", strip == 65, det);

        /* objects ($85:D000 + track*128), pinned to the live capture */
        check("track 7 has 21 objects, first an item box at (80,280)",
              crs.nobj == 21 && crs.obj[0].kind == 1
              && crs.obj[0].x == 80 && crs.obj[0].y == 280, NULL);
    }

    printf("\nstarting grid\n");
    {
        /* The grid is the game's own record now (NOTES 161), so this can
         * ask the strong question: every one of the eight slots, and the
         * time trial's own start, on drivable ground on all 20 courses. */
        int slots = 0, want = 0, solo = 0;
        for (int tr = 0; tr < 20; tr++) {
            static smk_track tt;
            static smk_course cc;
            if (!smk_track_load(&rom, tr, -1, &tt, err, sizeof err)) continue;
            if (!smk_course_load(&rom, tr, &cc)) continue;
            for (int sl = 0; sl < 8; sl++, want++) {
                float sx, sy; uint16_t sh;
                smk_course_start(&cc, sl, &sx, &sy, &sh);
                if (!smk_surface_solid(smk_track_surface(&tt, (int)sx, (int)sy)))
                    slots++;
            }
            float tx, ty; uint16_t th;
            smk_course_start_solo(&cc, &tx, &ty, &th);
            if (!smk_surface_solid(smk_track_surface(&tt, (int)tx, (int)ty)))
                solo++;
        }
        snprintf(det, sizeof det, "%d/%d slots, %d/20 solo", slots, want, solo);
        check("every grid slot is on drivable ground",
              slots == want && solo == 20, det);

        /* The three positions the game itself was measured at
         * (tools/labs/gridtable.py, and the two time-trial recordings). */
        static smk_course c7;
        float px, py; uint16_t ph;
        int pinned = 0;
        if (smk_course_load(&rom, 7, &c7)) {
            smk_course_start(&c7, 0, &px, &py, &ph);
            pinned += (px == 920.0f && py == 588.0f && ph == 0);
            smk_course_start(&c7, 7, &px, &py, &ph);
            pinned += (px == 952.0f && py == 756.0f);
            smk_course_start_solo(&c7, &px, &py, &ph);
            pinned += (px == 928.0f && py == 572.0f);
        }
        snprintf(det, sizeof det, "%d/3", pinned);
        check("track 7's grid matches the game's own", pinned == 3, det);
    }

    printf("\nthe AI only teleports when it is stuck\n");
    {
        /* The user: "AI players tend to teleport to get their positions
         * corrected: they often disappear and re-appear a few meters
         * further."  A rescue IS a jump, and a genuinely stuck kart
         * should get one - so the test is not "never", it is "only when
         * it had gone nowhere".  Every jump must be justified by the
         * kart's own net displacement over the preceding window
         * (NOTES 169). */
        static smk_track t9; static smk_course c9; static smk_physics p9;
        static int hx[SMK_AI_RESCUE_FRAMES], hy[SMK_AI_RESCUE_FRAMES];
        int jumps = 0, unjust = 0, worst_t = -1;
        for (int tr = 0; tr < 20; tr++) {
            if (!smk_track_load(&rom, tr, -1, &t9, err, sizeof err)) continue;
            if (!smk_course_load(&rom, tr, &c9)) continue;
            if (!smk_physics_load(&rom, 0, &p9)) continue;
            static smk_racer r9;
            smk_racer_start(&r9, &c9, 0);
            r9.character = 0;
            int px = smk_kart_px(r9.k.x), py = smk_kart_px(r9.k.y);
            for (int i = 0; i < SMK_AI_RESCUE_FRAMES; i++) { hx[i] = px; hy[i] = py; }
            for (int f = 0; f < 2400; f++) {
                smk_racer_step(&r9, &t9, &c9, &p9);
                int nx = smk_kart_px(r9.k.x), ny = smk_kart_px(r9.k.y);
                int dx = nx - px, dy = ny - py;
                if (dx >  512) dx -= 1024;
                if (dx < -512) dx += 1024;
                if (dy >  512) dy -= 1024;
                if (dy < -512) dy += 1024;
                if (dx * dx + dy * dy > 32 * 32) {
                    /* it jumped: was it going nowhere beforehand? */
                    int i = f % SMK_AI_RESCUE_FRAMES;
                    int ax = px - hx[i], ay = py - hy[i];
                    if (ax >  512) ax -= 1024;
                    if (ax < -512) ax += 1024;
                    if (ay >  512) ay -= 1024;
                    if (ay < -512) ay += 1024;
                    jumps++;
                    if (ax * ax + ay * ay
                        > SMK_AI_STUCK_PX * SMK_AI_STUCK_PX) {
                        unjust++;
                        worst_t = tr;
                    }
                }
                hx[f % SMK_AI_RESCUE_FRAMES] = nx;
                hy[f % SMK_AI_RESCUE_FRAMES] = ny;
                px = nx; py = ny;
            }
        }
        snprintf(det, sizeof det,
                 "%d rescues over 20 courses, %d of them unjustified%s",
                 jumps, unjust, unjust ? " (track shown)" : "");
        if (unjust) snprintf(det + strlen(det), sizeof det - strlen(det),
                             " %d", worst_t);
        check("no kart that is travelling is ever fished up", !unjust, det);
    }

    printf("\nLakitu's lap sign\n");
    {
        /* The path and the assembly are the game's own OAM at a
         * lap-completing crossing (NOTES 168). */
        smk_lapsign a, b3, c3, d3;
        smk_lapsign_frame(0, 2, 5, &a);
        smk_lapsign_frame(2, 2, 5, &b3);
        smk_lapsign_frame(40, 2, 5, &c3);
        smk_lapsign_frame(80, 2, 5, &d3);
        snprintf(det, sizeof det, "f2 (%d,%d), f40 (%d,%d), f80 (%d,%d)",
                 b3.x, b3.y, c3.x, c3.y, d3.x, d3.y);
        check("the sign flies the path the game's OAM shows",
              !a.on && b3.on && b3.x == 5 && b3.y == -39
              && c3.x == 63 && c3.y == 22 && d3.x == 91 && d3.y == 44, det);

        smk_lapsign_frame(SMK_LAPSIGN_FRAMES, 2, 5, &a);
        smk_lapsign_frame(40, 3, 5, &b3);
        smk_lapsign_frame(40, 4, 5, &c3);
        smk_lapsign_frame(40, 5, 5, &d3);
        snprintf(det, sizeof det, "lap 3 $%02X, lap 4 $%02X, final %s, past the end %s",
                 b3.digit, c3.digit, d3.final_lap ? "yes" : "NO",
                 a.on ? "STILL ON" : "gone");
        /* Lap 2 must come out pixel-identical to the game's own
         * assembly - plate 16x16 at X plus ONE 16x16 at X+8 - because
         * that is the frame the capture holds.  Any other lap then
         * follows by construction (NOTES 168b). */
        {
            static smk_hud h2;
            smk_hud_load(&rom, &h2);
            uint8_t game[16][32], mine[16][32];
            memset(game, 0, sizeof game); memset(mine, 0, sizeof mine);
            #define PUT(D, OX, OY, T) do {                                \
                const uint8_t *q = smk_hud_tile_px(&h2, (T));             \
                if (q) for (int yy = 0; yy < 8; yy++)                     \
                    for (int xx = 0; xx < 8; xx++)                        \
                        if (q[yy * 8 + xx]) (D)[(OY) + yy][(OX) + xx] = q[yy * 8 + xx]; \
            } while (0)
            /* the hardware order: the digit sprite (bar + numeral) is the
             * higher OAM entry, so the plate paints over its left half */
            PUT(game, 8, 0, 0xA3); PUT(game, 16, 0, 0xA4);
            PUT(game, 8, 8, 0xB3); PUT(game, 16, 8, 0xB4);
            PUT(game, 0, 0, 0xA0); PUT(game, 8, 0, 0xA1);
            PUT(game, 0, 8, 0xB0); PUT(game, 8, 8, 0xB1);
            smk_lapsign lp2; smk_lapsign_frame(40, 2, 5, &lp2);
            for (int r = 0; r < 2; r++) {
                PUT(mine, 8,  r * 8, SMK_LAPSIGN_BAR + r * 16);
                PUT(mine, 16, r * 8, lp2.digit + r * 16);
                PUT(mine, 0,  r * 8, lp2.plate + r * 16);
                PUT(mine, 8,  r * 8, lp2.plate + 1 + r * 16);
            }
            #undef PUT
            int bad = 0;
            for (int yy = 0; yy < 16; yy++)
                for (int xx = 0; xx < 32; xx++)
                    if (game[yy][xx] != mine[yy][xx]) bad++;
            snprintf(det, sizeof det, "%d px differ", bad);
            check("lap 2's sign is pixel-identical to the captured frame",
                  bad == 0, det);
        }

        smk_lapsign_frame(40, 2, 5, &a);
        snprintf(det, sizeof det, "lap 2 $%02X, 3 $%02X, 4 $%02X, final %s",
                 a.digit, b3.digit, c3.digit, d3.final_lap ? "yes" : "NO");
        /* one column per lap: $A4/$A5/$A6 are 2/3/4.  Reading the numeral
         * as half of a 16x16 drew "34" on lap 4 (NOTES 168b). */
        check("lap 2/3/4 pick $A4/$A5/$A6 and the last lap its own plate",
              a.digit == 0xA4 && b3.digit == 0xA5 && c3.digit == 0xA6
              && d3.final_lap && d3.plate == SMK_LAPSIGN_FINAL_L, det);
    }

    printf("\nLakitu on a rescue\n");
    {
        /* His row is the game's own, frame by frame - and the shape is
         * the point: he HOLDS, then RISES, then comes down.  A ramp from
         * the kart's height cannot do that, and the first port drove it
         * from $1E, the low word of a 24-bit height, which alternates
         * 0/-32768 (NOTES 169a). */
        int y0 = smk_rescue_y(0), y19 = smk_rescue_y(19);
        int y49 = smk_rescue_y(49), yend = smk_rescue_y(95);
        int rises = 0, falls = 0;
        for (int t = 1; t < SMK_RESCUE_FRAMES; t++) {
            int d = smk_rescue_y(t) - smk_rescue_y(t - 1);
            if (d < 0) rises++;
            if (d > 0) falls++;
        }
        snprintf(det, sizeof det, "f0 %d, f19 %d, f49 %d, end %d; %d up %d down",
                 y0, y19, y49, yend, rises, falls);
        check("he holds, rises to -56, then comes down to +38",
              y0 == -40 && y19 == -40 && y49 == -56 && yend == 38
              && rises > 0 && falls > rises, det);
        /* past the end he stays put rather than running off the table */
        check("the path is clamped at both ends",
              smk_rescue_y(-5) == y0
              && smk_rescue_y(SMK_RESCUE_FRAMES * 4) == yend, det);
    }

    printf("\nthe rubber band\n");
    {
        /* The two tables are the ROM's; read them back out of it rather
         * than trusting the copy (NOTES 167). */
        int tbl = 0;
        for (int cl = 0; cl < 4; cl++)
            for (int rk = 0; rk < 8; rk++) {
                uint32_t a = smk_snes_to_pc(&rom, 0x80AF0Fu + cl * 16u + rk * 2u);
                unsigned v = rom.data[a] | rom.data[a + 1] << 8;
                if (v == SMK_AI_CATCHUP[cl][rk]) tbl++;
            }
        for (int rk = 0; rk < 8; rk++) {
            uint32_t a = smk_snes_to_pc(&rom, 0x80B0A1u + rk * 2u);
            /* and the two tables NOTES 277/278 added: the decel rates the
             * original's AI steps down by (read in play: -4/-8/-16/-24,
             * never more) and the handicap bonus that replaces the rank
             * penalty for karts 4-7 (2/4/8/16/0, confirmed by their
             * maxima in three recordings) */
            if (rk == 0) {
                smk_ai_catchup_load(&rom);
                static const int16_t DEC[4] = { -4, -8, -16, -24 }, BON[5] = { 2, 4, 8, 16, 0 };
                bool ok = true;
                for (int i = 0; i < 4; i++) ok = ok && SMK_AI_DECEL[i] == DEC[i];
                for (int i = 0; i < 5; i++) ok = ok && SMK_AI_DA_BONUS[i] == BON[i];
                snprintf(det, sizeof det, "decel %d %d %d %d  bonus %d %d %d %d %d",
                         SMK_AI_DECEL[0], SMK_AI_DECEL[1], SMK_AI_DECEL[2], SMK_AI_DECEL[3],
                         SMK_AI_DA_BONUS[0], SMK_AI_DA_BONUS[1], SMK_AI_DA_BONUS[2], SMK_AI_DA_BONUS[3], SMK_AI_DA_BONUS[4]);
                check("$80B064 decel rates and $80B099 handicap bonus read as the recordings show", ok, det);
                /* THE ATTACK's tables (NOTES 279): the mask rows by
                 * character and victim rank, the types, the windows */
                smk_ai_attack_load(&rom);
                bool ta = SMK_AI_ATTACK_MASK[4][0] == 0 && SMK_AI_ATTACK_MASK[4][6] == 3
                       && SMK_AI_ATTACK_MASK[5][0] == 3 && SMK_AI_ATTACK_MASK[0][6] == 0xFFFF
                       && SMK_AI_ATTACK_MASK[2][5] == 0 && SMK_AI_ATTACK_MASK[2][6] == 0xFFFF
                       && SMK_AI_ATTACK_TYPE[0][0] == 0x0C && SMK_AI_ATTACK_TYPE[0][1] == 0x08
                       && SMK_AI_ATTACK_TYPE[4][0] == 0x0A && SMK_AI_ATTACK_TYPE[4][3] == 0x04
                       && SMK_AI_ATTACK_WIN[0][0] == 0xC0 && SMK_AI_ATTACK_WIN[0][1] == 0x10
                       && SMK_AI_ATTACK_WIN[1][0] == 0x90 && SMK_AI_ATTACK_WIN[1][1] == 0x30
                       && SMK_AI_ATTACK_WIN[2][0] == 0x40 && SMK_AI_ATTACK_WIN[2][1] == 0x20;
                snprintf(det, sizeof det, "DK mask %d/%d Yoshi %d Mario r6 %04X; types %02X %02X %02X %02X; windows %d-%d %d-%d %d-%d",
                         SMK_AI_ATTACK_MASK[4][0], SMK_AI_ATTACK_MASK[4][6], SMK_AI_ATTACK_MASK[5][0], SMK_AI_ATTACK_MASK[0][6],
                         SMK_AI_ATTACK_TYPE[0][0], SMK_AI_ATTACK_TYPE[0][1], SMK_AI_ATTACK_TYPE[4][0], SMK_AI_ATTACK_TYPE[4][3],
                         SMK_AI_ATTACK_WIN[0][1], SMK_AI_ATTACK_WIN[0][0], SMK_AI_ATTACK_WIN[1][1], SMK_AI_ATTACK_WIN[1][0],
                         SMK_AI_ATTACK_WIN[2][1], SMK_AI_ATTACK_WIN[2][0]);
                check("the attack tables: masks by character and victim rank, the types, the windows ($80EF95/$80F007)", ta, det);
                /* CPU RULES (NOTES 281): FAIR halves the handicap bonus and
                 * takes the cosine at the ramp; ORIGINAL keeps both */
                {
                    smk_kart ko = { .speed = 1000 }, kf = { .speed = 1000 };
                    smk_cpu_rules = SMK_CPU_ORIGINAL; smk_kart_ramp(&ko, 1); int bo = smk_ai_da_bonus(6);
                    smk_cpu_rules = SMK_CPU_FAIR;     smk_kart_ramp(&kf, 1); int bf = smk_ai_da_bonus(6);
                    smk_cpu_rules = SMK_CPU_ORIGINAL;
                    snprintf(det, sizeof det, "ramp speed original %d fair %d; Peach's bonus original %d fair %d", ko.speed, kf.speed, bo, bf);
                    check("CPU RULES: FAIR takes the cosine at a ramp and halves the handicap bonus", ko.speed == 1000 && kf.speed > 930 && kf.speed < 950 && bo == 16 && bf == 8, det);
                }
                /* and the machine: DK Jr leading a player 100 px behind
                 * on lap 2 arms on the 61st frame and drops at once; the
                 * next attack waits the 180-frame cooldown; a leading
                 * player with DK Jr 100 px behind gets a forward throw */
                {
                    smk_ai_attack st; smk_ai_attack_init(&st, 1);
                    smk_racer rr[8]; memset(rr, 0, sizeof rr);
                    smk_proj pj[SMK_PROJ_MAX]; memset(pj, 0, sizeof pj);
                    for (int i = 0; i < 8; i++) { rr[i].character = i; rr[i].rank = i; rr[i].lap = 2; rr[i].k.speed = 700; rr[i].k.x = (int32_t)(500 + i * 100) * SMK_POS_ONE; rr[i].k.y = 500 * SMK_POS_ONE; }
                    rr[4].character = 4; rr[0].character = 0;
                    /* DK Jr (slot 4) rank 0 at x 500, the player (slot 0) rank 1 at x 600 */
                    rr[4].rank = 0; rr[0].rank = 1; rr[4].k.x = 500 * SMK_POS_ONE; rr[0].k.x = 600 * SMK_POS_ONE;
                    for (int i = 1; i < 8; i++) if (i != 4) rr[i].rank = i + 1 > 7 ? 7 : i + 1;
                    rr[1].rank = 2; rr[2].rank = 3; rr[3].rank = 4; rr[5].rank = 5; rr[6].rank = 6; rr[7].rank = 7;
                    bool hum[8] = { true };
                    int first = -1, second = -1, t1 = 0, t2 = 0, who = -1;
                    for (int f = 0; f < 400; f++) {
                        int ty = 0; int w = smk_ai_attack_step(&st, rr, 8, hum, pj, SMK_PROJ_MAX, &ty);
                        if (w >= 0 && first < 0) { first = f; t1 = ty; who = w; }
                        else if (w >= 0 && second < 0) { second = f; t2 = ty; }
                        for (int i = 0; i < SMK_PROJ_MAX; i++) pj[i].kind = SMK_PROJ_NONE;   /* the block is freed again */
                    }
                    /* the player leads, DK Jr 100 px behind: a throw */
                    smk_ai_attack_init(&st, 1);
                    rr[0].rank = 0; rr[4].rank = 1; rr[0].k.x = 500 * SMK_POS_ONE; rr[4].k.x = 600 * SMK_POS_ONE;
                    int tf = -1, tt = 0;
                    for (int f = 0; f < 400 && tf < 0; f++) { int ty = 0; if (smk_ai_attack_step(&st, rr, 8, hum, pj, SMK_PROJ_MAX, &ty) >= 0) { tf = f; tt = ty; } }
                    snprintf(det, sizeof det, "first at f%d type $%02X by slot %d, second at f%d type $%02X (gap %d); leader case at f%d type $%02X",
                             first, t1, who, second, t2, second - first, tf, tt);
                    /* armed on the 61st frame, fired on the next - the recordings' 4775 -> 4776;
                     * then 180 of cooldown, 61 of adjacency and the firing frame: 242, the
                     * 100cc race's fastest real gap being 243 */
                    check("the attack machine: arms on the 61st frame, fires the next, waits 180, and throws forward at a leader",
                          first == 62 && t1 == 0x04 && who == 4 && second - first == 242 && tf == 62 && tt == 0x0A, det);
                }
            }
            int v = (int16_t)(rom.data[a] | rom.data[a + 1] << 8);
            if (v == SMK_AI_RANK_BONUS[rk]) tbl++;
        }
        snprintf(det, sizeof det, "%d/40 entries", tbl);
        check("the catch-up distances and rank bonuses are the ROM's own",
              tbl == 40, det);

        /* the band itself: a kart far behind the one ahead chases, one in
         * the pack holds, and a leader with clear air eases off */
        static smk_course cr7;
        static smk_racer field[SMK_CHARACTERS];
        smk_course_load(&rom, 7, &cr7);
        #define PUT(I, LAP, SEC, PX, PY) do {                        \
            memset(&field[I], 0, sizeof field[I]);                   \
            field[I].lap = (LAP); field[I].sector = (SEC);           \
            field[I].k.x = (int32_t)(PX) << SMK_POS_SHIFT;           \
            field[I].k.y = (int32_t)(PY) << SMK_POS_SHIFT;           \
        } while (0)
        /* $80ADA0 turns on ONE thing above all: whether the neighbouring
         * kart is the human player ($10 bit 15).  Same geometry, same
         * ranks, only the player moved - the rows must differ, or the
         * rubber band is not a rubber band (NOTES 174). */
        smk_ai_player_block = 0;
        for (int i = 0; i < SMK_CHARACTERS; i++) PUT(i, 0, 0, 500, 900);
        PUT(0, 1, 4, 500, 560);            /* the player, 60 px behind */
        PUT(1, 1, 5, 500, 500);            /* leading */
        smk_ai_rubber(field, 2, &cr7, 0);
        int with_player = field[1].row;
        smk_ai_player_block = 9;           /* nobody is the player now */
        PUT(0, 1, 4, 500, 560);
        PUT(1, 1, 5, 500, 500);
        smk_ai_rubber(field, 2, &cr7, 0);
        int without = field[1].row;
        smk_ai_player_block = 0;
        snprintf(det, sizeof det, "player behind -> row %d, AI behind -> row %d",
                 with_player, without);
        check("the leader's row responds to the PLAYER being behind it",
              with_player != without, det);

        /* $80ADB0: a kart in trouble takes the slowest row, whatever the
         * geometry says.  6% of kart-frames in the recorded race. */
        PUT(0, 1, 4, 500, 560);
        PUT(1, 1, 5, 500, 500);
        field[1].k.crash_frames = 20;
        smk_ai_rubber(field, 2, &cr7, 0);
        snprintf(det, sizeof det, "row %d", field[1].row);
        check("$80ADB0: a kart in trouble drops to the $18 row",
              field[1].row == SMK_AI_ROW_SLOW, det);
        field[1].k.crash_frames = 0;

        /* $80AF0F is the ROM's own 8 rows, overrun and all: rows 5-7 are
         * the CODE of $80AF5F, and the original indexes into them. */
        {
            uint32_t a5 = smk_snes_to_pc(&rom, 0x80AF0Fu + 5u * 16u);
            unsigned v5 = rom.data[a5] | rom.data[a5 + 1] << 8;
            snprintf(det, sizeof det, "row5[0] = $%04X (the bytes of $80AF5F)",
                     SMK_AI_CATCHUP[5][0]);
            check("the catch-up table keeps the original's overrun rows",
                  SMK_AI_CATCHUP[5][0] == v5 && v5 != 0, det);
        }

        /* The mover clear height, against the user's own recorded run.
         *
         * SMK_MOVER_CLEAR is OURS (ledgered): seven rigs failed to measure
         * the game's rule, and the user set it - "one kart sprite in
         * altitude, you can pass.  I would even say 80% of it".  What CAN
         * be checked is their Bowser Castle recording, where every crash
         * and every close pass was logged with the Thwomp's height
         * (tools/labs/thwomppass.py).  Those samples cannot pin the number
         * - there are none between 960 and 2880 - but they refute anything
         * outside that band, and they are the gate. */
        {
            static const int CRASHED[] = { 0, 0, 0, 0, 448, 960 };
            static const int PASSED[]  = { 2880, 3008, 3648, 4096 };
            static smk_course cm;
            int bad = 0, tested = 0;
            if (smk_course_load(&rom, 17, &cm) && cm.nent > 0) {
                for (int q = 0; q < 10; q++) {
                    int z = q < 6 ? CRASHED[q] : PASSED[q - 6];
                    int want_hit = q < 6;
                    smk_course_movers_reset(&cm);
                    for (int i = 0; i < 32; i++) cm.mv[i].z = (int16_t)z;
                    smk_kart kt; memset(&kt, 0, sizeof kt);
                    kt.x = (int32_t)(cm.ent[0].x - 20) << SMK_POS_SHIFT;
                    kt.y = (int32_t)(cm.ent[0].y)      << SMK_POS_SHIFT;
                    kt.speed = 480; kt.vx = 480; kt.vy = 0;
                    int hit = 0;
                    for (int f = 0; f < 12 && !hit; f++) {
                        smk_collide_objects(&kt, &cm);
                        /* a hit marks the kart as a wall does ($10 bit 12):
                         * the rev halves and the wall pair sounds (NOTES 287) */
                        if (kt.bounce_cool) hit = kt.bounce_hit ? 1 : 2;
                        kt.x += (int32_t)kt.vx << (SMK_POS_SHIFT - 8);
                        kt.y += (int32_t)kt.vy << (SMK_POS_SHIFT - 8);
                    }
                    tested++;
                    if (hit != want_hit) bad++;
                }
            }
            snprintf(det, sizeof det, "clear at %d; %d/%d recorded samples agree, every hit marked as a wall's",
                     SMK_MOVER_CLEAR, tested - bad, tested);
            check("a mover clears a kart at SMK_MOVER_CLEAR, and the user's "
                  "recorded run agrees", tested == 10 && bad == 0, det);
        }

        /* THE ROULETTE, against the user's own race (NOTES 185).  Frame
         * 2167 of `flag` hits a box on Mario Circuit 1 while leading on
         * lap 2+; the game rolled red (5) from sequence 4.  The roll is the
         * one thing not reproducible, so the outcome is forced and every
         * frame after it must match: 193 frames of stepping every fourth
         * frame, the stop on the target once the count is negative, 64
         * frames of hold, then READY. */
        {
            static const struct { int f; unsigned w, t; } R[] = {
#include "selftest_roulette.inc"
            };
            static smk_itemtab tab;
            smk_item it;
            int n = (int)(sizeof R / sizeof R[0]), ok = 0, first_bad = -1;
            if (smk_items_load(&rom, &tab)) {
                smk_item_tables = &tab;
                smk_item_box(&it, &tab, 7, 2, 0, 0);
                it.seq = 4; it.target = 5;              /* the game's roll */
                for (int i = 0; i < n; i++) {
                    if (R[i].f < 2167) { ok++; continue; }       /* before the box */
                    if (R[i].f > 2167) smk_item_step(&it, false, true);
                    bool same = it.word == R[i].w && (uint16_t)it.timer == R[i].t;
                    if (same) ok++; else if (first_bad < 0) first_bad = i;
                }
            }
            snprintf(det, sizeof det, "%d/%d frames exact%s", ok, n,
                     first_bad < 0 ? "" : "");
            if (first_bad >= 0)
                snprintf(det + strlen(det), sizeof det - strlen(det),
                         "; first miss f%d: ours $%04X/$%04X, game $%04X/$%04X",
                         R[first_bad].f, it.word, (uint16_t)it.timer,
                         R[first_bad].w, R[first_bad].t);
            check("the item roulette replays the user's race frame-exact",
                  n > 0 && ok == n, det);
            /* and the outcome tables are the ROM's: MC1 leader lap 2 -> seq 4,
             * and the recorded roll landed inside record 1's live set */
            smk_item_box(&it, &tab, 7, 2, 0, 21);     /* roll 21 -> red */
            snprintf(det, sizeof det, "MC1 lap2 P1: seq %d target %d (game: seq 4, red=5)",
                     it.seq, it.target);
            check("MC1's item block picks sequence 4 for the leader", it.seq == 4 && it.target == 5, det);
        }

        /* THE ITEMS' EFFECTS, each against its measurement (NOTES 185):
         * a shell leaves at the kart's speed + $300; a red shell turns
         * toward its target by at most $400 a frame; a banana hit spins
         * for 60 frames from a speed clamped to $300; a shell hit tumbles
         * with a rate that decays $40 a frame from $1000. */
        {
            static smk_track trk7; static smk_course cr7b;
            char e7[128];
            if (smk_track_load(&rom, 7, -1, &trk7, e7, sizeof e7) && smk_course_load(&rom, 7, &cr7b)) {
                smk_proj pj[SMK_PROJ_MAX]; memset(pj, 0, sizeof pj);
                smk_kart kk; memset(&kk, 0, sizeof kk);
                /* on MC1's own grid, or the shell dies on the grass first frame */
                float gx7, gy7; uint16_t gh7;
                smk_course_start(&cr7b, SMK_GRID_SLOT(0), &gx7, &gy7, &gh7);
                kk.x = (int32_t)(gx7 * SMK_POS_ONE); kk.y = (int32_t)(gy7 * SMK_POS_ONE);
                kk.speed = 500; kk.angle = 0;
                smk_proj_throw(pj, SMK_PROJ_MAX, SMK_PROJ_GREEN, &kk, 0, 0, -1, false, false);
                snprintf(det, sizeof det, "speed %d (kart 500 + $300 = 1268), heading $%04X",
                         pj[0].speed, pj[0].heading);
                check("a green shell leaves at the kart's speed + $300", pj[0].speed == 1268, det);
                /* red: a target due east; the shell heads north and must turn
                 * no faster than $400 a frame after the delay */
                memset(pj, 0, sizeof pj);
                smk_kart tgt = kk; tgt.x += (int32_t)200 << SMK_POS_SHIFT;
                smk_proj_throw(pj, SMK_PROJ_MAX, SMK_PROJ_RED, &kk, 0, 0, 1, false, false);
                const smk_kart *fk[2] = { &kk, &tgt };
                uint16_t h0 = pj[0].heading; int maxstep = 0;
                for (int f = 0; f < 12; f++) {
                    uint16_t before = pj[0].heading;
                    smk_proj_step(pj, SMK_PROJ_MAX, &trk7, fk, 2);
                    int st = (int16_t)(uint16_t)(pj[0].heading - before);
                    if (st < 0) st = -st;
                    if (st > maxstep) maxstep = st;
                }
                snprintf(det, sizeof det, "from $%04X to $%04X in 12 frames, largest step $%04X",
                         h0, pj[0].heading, maxstep);
                check("a red shell turns toward its target by at most $400 a frame",
                      pj[0].heading != h0 && maxstep <= 0x400 && (int16_t)pj[0].heading > 0, det);
                /* the banana hit */
                static smk_player pb; smk_player_setup(&rom, 0, 0, &pb); smk_player_reset(&pb, 0);
                smk_kart kb; memset(&kb, 0, sizeof kb); kb.speed = 900;
                bool ok1 = smk_player_hit_banana(&pb, &kb);
                snprintf(det, sizeof det, "state $%02X, speed %d, $FA %d", pb.state, kb.speed, pb.spin);
                check("a banana hit: state $0A/$0C, speed to $300, 60 frames of spin",
                      ok1 && (pb.state == 0x0A || pb.state == 0x0C) && kb.speed == 0x300 && pb.spin == 60, det);
                /* the shell hit */
                smk_player_reset(&pb, 0); kb.speed = 900;
                bool ok2 = smk_player_hit_shell(&pb, &kb, 1);
                snprintf(det, sizeof det, "state $%02X drive $%02X tumble $%04X speed %d",
                         pb.state, pb.drive, pb.tumble, kb.speed);
                check("a shell hit: state $1A, drive $14, tumble $1000, the knock's $180",
                      ok2 && pb.state == 0x1A && pb.drive == 0x14 && pb.tumble == 0x1000 && kb.speed == 0x180, det);
                /* and a star shrugs both off */
                smk_player_reset(&pb, 0); smk_player_star(&pb);
                check("a star kart ignores a banana and a shell",
                      !smk_player_hit_banana(&pb, &kb) && !smk_player_hit_shell(&pb, &kb, 0), NULL);
                /* the coinless bump: $80B435 sets the state and nothing else
                 * (tools/labs/bumpspin.py: 835 -> 0 in 56 frames at $480 a frame) */
                smk_player_reset(&pb, 0); kb.speed = 835; pb.drive = 0x10; pb.plag = 100;
                bool ok3 = smk_player_hit_bump(&pb, &kb);
                snprintf(det, sizeof det, "state $%02X drive $%02X speed %d vlag %d", pb.state, pb.drive, kb.speed, pb.vlag);
                check("a bump with no coins: state $10 (pose lag +), drive and speed untouched",
                      ok3 && pb.state == 0x10 && pb.drive == 0x10 && kb.speed == 835 && pb.vlag == 0
                      && !smk_player_hit_bump(&pb, &kb), det);
                /* the AI's drop (NOTES 190): rides behind its kart 58 frames, then stays */
                {
                    smk_proj pl[4]; memset(pl, 0, sizeof pl);
                    smk_kart ok; memset(&ok, 0, sizeof ok);
                    ok.x = 500 << SMK_POS_SHIFT; ok.y = 500 << SMK_POS_SHIFT; ok.angle = 0; ok.vy = -3 * 256;
                    smk_proj_ai_drop(pl, 4, SMK_PROJ_BANANA, &ok, 3);
                    const smk_kart *ks[8] = { 0 }; ks[3] = &ok;
                    int follow = 0;
                    for (int f = 0; f < 58; f++) {
                        ok.y -= 3 << SMK_POS_SHIFT;                      /* the kart drives on */
                        smk_proj_step(pl, 4, &trk7, ks, 8);
                        int dyb = smk_kart_px(pl[0].y) - smk_kart_px(ok.y);
                        if ((dyb == 8 || dyb == 7 || dyb == -8 || dyb == -7) && smk_proj_hit(pl, 4, &ok, 3) == SMK_PROJ_NONE) follow++;
                        if (f == 0) snprintf(det, sizeof det, "first frame dy %d", dyb);
                    }
                    int32_t yy = pl[0].y;
                    for (int f = 0; f < 30; f++) { ok.y -= 3 << SMK_POS_SHIFT; smk_proj_step(pl, 4, &trk7, ks, 8); }
                    snprintf(det + strlen(det), sizeof det - strlen(det), "; followed %d/58 frames, then moved %d px; weapons %d %d %d %d %d %d %d %d",
                             follow, smk_kart_px(pl[0].y) - smk_kart_px(yy),
                             smk_ai_weapon_of(0), smk_ai_weapon_of(1), smk_ai_weapon_of(2), smk_ai_weapon_of(3),
                             smk_ai_weapon_of(4), smk_ai_weapon_of(5), smk_ai_weapon_of(6), smk_ai_weapon_of(7));
                    check("an AI drop rides 8 px behind its kart for 58 frames, immune to it, then stays put",
                          follow == 58 && pl[0].kind == SMK_PROJ_BANANA && pl[0].y == yy
                          && smk_ai_weapon_of(4) == SMK_PROJ_BANANA && smk_ai_weapon_of(0) == SMK_AI_WEAPON_STAR
                          && smk_ai_weapon_of(2) == SMK_PROJ_FIREBALL, det);
                }
                /* the cup (NOTES 198): five courses, 9/6/3/1 from $85:BEB4,
                 * a retry when ranked out, the cup over after the fifth */
                {
                    smk_ui u; memset(&u, 0, sizeof u); smk_ui_init(&u);
                    smk_ui_input go = { false, false, false, false, true, false };
                    smk_ui_input none = { 0 };
                    u.screen = SMK_UI_MODE; u.mode_sel = SMK_UI_MODE_GP; smk_ui_step(&u, &rom, &go);   /* -> class  */
                    smk_ui_step(&u, &rom, &go);                                                   /* -> player */
                    smk_ui_step(&u, &rom, &go);                                                   /* -> course */
                    u.cup_sel = 0; bool started = smk_ui_step(&u, &rom, &go);                     /* -> race 0 */
                    int t0 = u.track;
                    smk_ui_result r; memset(&r, 0, sizeof r); r.entries = 8; r.position = 2;
                    for (int p = 0; p < 8; p++) { r.field[p].character = (p + 3) % 8; r.field[p].total = 1000 + p; }
                    r.field[1].player = 1; u.player_sel = r.field[1].character;
                    smk_ui_gp_award(&u, &r);
                    int pts_me = u.gp_points[u.player_sel], pts_win = u.gp_points[r.field[0].character];
                    u.screen = SMK_UI_RESULT; smk_ui_step(&u, &rom, &go);                         /* -> points */
                    bool pt = u.screen == SMK_UI_POINTS;
                    smk_ui_step(&u, &rom, &go);                                                   /* skips the animation */
                    bool pt2 = u.screen == SMK_UI_POINTS;
                    smk_ui_step(&u, &rom, &go);                                                   /* -> standings */
                    bool st = u.screen == SMK_UI_STANDINGS;
                    smk_ui_step(&u, &rom, &go);                                                   /* skips the animation */
                    bool next = smk_ui_step(&u, &rom, &go);                                       /* -> race 1 */
                    int t1 = u.track;
                    /* the grid for race 1 is the championship order, the
                     * winner on pole and the player (2nd) beside him -
                     * whatever block each of them drives (NOTES 274) */
                    int g[8], sl[8];
                    smk_grid_order(&rom, u.player_sel, 0, false, g);
                    smk_ui_grid_slots(&u, g, sl);
                    int win_slot = -1, me_slot = sl[0], last_slot = -1;
                    for (int i = 0; i < 8; i++) { if (g[i] == r.field[0].character) win_slot = sl[i];
                                                  if (g[i] == r.field[7].character) last_slot = sl[i]; }
                    bool gridok = win_slot == 0 && me_slot == 1 && last_slot == 7;
                    /* and it is the LAST RACE'S order, not the points
                     * (NOTES 275): a second race the points leader
                     * finishes fourth in puts its winner on pole */
                    {
                        smk_ui v = u;
                        smk_ui_result r2 = r;
                        /* race two: last race's 4th wins (1+9 = 10), the
                         * leader is 2nd (9+6 = 15, still the leader), 3rd
                         * stays, the player (6) is 4th (7) */
                        int lead = r.field[0].character;
                        r2.field[0].character = r.field[3].character;
                        r2.field[1].character = lead;
                        r2.field[3].character = r.field[1].character;
                        r2.field[0].player = 0; r2.field[1].player = 0; r2.field[3].player = 1;
                        r2.position = 4;
                        v.gp_race = 1; smk_ui_gp_award(&v, &r2);
                        int order[8]; smk_ui_gp_order(&v, order);
                        smk_ui_grid_slots(&v, g, sl);
                        int lead_slot = -1, win2_slot = -1;
                        for (int i = 0; i < 8; i++) { if (g[i] == lead) lead_slot = sl[i];
                                                      if (g[i] == r2.field[0].character) win2_slot = sl[i]; }
                        /* the leader still leads the championship (15 to 10) but starts second */
                        gridok = gridok && order[0] == lead && lead_slot == 1 && win2_slot == 0;
                    }
                    /* starting coins by slot: 2 from pole, 5 from the back (NOTES 275) */
                    gridok = gridok && smk_start_coins(&rom, 0) == 2 && smk_start_coins(&rom, 7) == 5
                                    && smk_start_coins(&rom, 3) == 3;
                    /* ranked out - the player sixth - is no retry any more
                     * (NOTES 282): the top four are paid, the player is
                     * not, and the cup goes on to the third course */
                    {
                        smk_ui_result r3 = r;
                        int mine = r.field[1].character;
                        r3.field[1].character = r.field[5].character; r3.field[1].player = 0;
                        r3.field[5].character = mine;                 r3.field[5].player = 1;
                        r3.position = 6;
                        smk_ui_gp_award(&u, &r3);
                    }
                    u.screen = SMK_UI_RESULT; smk_ui_step(&u, &rom, &go);
                    smk_ui_step(&u, &rom, &go); smk_ui_step(&u, &rom, &go); smk_ui_step(&u, &rom, &go);
                    bool goes_on = smk_ui_step(&u, &rom, &go) && u.gp_race == 2
                                && u.track == smk_cup_track(&rom, 0, 2) && u.track != t1;
                    bool kept = u.gp_points[u.player_sel] == pts_me                              /* nothing for sixth */
                             && u.gp_points[r.field[0].character] == pts_win + 9;              /* the winner is still paid */
                    snprintf(det, sizeof det, "table %d %d %d %d; me %d pts winner %d; tracks %d -> %d; points %d/%d standings %d goes on %d kept %d; slots win %d me %d last %d",
                             u.gp_pts_table[0], u.gp_pts_table[1], u.gp_pts_table[2], u.gp_pts_table[3], pts_me, pts_win, t0, t1, pt, pt2, st, goes_on, kept, win_slot, me_slot, last_slot);
                    check("a cup: 9/6/3/1 from the ROM, points then standings, the next grid from the last race's order, coins by slot, no retry when ranked out",
                          started && u.gp_pts_table[0] == 9 && u.gp_pts_table[3] == 1 && pts_me == 6 && pts_win == 9
                          && pt && pt2 && st && next && t1 != t0 && goes_on && kept && gridok, det);
                    /* THE WATER (NOTES 283, from the user's two underwater
                     * recordings): the fall-in loads $CA with $102 and holds
                     * the first eighteen frames; the kart steers and wades
                     * at up to 124; onto another class it hops out with 12
                     * of speed; if $CA runs out it sinks - z still for 33
                     * frames, then up $40 a frame to $700 and into Lakitu's
                     * carry, and down $80 a frame at the end. */
                    {
                        static smk_track wt;
                        memset(&wt, 0, sizeof wt);
                        wt.surface[0] = 0x22; wt.surface[1] = 0x40;
                        for (int y = 0; y < 62; y++)                /* road north of y 496 */
                            for (int x = 0; x < 128; x++) wt.map[y * 128 + x] = 1;
                        smk_player pw; smk_kart kw;
                        smk_player_setup(&rom, 0, 0, &pw);
                        smk_player_reset(&pw, 0);
                        memset(&kw, 0, sizeof kw);
                        kw.x = 512 << SMK_POS_SHIFT; kw.y = 512 << SMK_POS_SHIFT;
                        kw.speed = 100;
                        smk_player_step(&pw, &kw, &wt, 0x8000, 0);
                        int ca0 = pw.ca, hz0 = pw.hazard;
                        /* B and left for forty frames: it turns and it moves */
                        for (int i = 0; i < 40; i++) smk_player_step(&pw, &kw, &wt, 0x8200, 0);
                        uint16_t hdg40 = pw.heading; int spd40 = kw.speed, hz40 = pw.hazard;
                        /* straight on north with B: onto the road, and out */
                        smk_player_reset(&pw, 0); pw.hazard = 8; pw.drive = 8; pw.jump_state = 8; pw.ca = 0x100;
                        kw.speed = 0; kw.x = 512 << SMK_POS_SHIFT; kw.y = 500 << SMK_POS_SHIFT;
                        int out_at = -1, spd_out = 0;
                        for (int i = 0; i < 400 && out_at < 0; i++) {
                            smk_player_step(&pw, &kw, &wt, 0x8000, 0);
                            if (pw.hazard == 0) { out_at = i; spd_out = kw.speed; }
                        }
                        bool hopped = out_at >= 0 && kw.airborne && pw.drive == 2;
                        /* the sink: no throttle, the counter runs out */
                        smk_player_reset(&pw, 0); pw.hazard = 8; pw.drive = 8; pw.jump_state = 8; pw.ca = 2;
                        kw.speed = 0; kw.airborne = false; kw.x = 512 << SMK_POS_SHIFT; kw.y = 600 << SMK_POS_SHIFT; kw.z = 0;
                        pw.resc_x = 512; pw.resc_y = 600; pw.resc_h = 0;
                        int steps6 = 0, still = 0, carry_at = -1, down_at = -1, free_at = -1;
                        int32_t zcarry = -1;
                        for (int i = 0; i < 400; i++) {
                            smk_player_step(&pw, &kw, &wt, 0, 0);
                            if (pw.hazard == 6) { steps6++; if (kw.z == 0) still++; }
                            if (pw.hazard == 0x0C && carry_at < 0) { carry_at = i; zcarry = kw.z; }
                            if (pw.hazard == 0x0E && down_at < 0) down_at = i;
                            if (pw.hazard == 0 && carry_at >= 0 && free_at < 0) { free_at = i; break; }
                        }
                        snprintf(det, sizeof det, "fall-in: CA $%03X hazard %d; 40 frames B+left: heading %04X speed %d hazard %d; out after %d frames at speed %d hopped %d; sink: %d frames in 6 (%d still), carry at z $%04X, lowering %d frames",
                                 ca0, hz0, hdg40, spd40, hz40, out_at, spd_out, hopped, steps6, still, (unsigned)(zcarry >> 8), free_at - down_at);
                        check("the water: $102 to sink, steering and a wade, a hop out at 12, and Lakitu's lift of $700 over 61 frames",
                              ca0 == 0x102 && hz0 == 8 && hdg40 != 0 && spd40 >= 30 && hz40 == 8
                              && out_at > 0 && out_at < 200 && spd_out == 12 && hopped
                              && steps6 == 61 && still == 34 && zcarry == ((int32_t)0x700 << 8)
                              && free_at - down_at == 14, det);
                    }
                    /* THE REV AND THE HITS (NOTES 285): a wall bounce or a
                     * kart contact halves the rev ($80A10F, floor $100), a
                     * fall or a fall-in zeroes it ($80B768) */
                    {
                        static smk_track rd; memset(&rd, 0, sizeof rd); rd.surface[0] = 0x40;
                        smk_player pr; smk_kart kr;
                        smk_player_setup(&rom, 0, 1, &pr); smk_player_reset(&pr, 0);
                        memset(&kr, 0, sizeof kr); kr.x = 512 << SMK_POS_SHIFT; kr.y = 512 << SMK_POS_SHIFT;
                        kr.speed = 838; pr.rev = 0x2540; kr.bounce_hit = 1;
                        smk_player_step(&pr, &kr, &rd, 0x8000, 0);
                        int after_wall = (uint16_t)pr.rev;
                        pr.rev = 0x0180; kr.bounce_hit = 1;
                        smk_player_step(&pr, &kr, &rd, 0x8000, 0);
                        int floored = (uint16_t)pr.rev;
                        /* two karts touching mark both for the cost */
                        smk_kart ka, kb; memset(&ka, 0, sizeof ka); memset(&kb, 0, sizeof kb);
                        ka.x = 512 << SMK_POS_SHIFT; ka.y = 512 << SMK_POS_SHIFT; ka.speed = 600; ka.vy = -600;
                        kb.x = 512 << SMK_POS_SHIFT; kb.y = 510 << SMK_POS_SHIFT; kb.speed = 300; kb.vy = -300;
                        smk_kart *pair[2] = { &ka, &kb }; uint8_t wts[2] = { 2, 2 };
                        smk_karts_collide(pair, wts, 2);
                        bool marked = ka.bounce_hit && kb.bounce_hit;
                        /* and the water zeroes it */
                        static smk_track wt2; memset(&wt2, 0, sizeof wt2); wt2.surface[0] = 0x22;
                        smk_player_reset(&pr, 0); pr.rev = 0x2000; kr.speed = 100; kr.bounce_hit = 0; kr.airborne = false;
                        smk_player_step(&pr, &kr, &wt2, 0x8000, 0);
                        snprintf(det, sizeof det, "wall: $2540 -> $%04X; $0180 -> $%04X; contact marks both %d; fall-in rev $%04X hazard %d",
                                 after_wall, floored, marked, (uint16_t)pr.rev, pr.hazard);
                        check("a hit halves the rev ($2540 -> $12A0, floor $100), contact marks both karts, the water zeroes it",
                              after_wall == 0x12A0 && floored == 0x0100 && marked && pr.rev == 0 && pr.hazard == 8, det);
                    }
                    /* OFF-ROAD REV (NOTES 287): on a class $14 and up the rev
                     * falls at the row's off-road rate with B held, towards
                     * $1000 - $380 every eight frames at 100cc */
                    {
                        static smk_track gr; memset(&gr, 0, sizeof gr); gr.surface[0] = 0x5A;
                        smk_player pg; smk_kart kg;
                        smk_player_setup(&rom, 0, 1, &pg); smk_player_reset(&pg, 0);
                        memset(&kg, 0, sizeof kg); kg.x = 512 << SMK_POS_SHIFT; kg.y = 512 << SMK_POS_SHIFT;
                        kg.speed = 500; pg.rev = 0x2000;
                        for (int i = 0; i < 8; i++) { smk_player_step(&pg, &kg, &gr, 0x8000, 0); smk_player_rev_race(&pg); }
                        int after8 = (uint16_t)pg.rev;
                        for (int i = 0; i < 120; i++) { smk_player_step(&pg, &kg, &gr, 0x8000, 0); smk_player_rev_race(&pg); }
                        int after128 = (uint16_t)pg.rev;
                        snprintf(det, sizeof det, "grass, B held: $2000 -> $%04X after 8 frames, $%04X after 128", after8, after128);
                        check("off-road the rev falls $380 a step towards $1000 (Donut Plains' dirt does not sing like asphalt)",
                              after8 == 0x2000 - 0x380 && after128 >= 0x0C00 && after128 < 0x1400, det);
                    }
                    /* the finishing list's art (NOTES 282): $C3:0000 holds
                     * three rows of eight faces and the digits; Yoshi is
                     * our fifth driver and the sheet's last column */
                    {
                        smk_faces fa;
                        bool ok = smk_faces_load(&rom, &fa);
                        int lit = 0, dlit = 0;
                        for (int i = 0; i < SMK_FACE_PX * SMK_FACE_PX; i++)
                            if (fa.face[SMK_FACE_LIST][smk_face_of(5)][i]) lit++;
                        for (int i = 0; i < 8 * 16; i++) if (fa.digit[7][i] == 15) dlit++;
                        snprintf(det, sizeof det, "loaded %d; Yoshi -> column %d, %d lit px of 256; digit 8 has %d white px", ok, smk_face_of(5), lit, dlit);
                        check("the finishing list's faces and digits decode from $C3:0000",
                              ok && smk_face_of(5) == 7 && smk_face_of(7) == 6 && lit > 150 && dlit > 20 && dlit < 80, det);
                    }
                    (void)none;
                }
                smk_racer rb; memset(&rb, 0, sizeof rb); rb.k.speed = 835;
                smk_racer_hit(&rb, 4, 1);
                snprintf(det, sizeof det, "hit_t %d tumble $%04X", rb.hit_t, rb.tumble);
                check("an AI kart bumped with no coins spins $480 a frame for ~57 frames",
                      rb.hit_kind == 4 && rb.tumble == 0x480 && rb.hit_t >= 55 && rb.hit_t <= 59, det);
            }
        }

        /* and the row really does move the target speed */
        static smk_physics ph2;
        smk_physics_load(&rom, 0, &ph2);
        int t_ease  = ph2.w[SMK_PHYS_TARGET + 0 + SMK_AI_ROW_EASE];
        int t_chase = ph2.w[SMK_PHYS_TARGET + 0 + SMK_AI_ROW_CHASE];
        int t_hold  = ph2.w[SMK_PHYS_TARGET + 0 + SMK_AI_ROW_HOLD];
        snprintf(det, sizeof det, "chase %d > ease %d > hold %d",
                 t_chase, t_ease, t_hold);
        check("chasing is the fastest row and holding station the slowest",
              t_chase > t_ease && t_ease > t_hold, det);
        #undef PUT
    }

    printf("\nkart against kart\n");
    {
        /* Every number here was read out of the running game with
         * tools/labs/bump4.py: two karts placed at a chosen separation,
         * one frame stepped, the velocities read back (NOTES 166). */
        static smk_kart ka, kb;
        #define SET(K, PX, PY, VX, VY) do {                    \
            memset(&(K), 0, sizeof (K));                       \
            (K).x = (int32_t)(PX) << SMK_POS_SHIFT;            \
            (K).y = (int32_t)(PY) << SMK_POS_SHIFT;            \
            (K).vx = (VX); (K).vy = (VY);                      \
            (K).speed = (int16_t)(((VX)*(VX)+(VY)*(VY)) > 0 ?  \
                 (int)(sqrt((double)((VX)*(VX)+(VY)*(VY)))+0.5) : 0); \
        } while (0)

        /* the box: the ROM's `d + 4 < 8` window, [-4, +3] */
        int box = 0, want_box = 0;
        for (int d = -6; d <= 6; d++) {
            SET(ka, 500, 500, 0, -600);
            SET(kb, 500 + d, 500, 0, -400);
            bool hit = smk_kart_bump(&ka, 0x1A, &kb, 0x1A);
            bool in = (d >= -4 && d <= 3);
            want_box++;
            if (hit == in) box++;
        }
        for (int d = -6; d <= 6; d++) {
            SET(ka, 500, 500, 0, -600);
            SET(kb, 500, 500 + d, 0, -400);
            bool hit = smk_kart_bump(&ka, 0x1A, &kb, 0x1A);
            bool in = (d >= -4 && d <= 3);
            want_box++;
            if (hit == in) box++;
        }
        snprintf(det, sizeof det, "%d/%d separations", box, want_box);
        check("the contact box is the ROM's [-4,+3] on both axes",
              box == want_box, det);

        /* equal weights: the vectors are EXCHANGED, exactly */
        SET(ka, 500, 500, 0, -600);
        SET(kb, 500, 500, 0, -400);
        bool hit1 = smk_kart_bump(&ka, 0x1A, &kb, 0x1A);
        snprintf(det, sizeof det, "a (%d,%d) b (%d,%d)", ka.vx, ka.vy, kb.vx, kb.vy);
        check("equal weights exchange the velocity vectors, and only that",
              hit1 && ka.vx == 0 && ka.vy == -400
                   && kb.vx == 0 && kb.vy == -600, det);

        /* and the pair is shut for eight frames afterwards */
        bool again = smk_kart_bump(&ka, 0x1A, &kb, 0x1A);
        snprintf(det, sizeof det, "cool %d, second hit %s", ka.bump_cool,
                 again ? "FIRED" : "blocked");
        check("the pair is closed for the ROM's eight frames",
              !again && ka.bump_cool == SMK_BUMP_COOL, det);

        /* a second contact inside the pair's own cooldown is nearly
         * free ($819C93): at speed, nothing at all happens */
        SET(ka, 500, 500, 0, -600);
        SET(kb, 500, 500, 0, -400);
        smk_kart_bump(&ka, 0x1A, &kb, 0x1A);       /* the hard one */
        ka.bump_cool = 1; kb.bump_cool = 1;        /* the cooldown's tail */
        int16_t ax = ka.vx, ay = ka.vy, bx = kb.vx, by = kb.vy;
        bool soft = smk_kart_bump(&ka, 0x1A, &kb, 0x1A);
        snprintf(det, sizeof det, "%s, a (%d,%d) b (%d,%d)",
                 soft ? "ran" : "skipped", ka.vx, ka.vy, kb.vx, kb.vy);
        check("a re-contact at speed costs nothing at all",
              soft && ka.vx == ax && ka.vy == ay
                   && kb.vx == bx && kb.vy == by, det);

        /* ... but two karts that have STOPPED get nudged apart, which is
         * what keeps a heap from setting */
        SET(ka, 500, 500, 0, 0);
        SET(kb, 500, 500, 0, 0);
        ka.angle = 0; ka.bump_cool = 1;
        smk_kart_bump(&ka, 0x1A, &kb, 0x1A);
        snprintf(det, sizeof det, "a (%d,%d) speed %d", ka.vx, ka.vy, ka.speed);
        /* the vector is the ROM's own DSP-1 arithmetic, which is a unit
         * shy of a clean cos - $0180 along heading 0 comes out -383 */
        check("two stopped karts are nudged apart at $0180",
              ka.speed == 0x0180 && ka.vx == 0
                  && ka.vy <= -0x017F && ka.vy >= -0x0180, det);

        /* heavier AND faster: it keeps its line at speed - half the
         * closing speed, the light one is flung at that speed + $20 */
        SET(ka, 500, 500, 0, -600);
        SET(kb, 500, 500, 0, -400);
        smk_kart_bump(&ka, 0x1B, &kb, 0x19);
        int flung = kb.speed;
        snprintf(det, sizeof det, "heavy %d (want 500), light %d (want 632)",
                 ka.speed, flung);
        check("the heavier kart keeps its line, the lighter is flung off",
              ka.speed == 500 && ka.vx == 0 && flung >= 628 && flung <= 636, det);

        /* rammed from behind by a lighter kart two classes down: the
         * heavy one is untouched, the rammer is cut to a quarter */
        SET(ka, 500, 500, 0, -400);
        SET(kb, 500, 500, 0, -600);
        smk_kart_bump(&ka, 0x1B, &kb, 0x19);
        snprintf(det, sizeof det, "heavy %d (want 400), rammer %d (want ~100)",
                 ka.speed, kb.speed);
        check("ramming a heavier kart from behind costs three quarters",
              ka.speed == 400 && ka.vy == -400
                  && kb.speed >= 96 && kb.speed <= 104, det);
        #undef SET
    }

    printf("\nthe draw list\n");
    {
        /* Everything on the plane shares ONE depth-sorted list.  If it
         * cannot hold a course's entities AND the eight karts, the karts
         * are the ones that fall off the end - silently, and only on the
         * busy courses (NOTES 165). */
        int worst = 0, worst_t = -1, bad = 0;
        for (int tr = 0; tr < SMK_TRACK_COUNT; tr++) {
            static smk_course cd;
            if (!smk_course_load(&rom, tr, &cd)) continue;
            if (cd.nent > worst) { worst = cd.nent; worst_t = tr; }
            if (cd.nent + SMK_CHARACTERS > SMK_DRAW_LIST) bad++;
            if (cd.nent > SMK_COURSE_ENTS) bad++;
        }
        snprintf(det, sizeof det, "busiest is track %d with %d entities + %d karts, list holds %d",
                 worst_t, worst, SMK_CHARACTERS, SMK_DRAW_LIST);
        check("the draw list fits every entity AND the whole field", !bad, det);
    }

    printf("\nthe starting order\n");
    {
        /* racers[] is indexed by the game's kart BLOCK; SMK_GRID_SLOT
         * turns that into a grid row, and the measurement says block 0 -
         * the player - is at the BACK (NOTES 161's capture: $1000 took
         * (952,756) on track 7, which is y0 + 24*7). */
        static smk_course cg;
        int ok8 = smk_course_load(&rom, 7, &cg);
        float bx, by, px2, py2; uint16_t bh, ph2;
        smk_course_start(&cg, SMK_GRID_SLOT(0), &px2, &py2, &ph2);
        smk_course_start(&cg, SMK_GRID_SLOT(SMK_CHARACTERS - 1), &bx, &by, &bh);
        snprintf(det, sizeof det, "block 0 at (%.0f,%.0f), block 7 at (%.0f,%.0f)",
                 px2, py2, bx, by);
        check("the player's block starts at the back, block 7 on the pole",
              ok8 && px2 == 952.0f && py2 == 756.0f
                  && bx == 920.0f && by == 588.0f, det);

        /* every block gets its own row, and the eight fill the grid */
        int seen = 0, dup = 0;
        for (int i = 0; i < SMK_CHARACTERS; i++) {
            int sl = SMK_GRID_SLOT(i);
            if (sl < 0 || sl >= SMK_CHARACTERS) { dup++; continue; }
            if (seen & (1 << sl)) dup++;
            seen |= 1 << sl;
        }
        snprintf(det, sizeof det, "mask $%02X", seen);
        check("the eight blocks fill the eight rows exactly once",
              seen == 0xFF && !dup, det);
    }

    printf("\nthe start: Lakitu and his light\n");
    {
        /* The measured script (NOTES 162), from the game's own OAM:
         * tools/labs/lakitu.py + lakitu_full.py.  Every number here is a
         * frame of $0146 counted from the arm. */
        smk_start a, b2, c2, d2, e2;
        smk_start_frame(0, &a);
        smk_start_frame(1, &b2);
        smk_start_frame(178, &c2);
        smk_start_frame(179, &d2);
        smk_start_frame(SMK_COUNT_FRAMES, &e2);
        snprintf(det, sizeof det, "f0 %s, f1 %s at y=%d",
                 a.on ? "on" : "off", b2.on ? "on" : "off", b2.y);
        check("he drops in on frame 1, at y = -48",
              !a.on && b2.on && b2.y == -48 && b2.x == SMK_START_X, det);

        int lamps_ok = (c2.lamp[0] == SMK_LAMP_RED_OFF && c2.lit == 0)
                    && (d2.lamp[0] == SMK_LAMP_RED_ON && d2.lit == 1);
        smk_start_frame(244, &c2); lamps_ok &= c2.lamp[1] == SMK_LAMP_RED_ON
                                            && c2.lit == 2;
        smk_start_frame(243, &c2); lamps_ok &= c2.lamp[1] == SMK_LAMP_RED_OFF;
        smk_start_frame(309, &c2); lamps_ok &= c2.lamp[2] == SMK_LAMP_GREEN_ON
                                            && c2.lit == 3 && c2.cheer;
        smk_start_frame(308, &c2); lamps_ok &= c2.lamp[2] == SMK_LAMP_GREEN_OFF
                                            && !c2.cheer;
        check("red 179, red 244, green 309 - and the green brings the pose",
              lamps_ok, "each checked against the frame before it");

        /* the drop, sampled off the same capture */
        static const struct { int t, y; } WANT[] = {
            {  1, -48}, { 40, -19}, { 66,   0}, { 88,   7},
            {113,   5}, {336,   5}, {380,   3}, {439, -40},
        };
        int hits = 0;
        for (unsigned i = 0; i < sizeof WANT / sizeof WANT[0]; i++) {
            smk_start_frame(WANT[i].t, &c2);
            if (c2.y == WANT[i].y) hits++;
        }
        snprintf(det, sizeof det, "%d/%d sampled rows",
                 hits, (int)(sizeof WANT / sizeof WANT[0]));
        check("the drop, the overshoot and the climb out match the capture",
              hits == (int)(sizeof WANT / sizeof WANT[0]), det);

        /* still down in front when the field is released, and clear of
         * the top of the screen by the time the fixture parks him */
        smk_start_frame(SMK_START_LAST, &c2);
        snprintf(det, sizeof det, "y=%d at the release, y=%d parked", e2.y, c2.y);
        check("green and down at the release, off the top when parked",
              e2.on && e2.y == 5 && e2.lamp[2] == SMK_LAMP_GREEN_ON
              && c2.y + SMK_START_LAMP_DY + 24 <= 0, det);

        /* the lamps' art: the block smk_hud_load used to skip */
        static smk_hud hh;
        bool hok = smk_hud_load(&rom, &hh);
        const uint8_t *goff = smk_hud_tile_px(&hh, SMK_LAMP_GREEN_OFF);
        const uint8_t *gon  = smk_hud_tile_px(&hh, SMK_LAMP_GREEN_ON);
        int ink = 0, diff = 0;
        for (int i = 0; goff && gon && i < 64; i++) {
            ink += goff[i] != 0;
            diff += goff[i] != gon[i];
        }
        snprintf(det, sizeof det, "%d ink, %d px differ", ink, diff);
        check("the four lamp tiles load, and lit is not the same art as dark",
              hok && goff && gon && ink > 20 && diff > 10, det);
    }

    printf("\nsprites\n");
    static smk_sprites spr;
    int sprok = smk_sprites_load(&rom, 0, &spr);
    snprintf(det, sizeof det, "%d frames", spr.frames);
    check("kart sprite frames load", sprok && spr.frames == SMK_SPR_FRAMES, det);
    int filled = 0;
    for (int f = 0; f < spr.frames; f++) {
        int nz = 0;
        for (int i = 0; i < SMK_SPR_PX * SMK_SPR_PX; i++)
            if (spr.px[f][i]) nz++;
        /* a real 32x32 kart covers a good part of its box but not all of it */
        if (nz > 200 && nz < 900) filled++;
    }
    snprintf(det, sizeof det, "%d/%d frames look like a kart", filled, spr.frames);
    check("frames are sprites, not noise or blanks", filled >= spr.frames - 4, det);

    printf("\ncodec\n");
    /* Every command exercised through a stream we build by hand. */
    static const uint8_t stream[] = {
        0x02, 'A', 'B', 'C',              /* literal 3            */
        0x24, 'z',                        /* byte fill 5          */
        0x43, 'x', 'y',                   /* word fill 4          */
        0x62, 0x10,                       /* inc fill 3           */
        0x81, 0x00, 0x00,                 /* copy abs from 0, 2   */
        0xC1, 0x02,                       /* copy rel dist 2, 2   */
        0xFF
    };
    uint8_t out[64];
    long n = smk_decompress(stream, sizeof stream, 0, out, sizeof out, NULL);
    static const uint8_t want[] = { 'A','B','C', 'z','z','z','z','z',
                                    'x','y','x','y', 0x10,0x11,0x12,
                                    'A','B', 'A','B' };
    check("all commands decode correctly",
          n == (long)sizeof want && memcmp(out, want, sizeof want) == 0, NULL);

    uint8_t bad[] = { 0xC1, 0x05, 0xFF };      /* back-reference before start */
    check("a malformed stream is rejected",
          smk_decompress(bad, sizeof bad, 0, out, sizeof out, NULL) < 0, NULL);

    {
        /* Object stamps: track 7's map must carry the item-box tiles and
         * the pipes' dirt scatter with the counts captured from the live
         * game (WRAM tilemap, docs/NOTES.md 074). */
        smk_track trk;
        char terr[128];
        if (smk_track_load(&rom, 7, -1, &trk, terr, sizeof terr)) {
            smk_track_place_objects(&rom, &trk);
            int n196 = 0, n199 = 0, n254 = 0;
            for (int i = 0; i < SMK_MAP_BYTES; i++) {
                if (trk.map[i] == 196) n196++;
                if (trk.map[i] == 199) n199++;
                if (trk.map[i] == 254) n254++;
            }
            check("track 7 object stamps match the live capture",
                  n196 == 12 && n199 == 12 && n254 == 35, NULL);
            const uint8_t *ot = trk.tiles + 196 * SMK_TILE_BYTES;
            int opaque = 0;
            for (int i = 0; i < SMK_TILE_BYTES; i++) if (ot[i]) opaque++;
            check("object tile 196 decompressed to pixels", opaque > 16, NULL);
        } else {
            check("track 7 loads for the stamp check", 0, terr);
        }
        {
            /* Sprite obstacles: the decoded $85:C800 list for track 7
             * must match the entities captured live at race start
             * ((268,92) and (164,132) were the spawned pair). */
            smk_course cc;
            if (smk_course_load(&rom, 7, &cc)) {
                check("track 7 entity list matches the live spawn",
                      cc.nent == 8 && cc.ent[0].x == 268 && cc.ent[0].y == 92
                      && cc.ent[1].x == 164 && cc.ent[1].y == 132, NULL);
            } else {
                check("track 7 course loads for the entity check", 0, NULL);
            }
        }
    }

    test_player_replay(&rom);
    {
        /* The overtake voices (NOTES 235): the port reads $84:D99B and
         * $84:D9CA out of the ROM, and every one of these sixteen was
         * reproduced by forcing the rank change in the oracle, so a
         * silent drift in the table or the mapping is a failure here. */
        static const struct { const char *who; int gain, lose; } V[SMK_CHARACTERS] = {
            { "Mario",  0x4D, 0x00 }, { "Luigi",  0x4D, 0x00 },
            { "Bowser", 0x51, 0x51 }, { "Peach",  0x4D, 0x00 },
            { "DK Jr",  0x5C, 0x5C }, { "Yoshi",  0x4D, 0x00 },
            { "Koopa",  0x4D, 0x00 }, { "Toad",   0x50, 0x50 },
        };
        int ok = 1;
        char why[160] = "";
        for (int c = 0; c < SMK_CHARACTERS; c++) {
            int g = smk_sfx_pass_voice(&rom, c, true);
            int l = smk_sfx_pass_voice(&rom, c, false);
            if (g != V[c].gain || l != V[c].lose) {
                ok = 0;
                snprintf(why, sizeof why, "%s: got $%02X/$%02X want $%02X/$%02X",
                         V[c].who, g, l, V[c].gain, V[c].lose);
                break;
            }
        }
        check("overtake voices match the ROM's own two tables", ok, why);
        check("an out-of-range driver asks for nothing",
              smk_sfx_pass_voice(&rom, -1, true) == 0
              && smk_sfx_pass_voice(&rom, SMK_CHARACTERS, false) == 0, NULL);
    }
    {
        /* The start rev, the turbo band and the wheelspin (NOTES 143/145),
         * over the MEASURED 336-frame countdown.  The last two rows are
         * the user's own recording: their normal start read 11008 at the
         * line and their turbo one 11776 two frames out - both reproduced
         * to the unit by a flat 96-a-frame build. */
        struct { int press, rev, spin, window, drive; } W[] = {
            { 999,   256, 0, 0, 0x00 },   /* never touched: the idle floor */
            {   0, 18944, 1, 0, 0x00 },   /* held throughout: over-revved  */
            { 212, 12352, 1, 0, 0x00 },   /* one tick too early            */
            { 214, 12160, 0, 1, 0x10 },   /* the window opens              */
            { 216, 11968, 0, 1, 0x10 },   /* the user's TURBO run          */
            { 218, 11776, 0, 0, 0x00 },   /* just missed - their reading    */
            { 226, 11008, 0, 0, 0x00 },   /* the user's normal run          */
        };
        int bad = 0;
        char d[160];
        d[0] = 0;
        for (size_t i = 0; i < sizeof W / sizeof W[0]; i++) {
            static smk_player pr;
            smk_player_setup(&rom, 0, 1, &pr); smk_player_reset(&pr, 0);
            for (int f = 1; f <= SMK_COUNT_FRAMES; f++)
                smk_player_rev(&pr, f >= W[i].press, (unsigned)f);
            int spin = pr.rev_over, win = pr.rev_window, rev = pr.rev;
            smk_player_launch(&pr);
            if (rev != W[i].rev || spin != W[i].spin || win != W[i].window
                || pr.drive != W[i].drive) {
                bad++;
                snprintf(d, sizeof d, "press f%d: rev %d (want %d) over %d win %d drive $%02X",
                         W[i].press, rev, W[i].rev, spin, win, pr.drive);
            }
        }
        if (!d[0]) snprintf(d, sizeof d, "window f214..f217 of 336; the user's 11008, 11776 and 11968 reproduce");
        check("the start rev over-revs, hits the turbo band, or misses", !bad, d);

        /* The penalty itself ($8095E0 -> $80B0EE, NOTES 163): the rev is
         * snapped to $3000 at the line, then bleeds $70 a frame with $E2
         * bit 5 up - the very bit the ground effect reads as smoke - and
         * the kart is not stopped dead, it creeps. */
        {
            static smk_player pr; static smk_kart kk;
            smk_player_setup(&rom, 0, 1, &pr); smk_player_reset(&pr, 0);
            for (int f = 1; f <= SMK_COUNT_FRAMES; f++)
                smk_player_rev(&pr, true, (unsigned)f);
            smk_player_launch(&pr);
            int snapped = pr.rev;
            memset(&kk, 0, sizeof kk);
            static smk_track tt2; static smk_course cc2;
            smk_track_load(&rom, 7, -1, &tt2, err, sizeof err);
            smk_course_load(&rom, 7, &cc2);
            {   /* on the road, where the start actually happens */
                float sx2, sy2; uint16_t sh2;
                smk_course_start_solo(&cc2, &sx2, &sy2, &sh2);
                kk.x = (int32_t)(sx2 * SMK_POS_ONE);
                kk.y = (int32_t)(sy2 * SMK_POS_ONE);
                kk.angle = sh2;
                smk_player_reset(&pr, sh2);
                pr.rev = 0x3000; pr.rev_spin = 1; pr.flags |= 0x0001;
            }
            int smoke = 0, frames = 0, moved = 0;
            for (int f = 0; f < 200 && pr.rev_spin; f++) {
                smk_player_step(&pr, &kk, &tt2, 0x8000, f ? 0 : 0x8000);
                if (pr.flags & 0x0020) smoke++;
                frames++;
            }
            moved = kk.speed;
            snprintf(det, sizeof det,
                     "snap %d, %d frames spinning, %d smoking, crept to %d",
                     snapped, frames, smoke, moved);
            check("over-revved: snapped to $3000, smoking, creeping, then let go",
                  snapped == 0x3000 && frames >= 30 && frames <= 42
                  && smoke == frames - 1 && moved > 0 && moved < 0x80
                  && !pr.rev_spin,
                  det);
        }
    }
    {
        /* $80B5CD sets $1F = 1 and the game leaves it there for the whole
         * 60-frame countdown - the physics stops and waits (NOTES 135a).
         * Ours used to sink the kart under the plane instead (NOTES 142). */
        static smk_track tf; static smk_player pf; smk_kart kf = {0};
        char e[256];
        if (smk_track_load(&rom, 16, -1, &tf, e, sizeof e)) {
            smk_player_setup(&rom, 0, 1, &pf); smk_player_reset(&pf, 0);
            pf.hazard = 6; pf.resc_t = 0;
            pf.resc_x = 500; pf.resc_y = 500;
            int moved = 0;
            for (int f = 0; f < 55; f++) {
                smk_player_step(&pf, &kf, &tf, 0, 0);
                if (kf.z != ((int32_t)1 << 8)) moved = 1;
            }
            char d[96];
            snprintf(d, sizeof d, "z = %d after 55 frames, want %d",
                     (int)kf.z, (int)((int32_t)1 << 8));
            check("a falling kart holds $1F = 1 through the countdown", !moved, d);
        }
    }
    {
        /* All EIGHT characters, and they must differ in the ROM's own
         * order (S13).  Tops at 100cc from $81:8000, and the four classic
         * pairs fall out of the acceleration curves at $81:8010:
         *   Bowser/DK   944  slowest to accelerate
         *   Mario/Luigi 912
         *   Peach/Yoshi 880  quickest off the line
         *   Koopa/Toad  864  most agile steering
         * A regression that made every character drive the same - or that
         * silently used Mario's tables for all of them - shows up here. */
        static const int TOP[8] = { 912, 912, 944, 880, 944, 864, 864, 880 };
        static smk_track tc;
        char e[256];
        int bad = 0, dist[8];
        char d[160];
        d[0] = 0;
        if (smk_track_load(&rom, 7, -1, &tc, e, sizeof e)) {
            for (int c = 0; c < 8; c++) {
                static smk_player pc; smk_kart kc = {0};
                if (!smk_player_setup(&rom, c, 1, &pc)) { bad++; continue; }
                if (pc.base_top != TOP[c]) {
                    bad++;
                    snprintf(d, sizeof d, "character %d top %d, want %d",
                             c, pc.base_top, TOP[c]);
                }
                smk_player_reset(&pc, 0);
                kc.x = (int32_t)512 << 16; kc.y = (int32_t)900 << 16;
                pc.heading = pc.vel_angle = pc.pose = 0x4000;
                int32_t x0 = kc.x;
                for (int f = 0; f < 180; f++) smk_player_step(&pc, &kc, &tc, 0x8000, 0);
                dist[c] = (int)((kc.x - x0) >> 16);
            }
            /* the pairs must match each other and the groups must order */
            if (!(dist[0] == dist[1] && dist[2] == dist[4]
                  && dist[3] == dist[7] && dist[5] == dist[6])) bad++;
            if (!(dist[3] > dist[5] && dist[5] > dist[0] && dist[0] > dist[2])) bad++;
            if (!d[0])
                snprintf(d, sizeof d, "180-frame runs: Peach %d > Toad %d > Mario %d > Bowser %d",
                         dist[3], dist[5], dist[0], dist[2]);
        }
        check("all eight characters drive differently, in the ROM's order", !bad, d);
    }
    {
        /* $80FA5A: a kart clears a wall only above height 4.  A hop
         * launches at $E0 and peaks below that, so the Ghost Valley rails
         * cannot be jumped - which they could be until NOTES 137. */
        int peak = 0;
        {
            smk_kart k = {0};
            k.airborne = true;
            smk_kart_launch(&k, 0x00E0);
            for (int f = 0; f < 40 && k.airborne; f++) {
                smk_kart_gravity(&k);
                if ((int)(k.z >> 16) > peak) peak = (int)(k.z >> 16);
            }
        }
        int rpeak = 0;
        {
            smk_kart k = {0};
            k.speed = 400;                 /* under the $2E0 floor: the slowest launch */
            smk_kart_ramp(&k, 1);
            rpeak = (int)(k.z >> 16);      /* $1F = $280 from the first frame */
            for (int f = 0; f < 80 && k.airborne; f++) {
                smk_kart_gravity(&k);
                if ((int)(k.z >> 16) > rpeak) rpeak = (int)(k.z >> 16);
            }
        }
        char d[96];
        snprintf(d, sizeof d, "hop peaks at %d, ramp at %d, the wall test is 4", peak, rpeak);
        check("a hop cannot clear a wall but a ramp can", peak < 4 && rpeak >= 4, d);
    }
    {
        /* A kart wedged where both axes are blocked must NOT sit there:
         * $80F93C counts eight such frames and $80F964 ejects it along
         * the quadrant it faces at a flat $100 (NOTES 136).  Without it a
         * corner of the Ghost Valley rails is a dead stop. */
        static smk_track tk; static smk_player pk; smk_kart kk = {0};
        char e[256];
        if (smk_track_load(&rom, 16, -1, &tk, e, sizeof e)) {
            smk_blocks_bind(&tk);
            int cx = -1, cy = -1;
            for (int y = 1; y < 127 && cx < 0; y++)
                for (int x = 1; x < 127; x++) {
                    uint8_t here = tk.surface[tk.map[y * 128 + x]];
                    uint8_t nn = tk.surface[tk.map[(y - 1) * 128 + x]];
                    uint8_t ee = tk.surface[tk.map[y * 128 + x + 1]];
                    if (!smk_surface_solid(here) && smk_surface_solid(nn)
                        && smk_surface_solid(ee)) { cx = x * 8 + 4; cy = y * 8 + 4; break; }
                }
            if (cx >= 0) {
                smk_player_setup(&rom, 0, 1, &pk); smk_player_reset(&pk, 0);
                kk.x = (int32_t)cx << 16; kk.y = (int32_t)cy << 16; kk.speed = 400;
                pk.heading = pk.vel_angle = pk.pose = 0x2000;   /* into the corner */
                int32_t x0 = kk.x, y0 = kk.y;
                int moved = 0;
                for (int f = 0; f < 40; f++) {
                    smk_player_step(&pk, &kk, &tk, 0x8000, 0);
                    if (kk.x != x0 || kk.y != y0) moved = 1;
                }
                char d[128];
                snprintf(d, sizeof d, "corner (%d,%d) -> (%d,%d) speed %d",
                         cx, cy, smk_kart_px(kk.x), smk_kart_px(kk.y), kk.speed);
                check("a kart wedged in a corner gets ejected, not stopped dead",
                      moved && kk.speed > 0, d);
            }
        }
    }
    {
        /* Hitting a wall must COST something (NOTES 130): the impact frame
         * leaves the speed alone, the next frame damps it once, and it
         * then stays put for the whole window - measured in the game as
         * 418 held for eight frames.  Accelerating through the window is
         * what made bouncing free. */
        static smk_track tw; static smk_player pw; smk_kart kw = {0};
        char e[256];
        if (smk_track_load(&rom, 7, -1, &tw, e, sizeof e)) {
            int rx = -1, ry = -1;
            for (int cy = 1; cy < 127 && rx < 0; cy++)
                for (int cx = 1; cx < 120; cx++)
                    if (!smk_surface_solid(tw.surface[tw.map[cy * 128 + cx]])
                        && smk_surface_solid(tw.surface[tw.map[cy * 128 + cx + 3]])) {
                        rx = cx * 8 + 4; ry = cy * 8 + 4; break;
                    }
            smk_player_setup(&rom, 0, 1, &pw); smk_player_reset(&pw, 0);
            kw.x = (int32_t)rx << 16; kw.y = (int32_t)ry << 16; kw.speed = 835;
            pw.heading = pw.vel_angle = pw.pose = 0x4000;
            int hit = -1, at_hit = 0, after = 0, held = 1, drive = 0;
            for (int f = 0; f < 24; f++) {
                int before = kw.speed;
                smk_player_step(&pw, &kw, &tw, 0x8000, 0);
                if (hit < 0 && kw.bounce_cool > 0) {
                    hit = f; at_hit = kw.speed == before; drive = pw.drive;
                    (void)drive;
                } else if (hit >= 0 && f == hit + 1) {
                    after = kw.speed;
                } else if (hit >= 0 && f > hit + 1 && kw.bounce_cool > 0) {
                    if (kw.speed != after) held = 0;
                }
            }
            /* and it must RECOVER: the window ends and the throttle works
             * again.  A version of this that left the kart in drive state
             * $16 killed acceleration outright (NOTES 131). */
            /* $80A55B: once the window lets go the crash deceleration
             * bites - the speed must fall BELOW what the damping left,
             * and only then climb back.  Recovering straight from the
             * damped speed is what made crashing free (NOTES 132). */
            int trough = after, peak = 0;
            for (int f = 0; f < 40; f++) {
                smk_player_step(&pw, &kw, &tw, 0x8000, 0);
                if (kw.bounce_cool == 0 && kw.speed < trough) trough = kw.speed;
                if (kw.speed > peak) peak = kw.speed;
            }
            int recovered = peak > trough && trough < after - 100;
            char d[160];
            snprintf(d, sizeof d, "hit f%d kept %d, damped %d, held %d, trough %d, peak %d",
                     hit, at_hit, after, held, trough, peak);
            check("a wall hit costs speed, decelerates after it, then must be earned back",
                  hit > 0 && at_hit && after > 0 && after < 500 && held && recovered, d);
        }
    }
    {
        /* The object scale law and its ladder (NOTES 129), pinned at the
         * distances the bands fall on: scale = $4200 / axis depth, hidden
         * at $0300 and above, drawings chosen by $84DA3C = C0 60 30 00. */
        struct { float zf; int scale, tier; } W[] = {
            {  20.0f, 768,  0 },   /* clamped into band 0, still DRAWN        */
            {  22.0f, 768,  0 },
            {  23.0f, 735,  0 },
            {  88.0f, 192,  1 },   /* exactly $C0 is NOT above it            */
            {  87.0f, 194,  0 },
            { 282.0f,  60,  2 },
            { 280.0f,  60,  2 },
            { 563.0f,  30, -1 },   /* past the last threshold: not drawn     */
            { 560.0f,  30, -1 },
            { 500.0f,  34, -1 },
            { 300.0f,  56,  2 },
        };
        int bad = 0;
        char d[128];
        d[0] = 0;
        for (size_t i = 0; i < sizeof W / sizeof W[0]; i++) {
            int sc = (int)(SMK_OBJ_SCALE_K / W[i].zf + 0.5f);
            int ti;
            if (sc > SMK_OBJ_SCALE_HIDE) sc = SMK_OBJ_SCALE_HIDE;
            if (sc > SMK_OBJ_BAND0)       ti = 0;
            else if (sc > SMK_OBJ_BAND1)  ti = 1;
            else if (sc > SMK_OBJ_BAND2)  ti = 2;
            else ti = -1;
            if (sc != W[i].scale || ti != W[i].tier) {
                bad++;
                snprintf(d, sizeof d, "zf %.0f: scale %d want %d, tier %d want %d",
                         (double)W[i].zf, sc, W[i].scale, ti, W[i].tier);
            }
        }
        check("an object's drawing follows $4200/axis-depth through $84DA3C", !bad, d);
    }
    {
        /* Whatever is DRAWN is what you can hit (NOTES 151), and the
         * height that decides both has to come from one place.
         *
         * smk_course_movers_reset parks all 32 slots at SMK_MOVER_PARK
         * whatever the theme, and the step returns early where the theme
         * has no movers - so a pipe's raw mv[].z sits at 4096 all race.
         * Drawing gated on the theme and saw 0; collision did not, and
         * skipped everything above half the parked height.  Every pipe on
         * every non-mover track drew on the ground and was not solid.
         *
         * So: on a non-mover theme the raw z is parked and BOTH accessors
         * must say the object is on the ground. */
        static smk_course cmc, cbc;
        char d[160];
        int bad = 0;
        d[0] = 0;
        if (!smk_course_load(&rom, 7, &cmc) || !smk_course_load(&rom, 17, &cbc)) {
            bad = 1;
            snprintf(d, sizeof d, "course load failed");
        } else {
            if (smk_theme_has_movers(cmc.theme)) { bad = 1;
                snprintf(d, sizeof d, "track 7 theme %d unexpectedly has movers", cmc.theme); }
            if (!smk_theme_has_movers(cbc.theme)) { bad = 1;
                snprintf(d, sizeof d, "track 17 theme %d should have movers", cbc.theme); }
            if (!bad && cmc.mv[0].z != SMK_MOVER_PARK) { bad = 1;
                snprintf(d, sizeof d, "raw z %d, expected the parked %d",
                         cmc.mv[0].z, SMK_MOVER_PARK); }
            /* the pipe: parked raw, but on the ground to both readers */
            if (!bad && (smk_mover_z(&cmc, 0) != 0
                         || smk_mover_world(&cmc, 0) != 0.0f)) { bad = 1;
                snprintf(d, sizeof d, "non-mover theme: collision sees z %d, "
                         "drawing sees %.2f - both must be 0",
                         smk_mover_z(&cmc, 0), (double)smk_mover_world(&cmc, 0)); }
            /* the Thwomp: parked raw, and BOTH must still see it raised */
            if (!bad && (smk_mover_z(&cbc, 0) != cbc.mv[0].z
                         || smk_mover_world(&cbc, 0) <= 0.0f)) { bad = 1;
                snprintf(d, sizeof d, "mover theme: collision sees z %d, raw %d",
                         smk_mover_z(&cbc, 0), cbc.mv[0].z); }
        }
        check("a pipe is solid, and a parked Thwomp is still overhead", !bad, d);
    }
    {
        /* What makes the fall-behind-the-track priority work (NOTES 128):
         * the plane's VOID is palette index 0, which Mode 7 draws as
         * transparent, and the road is not.  So a sprite given a priority
         * under BG1 is hidden by the track and shows through the hole. */
        static smk_track tv;
        char e[256];
        if (smk_track_load(&rom, 16, -1, &tv, e, sizeof e)) {
            long vz = 0, vn = 0, rn = 0, rz = 0;
            for (int cy = 0; cy < SMK_MAP_DIM; cy++)
                for (int cx = 0; cx < SMK_MAP_DIM; cx++) {
                    uint8_t cls = tv.surface[tv.map[cy * SMK_MAP_DIM + cx]];
                    if (cls != 0x20 && cls != 0x40) continue;
                    for (int y = 0; y < 8; y++)
                        for (int x = 0; x < 8; x++) {
                            uint8_t v = smk_track_texel_index(&tv, cx * 8 + x, cy * 8 + y);
                            if (cls == 0x20) { if (v) vn++; else vz++; }
                            else             { if (v) rn++; else rz++; }
                        }
                }
            char d[160];
            snprintf(d, sizeof d, "void %ld/%ld opaque, road %ld/%ld", vn, vz + vn, rn, rn + rz);
            check("Ghost Valley's void is transparent and its road is not",
                  vn == 0 && vz > 1000 && rz == 0 && rn > 1000, d);
        }
    }
    {
        /* The lap-segment obstacle spawn (NOTES 127), replaying the run
         * measured in the game: waypoints 0/5/9 gave segment 0 and the
         * pair (268,92) (164,132); at 12 the segment flipped and both
         * objects jumped to (508,636) (148,676); at 27 the third
         * segment's slice is empty and the game falls back to the first. */
        static smk_course c7;
        if (smk_course_load(&rom, 7, &c7)) {
            char d[128];
            snprintf(d, sizeof d, "nseg=%d thresh %02X %02X %02X",
                     c7.nseg, c7.seg_thresh[0], c7.seg_thresh[1], c7.seg_thresh[2]);
            check("track 7's segment thresholds are the game's $84DACF",
                  c7.nseg == 3 && c7.seg_thresh[0] == 0x0C
                  && c7.seg_thresh[1] == 0x17 && c7.seg_thresh[2] == 0xFF, d);
            static const struct { int wp, seg, x0, y0, x1, y1; } R[] = {
                {  0, 0, 268,  92, 164, 132 },
                {  9, 0, 268,  92, 164, 132 },
                { 12, 1, 508, 636, 148, 676 },
                { 17, 1, 508, 636, 148, 676 },
                { 27, 2, 268,  92, 164, 132 },
            };
            int bad = 0;
            char d2[128];
            d2[0] = 0;
            for (size_t i = 0; i < sizeof R / sizeof R[0]; i++) {
                smk_course_spawn(&c7, R[i].wp, 0, false);
                /* FOUR live in 1P, measured off the cheep-cheep / choco
                 * recordings (the old "two" reading of $819136 was
                 * backwards); the windows' first two entities are the
                 * ones these captures pinned */
                if (c7.seg != R[i].seg || c7.nlive != 4
                    || c7.ent[c7.live[0]].x != R[i].x0 || c7.ent[c7.live[0]].y != R[i].y0
                    || c7.ent[c7.live[1]].x != R[i].x1 || c7.ent[c7.live[1]].y != R[i].y1) {
                    bad++;
                    snprintf(d2, sizeof d2, "wp %d: seg %d live %d (%d,%d)(%d,%d)",
                             R[i].wp, c7.seg, c7.nlive,
                             c7.ent[c7.live[0]].x, c7.ent[c7.live[0]].y,
                             c7.ent[c7.live[1]].x, c7.ent[c7.live[1]].y);
                }
            }
            check("the obstacles respawn per lap segment, four slots a window", !bad, d2);
            /* TWO DRIVERS, TWO SETS.  They used to refill one list from
             * their own segments, so an object blinked in and out of both
             * screens (the user).  Each takes its own four now, and one
             * driver moving on cannot disturb the other's. */
            {
                smk_course_spawn(&c7, R[0].wp, 0, true);   /* p1 in segment 0 */
                smk_course_spawn(&c7, R[2].wp, 1, true);   /* p2 in segment 1 */
                int a0 = c7.live[0], b0 = c7.live[4], b1 = c7.live[5];
                bool apart = a0 != b0;          /* the windows really differ */
                /* p1 drives into p2's segment: he picks that window up, and
                 * p2's own slots do not move underneath him */
                smk_course_spawn(&c7, R[2].wp, 0, true);
                char d3[160];
                snprintf(d3, sizeof d3, "nlive %d, windows %s, p1 %d,%d p2 %d,%d (was %d,%d)",
                         c7.nlive, apart ? "differ" : "SAME",
                         c7.live[0], c7.live[1], c7.live[4], c7.live[5], b0, b1);
                check("two drivers hold their own object blocks",
                      c7.nlive == 8 && apart
                      && c7.live[0] == b0 && c7.live[1] == b1
                      && c7.live[4] == b0 && c7.live[5] == b1, d3);
            }
            smk_course_spawn(&c7, R[0].wp, 0, false);   /* back to one driver */

            /* Ghost Valley has no static obstacles at all */
            static smk_course cg;
            if (smk_course_load(&rom, 16, &cg)) {
                smk_course_spawn(&cg, 10, 0, false);
                check("Ghost Valley spawns no static obstacles",
                      cg.nseg == 0 && cg.nent == 0 && cg.nlive == 0, NULL);
            }
        }
    }
    {
        /* The wall response, replaying three captures from the running
         * game (tools/labs/wall.py, NOTES 125).  Each row is the impact
         * frame's velocity and speed, then what the game had a frame
         * later - the damping and the re-derived scalar. */
        static const struct { int vx, vy, spd, dvx, dvy, dspd; } W[] = {
            {  833,   45,  835,  416,   42,  418 },
            {  573, -633,  855,  286, -594,  659 },
            {  879,  -80,  883,  439,  -75,  445 },
            {  662, -276,  718,  331, -259,  420 },
        };
        int bad = 0;
        char d[128];
        d[0] = 0;
        for (size_t i = 0; i < sizeof W / sizeof W[0]; i++) {
            smk_kart k = {0};
            k.vx = (int16_t)W[i].vx; k.vy = (int16_t)W[i].vy;
            k.speed = (int16_t)W[i].spd;
            k.bounce_dir = 0; k.bounce_pend = 1; k.bounce_cool = 9;
            smk_kart_bounce_damp_for_test(&k);
            if (k.vx != W[i].dvx || k.vy != W[i].dvy || k.speed != W[i].dspd) {
                bad++;
                snprintf(d, sizeof d, "row %d: got %d,%d spd %d want %d,%d spd %d",
                         (int)i, k.vx, k.vy, k.speed, W[i].dvx, W[i].dvy, W[i].dspd);
            }
        }
        check("the wall bounce damps and re-derives speed as the game does", !bad, d);
    }
    {
        /* The sector map and Lakitu's rescue (NOTES 124), both measured
         * against the running game's own WRAM. */
        static smk_course c7;
        if (smk_course_load(&rom, 7, &c7)) {
            int off = 0, zero = 0;
            for (int i = 0; i < SMK_SECT_CELLS; i++) {
                int sec = c7.map[i] & SMK_SECT_OFF;
                if (sec == SMK_SECT_OFF) off++; else if (sec == 0) zero++;
            }
            char d[96];
            snprintf(d, sizeof d, "$7F=%d sector0=%d", off, zero);
            check("track 7's sector map matches the game's own $7F:5000 census",
                  off == 1412 && zero == 78, d);
        }
        /* the rescue: it must LAND, on the waypoint, facing the field */
        static smk_track t5; static smk_course c5; static smk_player p5;
        char e5[256];
        if (smk_track_load(&rom, 5, -1, &t5, e5, sizeof e5)
            && smk_course_load(&rom, 5, &c5)) {
            smk_kart k5 = {0};
            smk_player_setup(&rom, 0, 1, &p5); smk_player_reset(&p5, 0);
            int sec = 10;
            p5.resc_x = c5.wx[sec]; p5.resc_y = c5.wy[sec];
            int fc = ((c5.wy[sec] >> 4) & 63) * 64 + ((c5.wx[sec] >> 4) & 63);
            p5.resc_h = (uint16_t)((c5.flow[fc] << 8) | c5.flow[(fc - 1) & 0xFFF]);
            /* drop it into the void well away from its waypoint */
            k5.x = (int32_t)(c5.wx[sec] + 100) << 16;
            k5.y = (int32_t)(c5.wy[sec] + 60) << 16;
            p5.hazard = 6; p5.resc_t = 0;
            int f = 0;
            for (; f < 1200 && p5.hazard; f++) smk_player_step(&p5, &k5, &t5, 0, 0);
            char d2[128];
            snprintf(d2, sizeof d2, "frame %d at (%d,%d) want (%d,%d) heading %04X want %04X",
                     f, smk_kart_px(k5.x), smk_kart_px(k5.y), p5.resc_x, p5.resc_y,
                     p5.heading, p5.resc_h);
            check("Lakitu puts the kart down, on its waypoint, facing the field",
                  p5.hazard == 0 && k5.z == 0
                  && smk_kart_px(k5.x) == p5.resc_x && smk_kart_px(k5.y) == p5.resc_y
                  && p5.heading == p5.resc_h, d2);
        }
    }
    {
        /* Breakable blocks (NOTES 123): the user's recorded Ghost Valley
         * session turned tile $1F (class $82) into $00 (class $20, the
         * void) over the sequence $26, $27, $28. */
        static smk_track gv;
        char err2[256];
        if (smk_track_load(&rom, 16, -1, &gv, err2, sizeof err2)) {
            smk_blocks_bind(&gv);
            check("class $82 is breakable, $80 is not",
                  smk_blocks_breakable(0x82) && smk_blocks_breakable(0x84)
                  && !smk_blocks_breakable(0x80) && !smk_blocks_breakable(0x40), NULL);
            int cell = 505;                       /* the cell the player broke */
            uint8_t was = gv.map[cell];
            smk_blocks_hit(cell, true);
            uint8_t seq[4];
            for (int i = 0; i < 4; i++) {
                for (int k = 0; k < 8; k++) smk_blocks_step();   /* one slot a frame */
                seq[i] = gv.map[cell];
            }
            char d2[96];
            snprintf(d2, sizeof d2, "%02X -> %02X %02X %02X %02X (class %02X)",
                     was, seq[0], seq[1], seq[2], seq[3], gv.surface[seq[3]]);
            check("a broken Ghost Valley block crumbles $26 $27 $28 $00 and leaves the void",
                  was == 0x1F && seq[0] == 0x26 && seq[1] == 0x27 && seq[2] == 0x28
                  && seq[3] == 0x00 && gv.surface[seq[3]] == 0x20, d2);
        }
        /* Vanilla Lake's ice, theme 4, the OTHER sequence at $80FC6C -
         * decoded from the ROM and confirmed in play by the user */
        static smk_track vl;
        if (smk_track_load(&rom, 4, -1, &vl, err2, sizeof err2)) {
            smk_blocks_bind(&vl);
            int cell = -1;
            for (int i = 0; i < SMK_MAP_BYTES && cell < 0; i++)
                if (smk_blocks_breakable(vl.surface[vl.map[i]])) cell = i;
            uint8_t was = cell >= 0 ? vl.map[cell] : 0;
            uint8_t seq[4] = {0};
            if (cell >= 0) {
                smk_blocks_hit(cell, true);
                for (int i = 0; i < 4; i++) {
                    for (int k = 0; k < 8; k++) smk_blocks_step();
                    seq[i] = vl.map[cell];
                }
            }
            char d3[96];
            snprintf(d3, sizeof d3, "%02X -> %02X %02X %02X %02X (class %02X)",
                     was, seq[0], seq[1], seq[2], seq[3], vl.surface[seq[3]]);
            check("a broken Vanilla Lake ice block crumbles $7B $7C $7D $08",
                  was == 0x7A && vl.surface[was] == 0x84
                  && seq[0] == 0x7B && seq[1] == 0x7C && seq[2] == 0x7D
                  && seq[3] == 0x08, d3);
        }
    }
    {
        /* the demo race is 2P: Mario and Toad; the game filled the grid
         * Luigi, Koopa, Bowser, Peach, DK, Yoshi (tools/labs/mame log) */
        int g[8];
        smk_grid_order(&rom, 0, 7, true, g);
        char d[64];
        snprintf(d, sizeof d, "%d %d %d %d %d %d %d %d", g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7]);
        check("grid order for P1 Mario / P2 Toad matches the demo race",
              g[0] == 0 && g[1] == 7 && g[2] == 1 && g[3] == 6 && g[4] == 2 && g[5] == 3 && g[6] == 4 && g[7] == 5, d);
        smk_grid_order(&rom, 0, 0, false, g);
        check("1P Mario: the rival in slot 1 is the row's 7th entry (Yoshi)", g[1] == 5 && g[7] == 4 && g[2] == 2, NULL);
    }

    {
        /* bug 13: a kart under a DESCENDING Thwomp is squashed (hazard
         * kind 2), and a PARKED one overhead is driven under untouched */
        smk_course c9;
        if (smk_course_load(&rom, 9, &c9)) {
            int e = c9.nlive ? c9.live[0] : 0;
            smk_kart k = { 0 };
            k.x = ((int32_t)c9.ent[e].x + 4) << SMK_POS_SHIFT;
            k.y = (int32_t)c9.ent[e].y << SMK_POS_SHIFT;
            c9.mv[e].phase = SMK_MV_FALL; c9.mv[e].z = 200;   /* the CONTACT band */
            smk_collide_objects(&k, &c9);
            check("a falling Thwomp squashes the kart under it (kind 2)",
                  k.hazard_hit == 2, NULL);
            k.hazard_hit = 0; k.vx = k.vy = 0;
            c9.mv[e].z = 2000;                    /* still descending, overhead */
            smk_collide_objects(&k, &c9);
            check("a falling Thwomp NOT yet in contact is neither hit nor wall",
                  k.hazard_hit == 0 && k.vx == 0 && k.vy == 0, NULL);
            k.hazard_hit = 0;
            c9.mv[e].phase = SMK_MV_PARK; c9.mv[e].z = SMK_MOVER_PARK;
            smk_collide_objects(&k, &c9);
            check("a parked Thwomp overhead is not a hit and not a wall",
                  k.hazard_hit == 0 && k.vx == 0 && k.vy == 0, NULL);
        }
    }
    {
        /* bug 14: the spawn offsets are the $84DAC5 TABLE, whose entry 4
         * is ZERO - Bowser Castle 1's (track 17) last segment respawns
         * the FIRST Thwomp pair, measured in the oracle (bc1seg.py:
         * waypoint 40 spawns (388,68)/(388,52) again, never ents 16-17) */
        smk_course bc1;
        if (smk_course_load(&rom, 17, &bc1)) {
            char d[64];
            snprintf(d, sizeof d, "off %d %d %d %d %d", bc1.seg_off[0],
                     bc1.seg_off[1], bc1.seg_off[2], bc1.seg_off[3], bc1.seg_off[4]);
            check("the $84DAC5 spawn offsets read 0/8/16/24 then ZERO",
                  bc1.seg_off[0] == 0 && bc1.seg_off[1] == 8 && bc1.seg_off[2] == 16
                  && bc1.seg_off[3] == 24 && bc1.seg_off[4] == 0, d);
            smk_course_spawn(&bc1, 40, 0, false);
            check("BC1's last segment respawns the FIRST window (oracle: wp 40)",
                  bc1.nlive == 4 && bc1.live[0] == 0 && bc1.live[1] == 1
                  && bc1.live[3] == 3
                  && bc1.ent[0].x == 388 && bc1.ent[0].y == 68, NULL);
        }
    }

    {   /* THE EFFECT ATTRIBUTE IS AN OR, NOT AN XOR (NOTES 268).
         *
         * The record's flags byte is ORed onto each sprite's attribute.
         * It matters in exactly one place: kind $24, the road drift puff,
         * whose templates carry attr $3E with a flags byte of $05.  ORed,
         * that is palette 7 - the game's own dust, captured off a real
         * frame as $AD9C52 / $8C7B31, which ARE palette 7's entries 7 and
         * 6.  XORed it is palette 5, which on Ghost Valley's theme is
         * reds, white and greys: the grey smoke the user reported. */
        static smk_effects fxe;
        if (smk_effects_load(&rom, &fxe)) {
            static const int kinds[] = { 0x00,0x06,0x0C,0x12,0x18,0x1E,0x24,0x2A,0x30,0x36,0x3C };
            int differ = 0, d24 = 0, bad = 0, seen24 = 0;
            for (unsigned q = 0; q < sizeof kinds / sizeof kinds[0]; q++) {
                const smk_effect_kind *k = &fxe.kind[kinds[q] / 6];
                if (!k->valid) continue;
                for (int f = 0; f < 12; f++)
                    for (int sc = 0; sc < k->script[f].n; sc++) {
                        const smk_effect_template *t = &k->script[f].t[sc];
                        for (int j = 0; j < t->n; j++) {
                            int po = ((t->attr[j] | k->attr_xor) >> 1) & 7;
                            int px = ((t->attr[j] ^ k->attr_xor) >> 1) & 7;
                            if (po != px) { differ++; if (kinds[q] == 0x24) d24++; }
                            if (kinds[q] == 0x24) { seen24++; if (po != 7) bad++; }
                        }
                    }
            }
            char de[96];
            snprintf(de, sizeof de, "%d sprites differ, %d of them kind $24", differ, d24);
            check("the effect attribute ORs: only the road drift puff is affected",
                  differ > 0 && differ == d24, de);
            snprintf(de, sizeof de, "%d of %d sprites off palette 7", bad, seen24);
            check("the road drift puff draws in OBJ palette 7 (the game's own dust)",
                  seen24 > 0 && bad == 0, de);
        }
    }

    printf("\n%d passed, %d failed\n", pass, fail);
    smk_rom_free(&rom);
    return fail ? 1 : 0;
}
