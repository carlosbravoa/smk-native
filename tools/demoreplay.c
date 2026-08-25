/* Replay the attract race's recorded inputs through the port and score the
 * trajectory against the game's own.
 *
 *   smk_demoreplay rom.sfc demo_race.csv [1000|1100] [--tol PX] [--no-resync]
 *                  [--trace A B] [--gate]
 *
 * The CSV is tools/labs/mame/demolog.lua's per-frame log of the running
 * game (both karts).  The port is set up from the log's initial state -
 * track, character, class, position, heading, coins - and then driven
 * only by the recorded pad words.  Each frame the position, heading and
 * speed are compared with the game's; when the position error exceeds
 * the tolerance the kart is resynchronised from the log and the
 * divergence is recorded with the game state at that frame, so the
 * output says WHERE and in what situation the port stops matching.
 */
#include "smk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int16_t s16(int v) { return (int16_t)(uint16_t)v; }

int main(int argc, char **argv)
{
    const char *rom_path = argc > 1 ? argv[1] : "rom/smk_usa.sfc";
    const char *csv = argc > 2 ? argv[2] : "tools/labs/mame/demo_race.csv";
    int kart_id = 1000;                 /* the log names karts by WRAM block: 1000 / 1100 */
    double tol = 4.0;
    bool resync = true, gate = false;
    int trace_a = -1, trace_b = -1;
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--tol") && i + 1 < argc) tol = atof(argv[++i]);
        else if (!strcmp(argv[i], "--no-resync")) resync = false;
        else if (!strcmp(argv[i], "--gate")) gate = true;
        else if (!strcmp(argv[i], "--trace") && i + 2 < argc) { trace_a = atoi(argv[++i]); trace_b = atoi(argv[++i]); }
        else kart_id = atoi(argv[i]);
    }

    static smk_demolog log;
    if (!smk_demolog_load(csv, kart_id, &log)) { fprintf(stderr, "cannot read %s for kart %d\n", csv, kart_id); return 2; }

    smk_rom rom;
    char err[256];
    if (!smk_rom_load(&rom, rom_path, err, sizeof err)) { fprintf(stderr, "%s\n", err); return 2; }

    static smk_track trk;
    static smk_course crs;
    if (!smk_track_load(&rom, log.track, -1, &trk, err, sizeof err)) { fprintf(stderr, "%s\n", err); return 2; }
    smk_track_place_objects(&rom, &trk);
    if (!smk_course_load(&rom, log.track, &crs)) { fprintf(stderr, "course %d\n", log.track); return 2; }

    static smk_player p;
    if (!smk_player_setup(&rom, log.character, log.engine_class, &p)) { fprintf(stderr, "player setup\n"); return 2; }

    smk_kart k;
    memset(&k, 0, sizeof k);
    int i0 = log.start;
    smk_demolog_sync(&log, i0, &p, &k);
    int track = log.track, character = log.character, engine_class = log.engine_class;
    int nrows = log.n;

    printf("demo replay: track %d, character %d, %dcc, kart %d, %d frames from %d, tol %.1f px%s\n",
           track, character, engine_class == 0 ? 50 : engine_class == 1 ? 100 : 150,
           kart_id, nrows - i0 - 1, i0, tol, resync ? "" : ", no resync");

    int n = 0, within1 = 0, within_tol = 0, resyncs = 0, streak = 0, best_streak = 0;
    double sum_err = 0, max_err = 0;
    int max_err_frame = -1, head_bad = 0, spd_bad = 0;
    for (int i = i0 + 1; i < nrows; i++) {
        const smk_demo_frame *r = &log.f[i];
        uint16_t c4 = r->c4, held, pressed;
        smk_demolog_pad(r, &held, &pressed);
        p.coins = r->coins;
        smk_player_step(&p, &k, &trk, held, pressed);
        smk_collide_objects(&k, &crs);

        double gx = r->x / 65536.0, gy = r->y / 65536.0;
        double px = k.x / 65536.0, py = k.y / 65536.0;
        double dx = px - gx, dy = py - gy;
        if (dx > 512) dx -= 1024;
        if (dx < -512) dx += 1024;
        if (dy > 512) dy -= 1024;
        if (dy < -512) dy += 1024;
        double e = sqrt(dx * dx + dy * dy);
        int dh = s16((int)p.heading - r->a4);
        int ds = k.speed - r->speed;
        n++;
        if (i >= trace_a && i <= trace_b)
            printf("  f%4d pad %04X | spd %4d/%4d | B2 %5d/%5d | A4 %5d/%5d | A8 %5d/%5d | A6 %02X/%02X | pos game %8.3f,%8.3f port %8.3f,%8.3f err %.2f\n",
                   i, c4, r->speed, k.speed, r->turn, p.turn, r->a4, p.heading,
                   r->vlag, p.vlag, r->state, p.state, gx, gy, px, py, e);
        sum_err += e;
        if (e <= 1.0) within1++;
        if (e <= tol) { within_tol++; streak++; if (streak > best_streak) best_streak = streak; }
        if (e > max_err) { max_err = e; max_err_frame = i; }
        if (dh < -8 || dh > 8) head_bad++;
        if (ds < -2 || ds > 2) spd_bad++;
        if (e > tol && resync) {
            resyncs++;
            printf("  diverged at frame %4d: err %6.1f px, heading %+6d, speed %+4d | game: spd %4d $A6 %02X $AC %02X $10 %04X $E2 %04X pad %04X\n",
                   i, e, dh, ds, r->speed, r->state, r->drive, r->flags10, r->flags, c4);
            smk_demolog_sync(&log, i, &p, &k);
            streak = 0;
        }
    }
    printf("frames %d: within 1 px %d (%.1f%%), within %.0f px %d (%.1f%%), mean err %.2f px, "
           "max %.1f px at %d\n", n, within1, 100.0 * within1 / n, tol, within_tol,
           100.0 * within_tol / n, sum_err / n, max_err, max_err_frame);
    printf("heading off by >8 on %d frames, speed off by >2 on %d frames, resyncs %d, "
           "longest run within tol %d frames\n", head_bad, spd_bad, resyncs, best_streak);
    if (gate) {
        /* The gate: what the port achieves today, so a regression shows.
         * The demo's mushroom and its two collisions are not modelled and
         * cost P1 its resyncs; the rest of the race must stay on rails. */
        bool ok = 100.0 * within_tol / n >= 99.0 && best_streak >= 800
               && 100.0 * within1 / n >= 85.0;
        printf("demo replay gate (kart %d): %s\n", kart_id, ok ? "PASS" : "FAIL");
        return ok ? 0 : 1;
    }
    return 0;
}
