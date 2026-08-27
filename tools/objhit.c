/* Drive the player's kart into a track object and log what happens.
 *
 * The user's report: hit a Thwomp on Rainbow Road, got stuck, and could
 * not do what the real game lets you do - shove free by hitting it a few
 * times while steering to one side.  This is the repro: put the kart a
 * known distance from a known object, hold the throttle (and optionally a
 * direction), and print the state frame by frame.
 */
#include "smk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int main(int argc, char **argv)
{
    const char *rom_path = "rom/smk_usa.sfc";
    int track = argc > 1 ? atoi(argv[1]) : 5;
    int ent   = argc > 2 ? atoi(argv[2]) : 12;
    int steer = argc > 3 ? atoi(argv[3]) : 0;   /* -1 left, +1 right, 0 none */
    int away  = argc > 4 ? atoi(argv[4]) : 60;  /* start this far off       */
    int frames= argc > 5 ? atoi(argv[5]) : 240;
    /* which way it drives, in ROM angle units: 0 = north, $4000 = east */
    int head  = argc > 6 ? (int)strtol(argv[6], NULL, 0) : 0x4000;

    smk_rom rom; char err[256];
    if (!smk_rom_load(&rom, rom_path, err, sizeof err)) { printf("%s\n", err); return 1; }
    static smk_track trk; static smk_course crs; static smk_physics phys;
    if (!smk_track_load(&rom, track, -1, &trk, err, sizeof err)) return 1;
    if (!smk_course_load(&rom, track, &crs)) return 1;
    if (!smk_physics_load(&rom, 0, &phys)) return 1;
    smk_track_place_objects(&rom, &trk);
    course_for_step = &crs;

    if (ent >= crs.nent) { printf("track %d has %d entities\n", track, crs.nent); return 1; }
    int ox = crs.ent[ent].x, oy = crs.ent[ent].y;

    smk_player p;
    if (!smk_player_setup(&rom, 0, 0, &p)) return 1;
    smk_kart k;
    memset(&k, 0, sizeof k);
    /* start `away` px back along the heading, so it drives straight at it */
    double a = (double)(head & 0xFFFF) * (2.0 * M_PI / 65536.0);
    k.x = (int32_t)lrint((ox - sin(a) * away) * 65536.0);
    k.y = (int32_t)lrint((oy + cos(a) * away) * 65536.0);
    k.angle = (uint16_t)head;
    smk_player_reset(&p, k.angle);

    printf("track %d, object %d at (%d,%d), start (%d,%d) %d px back, "
           "heading $%04X, steer %d\n",
           track, ent, ox, oy, smk_kart_px(k.x), smk_kart_px(k.y), away,
           head & 0xFFFF, steer);
    { uint8_t sv = smk_track_surface(&trk, smk_kart_px(k.x), smk_kart_px(k.y));
      printf("start surface $%02X (%s)\n", sv,
             smk_surface_solid(sv) ? "SOLID/void" : "driveable"); }
    printf("frame     x     y  dist  speed    vx    vy  bcool drive state"
           "  vlag  clag  vang  head\n");
    for (int f = 0; f < frames; f++) {
        uint16_t held = 0x8000;                       /* B: throttle */
        if (steer < 0) held |= 0x0200;
        if (steer > 0) held |= 0x0100;
        smk_player_step(&p, &k, &trk, held, 0);
        if (!p.hazard) smk_collide_objects(&k, &crs);
        int kx = smk_kart_px(k.x), ky = smk_kart_px(k.y);
        double d = hypot((double)(kx - ox), (double)(ky - oy));
        if (f < 12 || f % 10 == 0 || (d < 12.0))
            printf("%5d %5d %5d %5.1f %6d %5d %5d %6d %5X %5X %5d %5d  %04X  %04X\n",
                   f, kx, ky, d, k.speed, k.vx, k.vy, k.bounce_cool,
                   p.drive, p.state, p.vlag, k.crash_lag, p.vel_angle, p.heading);
    }
    return 0;
}
