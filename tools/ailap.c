/* AI lap regression - against the SHIPPED AI.
 *
 * This links src/ai.c, the same code the game runs.  The previous
 * version of this test lived outside the repo and could only have been a
 * second copy of the AI logic, since racer_step was static in main.c: it
 * could pass while the real AI was broken.  One implementation, one test.
 *
 * Every GP track must see an AI kart complete a lap within the budget.
 */
#include "smk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GP_TRACKS   20
#define MAX_FRAMES  9000      /* 2.5 minutes at 60 Hz */

/* One course: the seven AI karts drive it, and the result is whether
 * they lap, how fast, and how many frames any of them spent on a hazard
 * class (the NOTES 294 measure - the field off the void). */
static int run_course(const smk_rom *rom, int t, int cls, int need, int *lap_frames, long *hazard)
{
    static smk_track trk;
    static smk_course crs;
    smk_physics phys;
    char err[256];
    if (!smk_track_load(rom, t, -1, &trk, err, sizeof err)) { printf("  %s\n", err); return -1; }
    if (!smk_course_load(rom, t, &crs)) { printf("  track %d: no course data\n", t); return -1; }
    if (!smk_physics_load(rom, cls, &phys)) return -1;
    smk_track_place_objects(rom, &trk);
    course_for_step = &crs;

    static smk_racer racers[SMK_CHARACTERS];
    for (int i = 0; i < SMK_CHARACTERS; i++)
        smk_racer_start(&racers[i], &crs, i);

    int lapped = 0, at = 0;
    *hazard = 0;
    /* SMK_AILAP_TRACE=path: every kart every frame - class, kart, frame,
     * x, y, speed, sector, row - to see WHERE a course is slow or wet,
     * not just that it is */
    FILE *tr = getenv("SMK_AILAP_TRACE") ? fopen(getenv("SMK_AILAP_TRACE"), cls == 0 ? "w" : "a") : NULL;
    for (int f = 0; f < MAX_FRAMES && !lapped; f++) {
        for (int i = 1; i < SMK_CHARACTERS; i++) {
            smk_racer_step(&racers[i], &trk, &crs, &phys);
            if (tr)
                fprintf(tr, "%d %d %d %d %d %d %d %d\n", cls, i, f, smk_kart_px(racers[i].k.x), smk_kart_px(racers[i].k.y),
                        racers[i].k.speed, racers[i].sector, crs.wattr[racers[i].sector < 0 ? 0 : racers[i].sector] & 3);
            uint8_t s = smk_track_surface(&trk, smk_kart_px(racers[i].k.x), smk_kart_px(racers[i].k.y));
            if (s >= 0x20 && s < 0x40 && !racers[i].k.airborne) (*hazard)++;
            if (racers[i].lap >= need) { lapped = 1; at = f; }
        }
    }
    if (tr) fclose(tr);
    if (!lapped) {
        /* where the field got to: the kart that went furthest, its
         * position, sector and the class under it, and whether it was
         * still moving - the editor turns this into advice */
        int best = 1, bp = -1;
        for (int i = 1; i < SMK_CHARACTERS; i++) {
            int prog = (racers[i].lap << 8) | (racers[i].sector < 0 ? 0 : racers[i].sector);
            if (prog > bp) { bp = prog; best = i; }
        }
        const smk_racer *r = &racers[best];
        int px = smk_kart_px(r->k.x), py = smk_kart_px(r->k.y);
        printf("  stalled: x %d y %d sector %d of %d lap %d speed %d class $%02X kart %d\n",
               px, py, r->sector, crs.sectors, r->lap, r->k.speed,
               smk_track_surface(&trk, px, py), best);
    }
    *lap_frames = at;
    return lapped;
}

int main(int argc, char **argv)
{
    const char *rom_path = argc > 1 ? argv[1] : "rom/smk_usa.sfc";
    smk_rom rom;
    char err[256];
    if (!smk_rom_load(&rom, rom_path, err, sizeof err)) {
        printf("skipped: %s\n", err);
        return 77;
    }
    smk_ai_catchup_load(&rom);
    smk_tracks_scan_default();

    /* smk_ailap ROM [course ...]: named courses (an index, a slug or a
     * package directory) at all three classes; nothing named is the GP
     * regression at 50cc. */
    if (argc > 2) {
        int ok = 0, n = 0;
        for (int a = 2; a < argc; a++) {
            int t = (argv[a][0] >= '0' && argv[a][0] <= '9') ? atoi(argv[a]) : smk_tracks_find(argv[a]);
            if (t < 0) { printf("  %s: %s\n", argv[a], smk_tracks_error()); n++; continue; }
            for (int cls = 0; cls < 3; cls++) {
                int at; long hz;
                /* a FULL lap: the grid is behind the line, so lap 1 is
                 * the first crossing and lap 2 the loop driven round */
                int r = run_course(&rom, t, cls, 2, &at, &hz);
                n++;
                if (r > 0) ok++;
                printf("  %-16s %dcc: %s  full lap at frame %d, %ld hazard frames%s\n",
                       smk_tracks_id(t), 50 + cls * 50,
                       r > 0 ? "lap" : r == 0 ? "NO LAP" : "no course", at, hz,
                       hz ? "  <-- off the road" : "");
            }
        }
        printf("%d/%d runs: AI completes a lap\n", ok, n);
        smk_rom_free(&rom);
        return ok == n ? 0 : 1;
    }

    int ok = 0;
    for (int t = 0; t < GP_TRACKS; t++) {
        int at; long hz;
        int lapped = run_course(&rom, t, 0, 1, &at, &hz);
        if (lapped > 0) ok++;
        printf("  track %2d: %s%s\n", t,
               lapped > 0 ? "lap" : "NO LAP",
               lapped > 0 ? "" : "  <-- regression");
        if (lapped > 0 && at > MAX_FRAMES / 2)
            printf("            (slow: %d frames)\n", at);
    }
    printf("%d/%d GP tracks: AI completes a lap\n", ok, GP_TRACKS);
    smk_rom_free(&rom);
    return ok == GP_TRACKS ? 0 : 1;
}
