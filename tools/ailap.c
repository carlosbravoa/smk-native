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
#include <string.h>

#define GP_TRACKS   20
#define MAX_FRAMES  7200      /* 2 minutes at 60 Hz */

int main(int argc, char **argv)
{
    const char *rom_path = argc > 1 ? argv[1] : "rom/smk_usa.sfc";
    smk_rom rom;
    char err[256];
    if (!smk_rom_load(&rom, rom_path, err, sizeof err)) {
        printf("skipped: %s\n", err);
        return 77;
    }
    int ok = 0;
    for (int t = 0; t < GP_TRACKS; t++) {
        smk_track trk;
        static smk_course crs;
        smk_physics phys;
        if (!smk_track_load(&rom, t, -1, &trk, err, sizeof err)) continue;
        if (!smk_course_load(&rom, t, &crs)) continue;
        if (!smk_physics_load(&rom, 0, &phys)) continue;
        smk_track_place_objects(&rom, &trk);
        course_for_step = &crs;

        static smk_racer racers[SMK_CHARACTERS];
        for (int i = 0; i < SMK_CHARACTERS; i++)
            smk_racer_start(&racers[i], &crs, i);

        int lapped = 0, at = 0;
        for (int f = 0; f < MAX_FRAMES && !lapped; f++) {
            for (int i = 1; i < SMK_CHARACTERS; i++) {
                smk_racer_step(&racers[i], &trk, &crs, &phys);
                if (racers[i].lap >= 1) { lapped = 1; at = f; }
            }
        }
        if (lapped) ok++;
        printf("  track %2d: %s%s\n", t,
               lapped ? "lap" : "NO LAP",
               lapped ? "" : "  <-- regression");
        if (lapped && at > MAX_FRAMES / 2)
            printf("            (slow: %d frames)\n", at);
    }
    printf("%d/%d GP tracks: AI completes a lap\n", ok, GP_TRACKS);
    smk_rom_free(&rom);
    return ok == GP_TRACKS ? 0 : 1;
}
