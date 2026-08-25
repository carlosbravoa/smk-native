/* Our acceleration curve, printed the same way the ROM battery prints
 * its own, so the two can be compared directly.
 *
 * ROM, measured (NOTES 088), every 10th frame from a standstill:
 *   1 21 41 61 113 159 203 295 415 507 583 623 653 673 693 711 ... 963 top
 */
#include "smk.h"
#include <stdio.h>

int main(int argc, char **argv)
{
    smk_rom rom; char err[128];
    const char *path = argc > 1 ? argv[1] : "rom/smk_usa.sfc";
    if (!smk_rom_load(&rom, path, err, sizeof err)) { puts(err); return 77; }
    for (int cls = 0; cls < 3; cls++) {
        smk_physics phys;
        if (!smk_physics_load(&rom, cls, &phys)) continue;
        int top = (int16_t)phys.w[SMK_PHYS_TARGET + 6];   /* FEEL_TARGET_IDX */
        smk_kart k = {0};
        printf("class %d (top %4d): ", cls, top);
        for (int f = 0; f < 400; f++) {
            int32_t a = (int32_t)smk_physics_accel(&phys, k.speed) << 8;
            if (top > 0) {                       /* the measured taper */
                float head = (float)(top - k.speed) / (float)top * 1.6f;
                if (head < 0.0f) head = 0.0f;
                if (head < 1.0f) a = (int32_t)((float)a * head);
            }
            k.accel = (int16_t)(a >> 16);
            k.accel_frac = (uint16_t)(a & 0xFFFF);
            smk_kart_accelerate(&k);
            if (k.speed > top) k.speed = (int16_t)top;
            if (f % 10 == 0 && f < 160) printf("%d ", k.speed);
        }
        printf("| after 400f: %d\n", k.speed);
    }
    smk_rom_free(&rom);
    return 0;
}
