/* The AI row chooser, replayed against the real game.
 *
 *   tools/rowcheck <rowlog.csv>
 *
 * tools/labs/mame/rowlog.lua logs every input $80ADA0 reads and the $C8
 * it produced, for all eight karts of a recorded race.  This feeds those
 * inputs to the SHIPPED routine and counts how often it answers what the
 * original answered - 39,074 kart-frames of ground truth, with none of
 * the race-dynamics confound that comparing two different races has.
 */
#include "smk.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define NF 14                   /* fields per kart in rowlog.csv */
enum { F_C8, F_DA, F_E2, F_E6, F_10, F_84, F_90, F_92, F_94, F_96,
       F_C1, F_SPD, F_X, F_Y };

int main(int argc, char **argv)
{
    /* $80AF0F has to be loaded or every catch-up distance is zero and
     * every distance test passes - which reads exactly like a broken
     * port.  It cost one debugging round; the check stays. */
    smk_rom rom; char err[256];
    if (!smk_rom_load(&rom, "rom/smk_usa.sfc", err, sizeof err)) {
        printf("skipped: %s\n", err); return 77;
    }
    if (!smk_ai_catchup_load(&rom)) { printf("catch-up table failed\n"); return 1; }

    { const char *e = getenv("SMK_AI_SKILL"); if (e) smk_ai_skill = atoi(e); }
    const char *path = argc > 1 ? argv[1] : "tools/labs/mame/rowlog_flag.csv";
    FILE *f = fopen(path, "r");
    if (!f) { printf("skipped: no %s\n", path); return 77; }

    static char line[8192];
    long agree = 0, total = 0, started = 0;
    long conf[4][4]; memset(conf, 0, sizeof conf);
    while (fgets(line, sizeof line, f)) {
        if (line[0] < '0' || line[0] > '9') continue;
        int v[1 + 10 + 8 * NF], n = 0;
        for (char *t = strtok(line, ","); t && n < (int)(sizeof v / sizeof *v);
             t = strtok(NULL, ",")) v[n++] = atoi(t);
        if (n != 1 + 10 + 8 * NF) continue;
        const int *K = v + 11;
        #define G(k, F) K[(k) * NF + (F)]
        int moving = 0;
        for (int k = 0; k < 8; k++) if (G(k, F_SPD) > 0) moving = 1;
        if (!started && !moving) continue;
        started = 1;

        static smk_racer r[8];
        smk_racer *by_rank[8]; memset(by_rank, 0, sizeof by_rank);
        for (int k = 0; k < 8; k++) {
            memset(&r[k], 0, sizeof r[k]);
            r[k].rank      = G(k, F_E6) / 2;
            r[k].da        = G(k, F_DA);
            /* the game's own $C1 unless a fallback is being swept: the
             * port has to choose one value, because $C1 is not decoded */
            r[k].skill     = smk_ai_skill >= 0 ? smk_ai_skill
                                               : (G(k, F_C1) & 7);
            r[k].is_player = (G(k, F_10) & 0x8000) != 0;
            r[k].trouble   = G(k, F_84) != 0 || (G(k, F_10) & 0x0020) != 0;
            r[k].row       = G(k, F_C8) / 2;      /* the neighbour's own row */
            r[k].k.x = (int32_t)G(k, F_X) << SMK_POS_SHIFT;
            r[k].k.y = (int32_t)G(k, F_Y) << SMK_POS_SHIFT;
            if (r[k].rank >= 0 && r[k].rank < 8) by_rank[r[k].rank] = &r[k];
        }
        int s04 = 0, s06 = 0;
        for (int k = 0; k < 8; k++) {
            if (r[k].is_player) continue;
            int rk = r[k].rank;
            int got = smk_ai_row_for(&r[k], rk > 0 ? by_rank[rk - 1] : NULL,
                                     rk < 7 ? by_rank[rk + 1] : NULL,
                                     by_rank[2], &s04, &s06);
            int want = G(k, F_C8) / 2;
            total++; if (got == want) agree++;
            conf[want / 4][got / 4]++;
        }
        #undef G
    }
    fclose(f);
    if (!total) { printf("skipped: %s held no race frames\n", path); return 77; }
    printf("  %ld/%ld kart-frames agree with the original (%.2f%%)\n",
           agree, total, 100.0 * (double)agree / (double)total);
    static const char *NM[4] = { "$00", "$08", "$10", "$18" };
    printf("  ROM \\ ours   %s     %s     %s     %s\n", NM[0], NM[1], NM[2], NM[3]);
    for (int a = 0; a < 4; a++) {
        printf("      %s   ", NM[a]);
        for (int b = 0; b < 4; b++) printf("%8ld", conf[a][b]);
        printf("\n");
    }
    return agree * 100 >= total * 90 ? 0 : 1;
}
