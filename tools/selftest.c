/* Headless verification of the C asset pipeline.
 *
 * Checks the same facts the Python suite checks, but through the code the
 * game actually runs, so the two implementations cannot silently diverge.
 */
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
        int on_road = 0;
        for (int tr = 0; tr < SMK_TRACK_COUNT; tr++) {
            static smk_track tt;
            if (!smk_track_load(&rom, tr, -1, &tt, err, sizeof err)) continue;
            float sx, sy, sa;
            smk_track_start(&tt, 0, &sx, &sy, &sa);
            if (!smk_surface_solid(smk_track_surface(&tt, (int)sx, (int)sy)))
                on_road++;
        }
        snprintf(det, sizeof det, "%d/%d", on_road, SMK_TRACK_COUNT);
        check("every course starts on drivable ground",
              on_road == SMK_TRACK_COUNT, det);
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

    printf("\n%d passed, %d failed\n", pass, fail);
    smk_rom_free(&rom);
    return fail ? 1 : 0;
}
