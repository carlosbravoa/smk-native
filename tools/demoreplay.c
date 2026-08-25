/* Replay the attract race's recorded inputs through the port and score the
 * trajectory against the game's own.
 *
 *   smk_demoreplay rom.sfc demo_race.csv [1000|1100] [--tol PX] [--no-resync]
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

#define MAXF 4000
#define MAXC 64

static int ncol;
static char *cols[MAXC];
static int col(const char *name)
{
    for (int i = 0; i < ncol; i++) if (!strcmp(cols[i], name)) return i;
    fprintf(stderr, "missing column %s\n", name);
    exit(2);
}

typedef struct { int v[MAXC]; } row_t;
static row_t rows[MAXF];
static int nrows;

static int16_t s16(int v) { return (int16_t)(uint16_t)v; }

int main(int argc, char **argv)
{
    const char *rom_path = argc > 1 ? argv[1] : "rom/smk_usa.sfc";
    const char *csv = argc > 2 ? argv[2] : "tools/labs/mame/demo_race.csv";
    int kart_id = 1000;                 /* the log names karts by WRAM block: 1000 / 1100 */
    double tol = 4.0;
    bool resync = true;
    int trace_a = -1, trace_b = -1;
    bool gate = false;
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--tol") && i + 1 < argc) tol = atof(argv[++i]);
        else if (!strcmp(argv[i], "--no-resync")) resync = false;
        else if (!strcmp(argv[i], "--gate")) gate = true;
        else if (!strcmp(argv[i], "--trace") && i + 2 < argc) { trace_a = atoi(argv[++i]); trace_b = atoi(argv[++i]); }
        else kart_id = atoi(argv[i]);
    }

    FILE *f = fopen(csv, "r");
    if (!f) { perror(csv); return 2; }
    static char line[4096];
    if (!fgets(line, sizeof line, f)) return 2;
    line[strcspn(line, "\r\n")] = 0;
    for (char *t = strtok(line, ","); t && ncol < MAXC; t = strtok(NULL, ","))
        cols[ncol++] = strdup(t);
    int c_kart = col("kart"), c_c4 = col("fC4"), c_x = col("f18"), c_xf = col("f16");
    int c_y = col("f1C"), c_yf = col("f1A"), c_a4 = col("fA4"), c_ea = col("fEA");
    int c_e8 = col("fE8"), c_a8 = col("fA8"), c_aa = col("fAA"), c_fa = col("fFA");
    int c_b2 = col("fB2"), c_a6 = col("fA6"), c_ac = col("fAC"), c_e2 = col("fE2");
    int c_z = col("f1F"), c_zv = col("f26"), c_vx = col("f22"), c_vy = col("f24");
    int c_coins = col(kart_id == 1000 ? "gE00" : "gE02");
    int c_track = col("g124"), c_class = col("g30"), c_char = col("f12"), c_f10 = col("f10");
    int c_ae = col("fAE"), c_ee = col("fEE"), c_a2 = col("fA2");
    (void)c_ae; (void)c_a2;
    while (fgets(line, sizeof line, f) && nrows < MAXF) {
        int i = 0;
        row_t r;
        for (char *t = strtok(line, ","); t && i < ncol; t = strtok(NULL, ","))
            r.v[i++] = (int)strtol(t, NULL, 10);
        if (r.v[c_kart] == kart_id) rows[nrows++] = r;
    }
    fclose(f);
    if (nrows < 10) { fprintf(stderr, "no rows for kart %x\n", kart_id); return 2; }

    smk_rom rom;
    char err[256];
    if (!smk_rom_load(&rom, rom_path, err, sizeof err)) { fprintf(stderr, "%s\n", err); return 2; }

    int track = rows[0].v[c_track];
    int engine_class = rows[0].v[c_class] / 2;
    int character = rows[0].v[c_char] / 2;
    static smk_track trk;
    static smk_course crs;
    if (!smk_track_load(&rom, track, -1, &trk, err, sizeof err)) { fprintf(stderr, "%s\n", err); return 2; }
    smk_track_place_objects(&rom, &trk);
    if (!smk_course_load(&rom, track, &crs)) { fprintf(stderr, "course %d\n", track); return 2; }

    static smk_player p;
    if (!smk_player_setup(&rom, character, engine_class, &p)) { fprintf(stderr, "player setup\n"); return 2; }

    /* start at the frame before the kart first moves: the game holds the
     * grid until the lights, above the per-kart dispatcher */
    int i0 = 0;
    while (i0 < nrows - 1 && s16(rows[i0 + 1].v[c_ea]) == 0) i0++;

    smk_kart k;
    memset(&k, 0, sizeof k);
#define SYNC(i) do { const row_t *r = &rows[i]; \
        k.x = ((int32_t)r->v[c_x] << 16) | (r->v[c_xf] & 0xFFFF); \
        k.y = ((int32_t)r->v[c_y] << 16) | (r->v[c_yf] & 0xFFFF); \
        k.vx = s16(r->v[c_vx]); k.vy = s16(r->v[c_vy]); \
        k.speed = s16(r->v[c_ea]); k.speed_frac = (uint16_t)r->v[c_e8]; \
        k.z = (int32_t)r->v[c_z] << 8; k.zvel = s16(r->v[c_zv]); \
        k.airborne = (r->v[c_e2] & 0x8000) != 0; k.bounce_cool = 0; k.bvx = k.bvy = 0; \
        smk_player_reset(&p, (uint16_t)r->v[c_a4]); \
        p.vlag = s16(r->v[c_a8]); p.plag = s16(r->v[c_aa]); p.spin = s16(r->v[c_fa]); \
        p.turn = s16(r->v[c_b2]); p.state = r->v[c_a6]; p.flags = (uint16_t)(r->v[c_e2] & 0x802C); \
        p.pad = (uint16_t)r->v[c_c4]; \
        p.vel_angle = (uint16_t)(p.heading + p.vlag); p.pose = (uint16_t)(p.heading - p.plag); \
        p.coins = r->v[c_coins]; p.accel32 = (int32_t)s16(r->v[c_ee]) << 16; } while (0)
    SYNC(i0);

    printf("demo replay: track %d, character %d, %dcc, kart %d, %d frames from %d, tol %.1f px%s\n",
           track, character, engine_class == 0 ? 50 : engine_class == 1 ? 100 : 150,
           kart_id, nrows - i0 - 1, i0, tol, resync ? "" : ", no resync");

    int n = 0, within1 = 0, within_tol = 0, resyncs = 0, streak = 0, best_streak = 0;
    double sum_err = 0, max_err = 0;
    int max_err_frame = -1, head_bad = 0, spd_bad = 0;
    for (int i = i0 + 1; i < nrows; i++) {
        const row_t *r = &rows[i];
        uint16_t c4 = (uint16_t)r->v[c_c4];
        uint16_t held = (uint16_t)(c4 & 0xFFF0);
        uint16_t pressed = (uint16_t)(((c4 & 3) << 8) | ((c4 & 0xC) << 2));
        p.coins = r->v[c_coins];
        smk_player_step(&p, &k, &trk, held, pressed);
        smk_collide_objects(&k, &crs);

        double gx = r->v[c_x] + r->v[c_xf] / 65536.0, gy = r->v[c_y] + r->v[c_yf] / 65536.0;
        double px = k.x / 65536.0, py = k.y / 65536.0;
        double dx = px - gx, dy = py - gy;
        if (dx > 512) dx -= 1024;
        if (dx < -512) dx += 1024;
        if (dy > 512) dy -= 1024;
        if (dy < -512) dy += 1024;
        double e = sqrt(dx * dx + dy * dy);
        int dh = s16((int)p.heading - r->v[c_a4]);
        int ds = k.speed - s16(r->v[c_ea]);
        n++;
        if (i >= trace_a && i <= trace_b)
            printf("  f%4d pad %04X | spd %4d/%4d | B2 %5d/%5d | A4 %5d/%5d | A8 %5d/%5d | A6 %02X/%02X | pos game %8.3f,%8.3f port %8.3f,%8.3f err %.2f\n",
                   i, c4, s16(r->v[c_ea]), k.speed, s16(r->v[c_b2]), p.turn, r->v[c_a4], p.heading,
                   s16(r->v[c_a8]), p.vlag, r->v[c_a6], p.state, gx, gy, px, py, e);
        sum_err += e;
        if (e <= 1.0) within1++;
        if (e <= tol) { within_tol++; streak++; if (streak > best_streak) best_streak = streak; }
        if (e > max_err) { max_err = e; max_err_frame = i; }
        if (dh < -8 || dh > 8) head_bad++;
        if (ds < -2 || ds > 2) spd_bad++;
        if (e > tol && resync) {
            resyncs++;
            printf("  diverged at frame %4d: err %6.1f px, heading %+6d, speed %+4d | game: spd %4d $A6 %02X $AC %02X $10 %04X $E2 %04X pad %04X\n",
                   i, e, dh, ds, s16(r->v[c_ea]), r->v[c_a6], r->v[c_ac], r->v[c_f10], r->v[c_e2], c4);
            SYNC(i);
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
