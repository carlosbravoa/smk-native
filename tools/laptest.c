/* The race length, end to end, on the shipped AI.
 *
 * The ROM's bookkeeping ($8089B6, $014C = $8500, NOTES 052): the progress
 * word starts at lap $7F because the grid sits BEHIND the finish line, the
 * first crossing only takes it to $80 - entering lap 1, not completing one
 * - and the five that follow reach the $8500 threshold.  So a five-lap
 * race is SIX crossings, and the lap a player sees is the crossing count.
 *
 * This drives an AI kart round each GP course and checks exactly that: the
 * first crossing comes almost immediately (a few seconds of run-up from
 * the grid, not a whole circuit), and the ones after it are a lap apart.
 * If the grid ever moves in front of the line, the first split would swallow
 * a whole lap and every time trial would be wrong by one - silently.
 */
#include "smk.h"
#include <stdio.h>
#include <string.h>

#define GP_TRACKS  20
#define MAX_FRAMES 30000
#define WANT       (SMK_RACE_LAPS + 1)     /* six crossings */

static int fails;
static void check(int cond, const char *what)
{
    if (!cond) { printf("  FAIL: %s\n", what); fails++; }
}

/* The time trial's own bookkeeping, on a made-up clock: five laps out of
 * six crossings, lap 1 carrying the run-up, the best lap spotted, and the
 * table keeping the five fastest in order. */
static void shell_checks(smk_rom *rom)
{
    smk_ui_result res;
    memset(&res, 0, sizeof res);
    int cross = 0;
    long start = 0;
    const long at[] = { 90, 2000, 3900, 5700, 7600, 9400 };
    bool over = false;
    for (int i = 0; i < 6; i++) over = smk_tt_crossing(&res, &cross, &start, at[i]);
    check(!over || true, "six crossings end the race");
    check(over, "the sixth crossing ends the race");
    check(res.laps_done == SMK_RACE_LAPS, "five laps recorded");
    check(res.lap[0] == 2000, "lap 1 is timed from the lights, not the line");
    check(res.lap[1] == 1900, "lap 2 is crossing to crossing");
    check(res.lap[4] == 1800, "lap 5");
    check(res.best_lap == 1800, "the best lap is the fastest split");
    check(res.total == 9400, "the total is the last crossing");

    /* one crossing alone must not produce a split */
    memset(&res, 0, sizeof res); cross = 0; start = 0;
    smk_tt_crossing(&res, &cross, &start, 90);
    check(res.laps_done == 0, "the grid crossing is not a lap");

    smk_records recs;
    smk_records_clear(&recs);
    const long t[] = { 5000, 4000, 6000, 3000, 4500, 3500 };
    for (int i = 0; i < 6; i++) smk_records_add(&recs, 3, t[i], i % SMK_CHARACTERS);
    check(recs.best[3][0].frames == 3000, "records: fastest first");
    check(recs.best[3][1].frames == 3500, "records: second");
    check(recs.best[3][4].frames == 5000, "records: fifth");
    check(smk_records_add(&recs, 3, 9999, 0) == -1, "records: slow time rejected");
    check(smk_records_add(&recs, 3, 100, 0) == 0, "records: a new best takes slot 1");

    char tm[16];
    smk_time_text(3690, tm, sizeof tm);
    check(!strcmp(tm, "1'01\"50"), "the clock formats as the game does");

    /* the shell walks title -> players -> mode -> driver -> course ->
     * race, and the course it lands on is the ROM's own cup table entry */
    smk_ui ui;
    smk_ui_init(&ui);
    smk_ui_input none = { 0 };
    smk_ui_input go = { 0 }; go.confirm = true;
    smk_ui_step(&ui, rom, &go);
    check(ui.screen == SMK_UI_PLAYERS, "title -> players");
    smk_ui_step(&ui, rom, &go);
    check(ui.screen == SMK_UI_MODE, "players -> mode");
    check(ui.mode_sel == SMK_UI_MODE_RACE, "the shell opens on SINGLE RACE");
    smk_ui_step(&ui, rom, &go);
    check(ui.screen == SMK_UI_PLAYER, "single race -> driver");
    smk_ui_step(&ui, rom, &go);
    check(ui.screen == SMK_UI_COURSE, "driver -> course");
    smk_ui_input right = { 0 }; right.right = true;
    smk_ui_step(&ui, rom, &right);
    smk_ui_input down = { 0 }; down.down = true;
    smk_ui_step(&ui, rom, &down);
    check(smk_ui_step(&ui, rom, &go), "course -> race");
    check(ui.track == smk_cup_track(rom, 1, 1), "the chosen track is the cup table's");
    check(ui.track == 1, "flower cup course 2 is track 1 (GHOST VALLEY 2)");
    check(!strcmp(smk_track_name(rom, ui.track), "GHOST VALLEY 2"), "and it is named so");

    /* THE SHELL'S ORDER.  How many players is asked FIRST, as the
     * original asks it, because it decides what the mode screen offers
     * (NOTES 257). */
    smk_ui_input up = { 0 }; up.up = true;
    smk_ui_init(&ui);
    ui.pads = 1;
    smk_ui_step(&ui, rom, &go);
    check(ui.screen == SMK_UI_PLAYERS, "the title asks how many players");
    check(ui.players == SMK_PLAYERS_1, "starting on one");
    smk_ui_step(&ui, rom, &down);
    check(ui.players == SMK_PLAYERS_CPU, "down reaches 1P vs CPU");
    smk_ui_step(&ui, rom, &down);
    check(ui.players == SMK_PLAYERS_2, "and then two players");
    smk_ui_step(&ui, rom, &down);
    check(ui.players == SMK_PLAYERS_1, "and wraps");

    /* A TIME TRIAL IS SOLO: with a second driver the row is not there */
    check(smk_ui_mode_rows(&ui) == SMK_UI_MODES, "one player: three modes");
    ui.players = SMK_PLAYERS_2;
    check(smk_ui_mode_rows(&ui) == SMK_UI_MODES - 1, "two: no time trial");
    ui.players = SMK_PLAYERS_CPU;
    check(smk_ui_mode_rows(&ui) == SMK_UI_MODES - 1, "nor against a CPU");
    ui.mode_sel = SMK_UI_MODE_TT;          /* left over from a solo run */
    smk_ui_step(&ui, rom, &go);
    check(ui.screen == SMK_UI_MODE, "players -> mode");
    check(ui.mode_sel != SMK_UI_MODE_TT, "and a stale time trial is dropped");

    /* with no controller the two-human row cannot be landed on */
    smk_ui_init(&ui);
    ui.pads = 0;
    smk_ui_step(&ui, rom, &go);
    smk_ui_step(&ui, rom, &down);
    check(ui.players == SMK_PLAYERS_CPU, "down reaches 1P vs CPU");
    smk_ui_step(&ui, rom, &down);
    check(ui.players == SMK_PLAYERS_1, "and steps over two players");

    /* the modes wrap, and Grand Prix ENTERS the cup (NOTES 198) */
    smk_ui_init(&ui);
    smk_ui_step(&ui, rom, &go);            /* title -> players */
    smk_ui_step(&ui, rom, &go);            /* players -> mode  */
    check(ui.screen == SMK_UI_MODE, "and the mode screen after it");
    smk_ui_step(&ui, rom, &down);          /* onto TIME TRIAL  */
    check(ui.mode_sel == SMK_UI_MODE_TT, "down reaches Time Trial");
    smk_ui_step(&ui, rom, &down);          /* wraps to GRAND PRIX */
    check(ui.mode_sel == SMK_UI_MODE_GP, "and wraps to Grand Prix");
    smk_ui_step(&ui, rom, &go);
    check(ui.screen == SMK_UI_PLAYER, "Grand Prix is entered");
    check(ui.gp, "and arms the cup");
    smk_ui_init(&ui);
    smk_ui_step(&ui, rom, &go);
    smk_ui_step(&ui, rom, &go);
    smk_ui_step(&ui, rom, &up);            /* Single Race -> Grand Prix */
    check(ui.mode_sel == SMK_UI_MODE_GP, "up reaches Grand Prix");
    smk_ui_step(&ui, rom, &up);            /* and wraps to Time Trial */
    check(ui.mode_sel == SMK_UI_MODE_TT, "up wraps to Time Trial");
    smk_ui_step(&ui, rom, &up);
    check(ui.mode_sel == SMK_UI_MODE_RACE, "and back to Single Race");
    smk_ui_step(&ui, rom, &go);
    check(ui.screen == SMK_UI_PLAYER, "a single race is entered");
    check(!ui.gp, "with the cup unarmed");

    /* the driver screen picks BOTH drivers, never the same one */
    smk_ui_init(&ui);
    ui.pads = 1;
    smk_ui_step(&ui, rom, &go);
    smk_ui_step(&ui, rom, &down);
    smk_ui_step(&ui, rom, &down);
    check(ui.players == SMK_PLAYERS_2, "two players");
    smk_ui_step(&ui, rom, &go);            /* -> mode   */
    smk_ui_step(&ui, rom, &go);            /* -> driver */
    check(ui.screen == SMK_UI_PLAYER && !ui.picking_p2, "player 1 first");
    int p1 = ui.player_sel;
    smk_ui_step(&ui, rom, &go);
    check(ui.screen == SMK_UI_PLAYER && ui.picking_p2, "then player 2");
    check(ui.player2_sel != p1, "and not the driver player 1 took");
    smk_ui_step(&ui, rom, &go);
    check(ui.screen == SMK_UI_COURSE, "and then the course");
    (void)none;
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
    shell_checks(&rom);
    int ok = 0, checked = 0;
    for (int t = 0; t < GP_TRACKS; t++) {
        static smk_track trk;
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

        /* slot 1: the first AI kart, driven by the shipped controller */
        smk_racer *r = &racers[1];
        int at[WANT];
        int seen = 0, last = r->lap;
        for (int f = 0; f < MAX_FRAMES && seen < WANT; f++) {
            smk_racer_step(r, &trk, &crs, &phys);
            if (r->lap > last) { at[seen++] = f; last = r->lap; }
            else if (r->lap < last) last = r->lap;
        }
        checked++;
        if (seen < 2) {
            printf("  track %2d: only %d crossing(s)  <-- cannot time a lap\n",
                   t, seen);
            continue;
        }
        /* the run-up must be short next to a real lap */
        int first = at[0];
        int lap1 = at[1] - at[0];
        bool grid_behind = first * 3 < lap1;
        if (grid_behind) ok++;
        printf("  track %2d: first crossing %4d f, lap %5d f, %d crossings%s\n",
               t, first, lap1, seen, grid_behind ? "" : "  <-- grid NOT behind the line");
    }
    printf("%d/%d GP courses: the grid is behind the line, "
           "so %d laps = %d crossings; %d shell check%s failed\n",
           ok, checked, SMK_RACE_LAPS, WANT, fails, fails == 1 ? "" : "s");
    smk_rom_free(&rom);
    return (ok == checked && fails == 0) ? 0 : 1;
}
