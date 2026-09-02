/* The port's half of the acceleration/grip battery.
 *
 *   smk_accelgrip rom.sfc [track] [character] [class]
 *
 * It runs exactly the batteries `tools/labs/accelgrip.py` runs on the ROM,
 * in the same order with the same columns, so the two logs diff line for
 * line.  Nothing here is a model of the ROM's answer: it is our own
 * physics, driven by the same pad words on the same surfaces.
 *
 * Pad words are the SNES ones smk_player_step takes: B $8000, Left $0200,
 * Right $0100, L $0020, R $0010, Y $4000.
 */
#include "smk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static smk_rom rom;
static smk_track trk;
static smk_course crs;
static smk_player p;
static smk_kart k;
static uint8_t surf_save[256];

static int16_t s16(int v) { return (int16_t)(uint16_t)v; }

static void park(int x, int y)
{
    k.x = (int32_t)x << SMK_POS_SHIFT;
    k.y = (int32_t)y << SMK_POS_SHIFT;
}

static void plain(int cls)
{
    for (int i = 0; i < 256; i++) trk.surface[i] = (uint8_t)cls;
}

static void restore(void) { memcpy(trk.surface, surf_save, sizeof surf_save); }

/* the machine at rest, at a chosen speed, pointing north */
static void rest(int speed)
{
    smk_player_reset(&p, 0);
    memset(&k, 0, sizeof k);
    k.angle = 0;
    k.speed = (int16_t)speed;
    p.state = 0; p.drive = 0; p.vlag = 0; p.plag = 0; p.spin = 0; p.turn = 0;
    p.accel32 = 0;
    park(512, 512);
}

/* one frame with a held pad word; `pressed` is the edge set (we hold, so
 * only the first frame of a button counts) */
static uint16_t prev_pad;
static void frame(uint16_t held)
{
    uint16_t pressed = (uint16_t)(held & ~prev_pad);
    prev_pad = held;
    smk_player_step(&p, &k, &trk, held, pressed);
}

/* --surfcheck CSV: is OUR surface map the game's?  For every frame of a
 * logged drive, read our map at the position the GAME had (not ours, so
 * trajectory drift cannot contaminate it) and compare with the class byte
 * the game itself read that frame ($AE).  A map error and a physics error
 * look identical from the driver's seat; this separates them. */
static int surfcheck(const char *rom_path, const char *csv, bool gate)
{
    char err[256];
    if (!smk_rom_load(&rom, rom_path, err, sizeof err)) { fprintf(stderr, "%s\n", err); return 2; }
    static smk_demolog log;
    if (!smk_demolog_load(csv, 1000, &log)) { fprintf(stderr, "cannot read %s\n", csv); return 2; }
    if (!smk_track_load(&rom, log.track, -1, &trk, err, sizeof err)) { fprintf(stderr, "%s\n", err); return 2; }
    /* the running game's tilemap has the object stamps blitted into it
     * ($84F1A4), so compare against a map that has them too */
    smk_track_place_objects(&rom, &trk);
    int n = 0, bad = 0;
    int pairs[256][256];
    memset(pairs, 0, sizeof pairs);
    for (int i = log.start; i < log.n; i++) {
        int gx = smk_kart_px(log.f[i].x) & 1023, gy = smk_kart_px(log.f[i].y) & 1023;
        uint8_t ours = smk_track_surface(&trk, gx, gy), theirs = log.f[i].surf;
        /* $14 item box and $1A coin are STAMPED and removed as they are
         * taken, so a fresh track disagrees with a race in progress about
         * them by design; they carry no surface type ($80B3B7 only reads
         * $40 and up).  The gate is about the DRIVING surface. */
        if (gate && (theirs == 0x14 || theirs == 0x16 || theirs == 0x1A
                     || ours == 0x14 || ours == 0x16 || ours == 0x1A)) continue;
        n++;
        if (ours != theirs) {
            bad++;
            if (pairs[theirs][ours]++ < 4)
                printf("   f%-5d (%4d,%4d) cell %3d,%3d tile $%02X: game $%02X ours $%02X\n",
                       i, gx, gy, gx >> 3, gy >> 3, trk.map[(gy >> 3) * 128 + (gx >> 3)],
                       theirs, ours);
        }
    }
    printf("surface map vs the game's own $AE, track %d, %d frames: %d differ (%.2f%%)%s\n",
           log.track, n, bad, 100.0 * bad / n,
           gate ? (bad ? "  FAIL" : "  PASS") : "");
    for (int a = 0; a < 256; a++) for (int b = 0; b < 256; b++) if (pairs[a][b])
        printf("   game $%02X -> ours $%02X on %d frames\n", a, b, pairs[a][b]);
    return bad ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc > 2 && !strcmp(argv[1], "--surfcheck")) {
        bool gate = false;
        for (int i = 2; i < argc; i++) if (!strcmp(argv[i], "--gate")) gate = true;
        return surfcheck(argc > 3 && argv[3][0] != '-' ? argv[3] : "rom/smk_usa.sfc",
                         argv[2], gate);
    }
    const char *rom_path = argc > 1 ? argv[1] : "rom/smk_usa.sfc";
    int track = argc > 2 ? atoi(argv[2]) : 7;
    int character = argc > 3 ? atoi(argv[3]) : 0;
    int cls = argc > 4 ? atoi(argv[4]) : 1;
    int coins = argc > 5 ? atoi(argv[5]) : 0;
    char err[256];
    if (!smk_rom_load(&rom, rom_path, err, sizeof err)) { fprintf(stderr, "%s\n", err); return 2; }
    if (!smk_track_load(&rom, track, -1, &trk, err, sizeof err)) { fprintf(stderr, "%s\n", err); return 2; }
    if (!smk_course_load(&rom, track, &crs)) { fprintf(stderr, "course\n"); return 2; }
    if (!smk_player_setup(&rom, character, cls, &p)) { fprintf(stderr, "setup\n"); return 2; }
    memcpy(surf_save, trk.surface, sizeof surf_save);
    p.coins = coins;

    printf("character %d   base top $B4 %d   coins %d   class %d\n",
           character, p.base_top, coins, cls);

    /* ---------------------------------------------------------- A */
    printf("\n=== A. acceleration from a standstill, straight, plain road ($40) ===\n");
    plain(0x40);
    rest(0);
    prev_pad = 0;
    static int curve[700];
    for (int f = 0; f < 700; f++) { park(512, 512); frame(0x8000); curve[f] = k.speed; }
    int top = 0;
    for (int f = 0; f < 700; f++) if (curve[f] > top) top = curve[f];
    printf("  top reached %d ($B4 %d, $D6 %d)\n", top, p.base_top, p.target);
    const double fr[] = { .5, .75, .9, .95, .99, 1.0 };
    for (int i = 0; i < 6; i++) {
        double want = top * fr[i];
        int at = -1;
        for (int f = 0; f < 700; f++) if (curve[f] >= want) { at = f; break; }
        printf("  %5.0f%% of top (%4.0f): frame %d\n", fr[i] * 100, want, at);
    }
    printf("  per-frame gain by speed band:\n");
    {
        double sum[32] = { 0 }; int cnt[32] = { 0 }, prev = 0;
        for (int f = 0; f < 700; f++) {
            int b = prev >> 6; if (b > 31) b = 31;
            sum[b] += curve[f] - prev; cnt[b]++; prev = curve[f];
        }
        for (int b = 0; b < 32; b++) if (cnt[b])
            printf("    speed %4d-%4d: gain %5.2f/frame over %3d frames\n",
                   b * 64, b * 64 + 63, sum[b] / cnt[b], cnt[b]);
    }
    printf("  trace:");
    { const int at[] = {0,10,30,60,120,180,240,300,400,500,699};
      for (int i = 0; i < 11; i++) printf(" %d:%d", at[i], curve[at[i]]); printf("\n"); }

    /* ---------------------------------------------------------- A0 */
    printf("\n=== A0. the first 24 frames of a standing start ===\n");
    rest(0); prev_pad = 0;
    printf("start: $AC %04X  $E2 %04X  $B0 %04X  $D6 %d  $B4 %d\n",
           p.drive, p.flags, p.type, p.target, p.base_top);
    printf("   f  $EA(speed)  $E8(frac)  $EE(accel)  $EC(accel frac)  $AC  $A6\n");
    for (int f = 0; f < 25; f++) {
        park(512, 512); frame(0x8000);
        printf("  %2d  %9d  %9d  %10d  %14d  %3X  %3X\n",
               f, k.speed, k.speed_frac, k.accel, k.accel_frac, p.drive, p.state);
    }

    /* ---------------------------------------------------------- A2 */
    printf("\n=== A2. recovery: how long to climb back to the top from a loss ===\n");
    { const int lost[] = { 100, 200, 300, 400, 600 };
      for (int i = 0; i < 5; i++) {
        rest(top - lost[i]); prev_pad = 0;
        int n = -1;
        for (int f = 0; f < 900; f++) {
            park(512, 512); frame(0x8000);
            if (k.speed >= top) { n = f; break; }
        }
        printf("  from %4d (top-%3d): back to %d in %d frames (%.2f s)\n",
               top - lost[i], lost[i], top, n, n / 60.0);
      } }

    /* ---------------------------------------------------------- B */
    printf("\n=== B. hold the top speed while STEERING (full lock, plain road) ===\n");
    { struct { const char *name; uint16_t pad; } cases[] = {
        { "B held + LEFT held, 400 frames", 0x8200 },
        { "B + LEFT + shoulder R held (power slide row)", 0x8210 },
        { "B held, steering RELEASED (control)", 0x8000 },
      };
      for (unsigned c = 0; c < 3; c++) {
        rest(top); prev_pad = 0;
        static int spd[400], a6[400], a8[400], aa[400], fa[400], b2[400], ee[400];
        for (int f = 0; f < 400; f++) {
            park(512, 512); frame(cases[c].pad);
            spd[f] = k.speed; a6[f] = p.state; a8[f] = p.vlag; aa[f] = p.plag;
            fa[f] = p.spin; b2[f] = p.turn; ee[f] = k.accel;
        }
        printf("  -- %s\n", cases[c].name);
        printf("      f   spd  $A6  $A8(deg)  $AA(deg)   $FA   $B2   $EE\n");
        const int at[] = {0,1,2,5,10,20,40,60,90,120,180,240,300,399};
        for (int i = 0; i < 14; i++) { int f = at[i];
            printf("    %3d  %4d   %02X  %6d(%5.1f) %6d(%5.1f) %6d %5d %5d\n",
                   f, spd[f], a6[f], a8[f], a8[f] * 360.0 / 65536,
                   aa[f], aa[f] * 360.0 / 65536, fa[f], b2[f], ee[f]); }
        int mn = spd[0]; double sum = 0;
        unsigned seen = 0;
        for (int f = 0; f < 400; f++) { if (spd[f] < mn) mn = spd[f]; sum += spd[f]; seen |= 1u << (a6[f] & 31); }
        printf("     speed: start %d  min %d  final %d  mean %.1f   states seen",
               spd[0], mn, spd[399], sum / 400);
        for (int s = 0; s < 32; s++) if (seen >> s & 1) printf(" %02X", s);
        printf("\n");
      } }

    printf("\n=== B2. a human corner: hold left N frames, release N, repeat ===\n");
    { const int holds[] = { 10, 20, 40 };
      for (int i = 0; i < 3; i++) {
        int hold = holds[i];
        rest(top); prev_pad = 0;
        int mn = 1 << 20, last = 0, attop = 0; double sum = 0;
        for (int f = 0; f < 360; f++) {
            park(512, 512);
            frame((uint16_t)(0x8000 | (((f / hold) % 2 == 0) ? 0x0200 : 0)));
            if (k.speed < mn) mn = k.speed;
            sum += k.speed; last = k.speed;
            if (k.speed >= top) attop++;
        }
        printf("  hold/release %2d: min %4d  final %4d  mean %6.1f  frames at top %d/%d\n",
               hold, mn, last, sum / 360, attop, 360);
      } }

    /* ---------------------------------------------------------- G
     * "Grip" as a driver means two numbers: how tight it turns, and how
     * far the velocity lags where it points.  Both are functions of SPEED
     * here, so sweep speed with everything else held still.  The twin is
     * tools/labs/accelgrip_g.py. */
    printf("\n=== G. full-lock turn at each speed (120 frames, B + Left) ===\n");
    plain(0x40);
    printf("   speed  slide  turn/f(deg)  radius(px)  lag $A8 (deg)  spinout f  end spd\n");
    { int sw[] = { 200, 300, 400, 500, 600, 700, 784, 850, 900, 912, 940, p.target };
      for (unsigned i = 0; i < sizeof sw / sizeof *sw; i++) {
        int s = sw[i];
        rest(s); prev_pad = 0;
        int lag = 0, spin_at = -1; double rate = 0;
        uint16_t prev_h = p.heading;
        int rates[120];
        for (int f = 0; f < 120; f++) {
            park(512, 512); frame(0x8200);
            rates[f] = s16((int)p.heading - (int)prev_h);
            prev_h = p.heading;
            if (abs(p.vlag) > lag) lag = abs(p.vlag);
            if ((p.state == 0x0E || p.state == 0x10) && spin_at < 0) spin_at = f;
        }
        for (int f = 100; f < 120; f++) rate += rates[f];
        rate /= 20.0;
        double rad = rate ? fabs((s / 256.0) / (rate * 2 * M_PI / 65536.0)) : 0;
        printf("   %5d   %s   %8.2f   %9.1f   %6d(%5.1f)      %6d   %5d\n",
               s, lag ? "yes" : " no ", rate * 360.0 / 65536, rad,
               lag, lag * 360.0 / 65536, spin_at, k.speed);
      } }
    restore();

    /* ---------------------------------------------------------- E
     * The ceiling is asymptotic, so how hard it is to SIT on depends on
     * where it is - which is class and coins.  Sweep both. */
    printf("\n=== E. the ceiling by class and coins (plain road, throttle held) ===\n");
    printf("  class  coins   $D6   frames 0->99%%   frames 0->top   last-64 gain/f   recover top-100\n");
    plain(0x40);
    for (int ec = 0; ec < 3; ec++) {
        for (int co = 0; co <= 10; co += 5) {
            if (!smk_player_setup(&rom, character, ec, &p)) return 2;
            p.coins = co;
            rest(0); prev_pad = 0;
            int cv[900], t = 0;
            for (int f = 0; f < 900; f++) { park(512, 512); frame(0x8000); cv[f] = k.speed; }
            for (int f = 0; f < 900; f++) if (cv[f] > t) t = cv[f];
            int f99 = -1, f100 = -1;
            for (int f = 0; f < 900; f++) { if (f99 < 0 && cv[f] >= t * 0.99) f99 = f;
                                            if (f100 < 0 && cv[f] >= t) f100 = f; }
            double g = 0; int gc = 0;
            for (int f = 1; f < 900; f++) if (cv[f-1] >= t - 64 && cv[f-1] < t) { g += cv[f] - cv[f-1]; gc++; }
            rest(t - 100); prev_pad = 0;
            int rec = -1;
            for (int f = 0; f < 1200; f++) { park(512, 512); frame(0x8000);
                                             if (k.speed >= t) { rec = f; break; } }
            printf("  %3dcc  %5d  %4d   %6d (%4.1f s)  %6d (%4.1f s)  %11.3f   %5d f (%4.1f s)\n",
                   ec == 0 ? 50 : ec == 1 ? 100 : 150, co, p.target, f99, f99 / 60.0,
                   f100, f100 / 60.0, gc ? g / gc : 0, rec, rec / 60.0);
        }
    }
    /* put the tool's own player back */
    if (!smk_player_setup(&rom, character, cls, &p)) return 2;
    p.coins = coins;
    restore();

    /* ---------------------------------------------------------- C */
    printf("\n=== C. the real track: hold the throttle and steer down the flow field ===\n");
    restore();
    rest(0);
    prev_pad = 0;
    park(952, 756);
    static int spd[1800]; static uint8_t sc[1800]; static int a6s[1800], a8s[1800];
    for (int f = 0; f < 1800; f++) {
        int px = smk_kart_px(k.x) & 1023, py = smk_kart_px(k.y) & 1023;
        uint16_t want = (uint16_t)(crs.flow[((py >> 4) & 63) * 64 + ((px >> 4) & 63)] << 8);
        int d = s16((int)want - (int)p.heading);
        uint16_t pad = 0x8000;
        if (d < -0x300) pad |= 0x0200;
        else if (d > 0x300) pad |= 0x0100;
        frame(pad);
        px = smk_kart_px(k.x) & 1023; py = smk_kart_px(k.y) & 1023;
        spd[f] = k.speed;
        sc[f] = smk_track_surface(&trk, px, py);
        a6s[f] = p.state; a8s[f] = p.vlag;
    }
    { static int sorted[1800];
      memcpy(sorted, spd, sizeof sorted);
      for (int i = 1; i < 1800; i++) { int v = sorted[i], j = i - 1;
          while (j >= 0 && sorted[j] > v) { sorted[j + 1] = sorted[j]; j--; } sorted[j + 1] = v; }
      int tgt = p.target;
      printf("  frames %d   target $D6 %d\n", 1800, tgt);
      printf("  speed percentiles: p10 %d  p25 %d  p50 %d  p75 %d  p90 %d  p99 %d  max %d\n",
             sorted[180], sorted[450], sorted[900], sorted[1350], sorted[1620], sorted[1782], sorted[1799]);
      int atop = 0, near = 0;
      for (int f = 0; f < 1800; f++) { if (spd[f] >= tgt) atop++; if (spd[f] >= tgt * 0.95) near++; }
      printf("  frames at/above the target: %d (%.1f%%)   above 95%% of it: %d (%.1f%%)\n",
             atop, 100.0 * atop / 1800, near, 100.0 * near / 1800);
      int hist[256] = { 0 };
      for (int f = 0; f < 1800; f++) hist[sc[f]]++;
      printf("  surface class under the kart:");
      for (int i = 0; i < 256; i++) if (hist[i]) printf(" $%02X:%d", i, hist[i]);
      printf("\n");
      int sh[64] = { 0 };
      for (int f = 0; f < 1800; f++) sh[a6s[f] & 63]++;
      printf("  slide state $A6:");
      for (int i = 0; i < 64; i++) if (sh[i]) printf(" %02X:%d", i, sh[i]);
      printf("\n");
      int nz = 0, big = 0;
      for (int f = 0; f < 1800; f++) { if (a8s[f]) nz++; if (abs(a8s[f]) > 0x200) big++; }
      printf("  |$A8| over 0 on %d frames, over $200 on %d\n", nz, big);
    }
    return 0;
}
