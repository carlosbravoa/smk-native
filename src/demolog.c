/* Reader for the per-frame log of the running game that
 * tools/labs/mame/demolog.lua writes (one row per frame per kart, the
 * kart's WRAM fields by offset).  Used by tools/demoreplay.c (the accuracy
 * gate) and by the game's --replay mode. */
#include "smk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXC 64

static int16_t s16(int v) { return (int16_t)(uint16_t)v; }

bool smk_demolog_load(const char *path, int kart, smk_demolog *out)
{
    memset(out, 0, sizeof *out);
    FILE *f = fopen(path, "r");
    if (!f) return false;
    static char line[4096];
    if (!fgets(line, sizeof line, f)) { fclose(f); return false; }
    line[strcspn(line, "\r\n")] = 0;
    char *cols[MAXC];
    int ncol = 0;
    for (char *t = strtok(line, ","); t && ncol < MAXC; t = strtok(NULL, ","))
        cols[ncol++] = t;
    int idx[MAXC];
    const char *want[] = { "kart", "fC4", "f18", "f16", "f1C", "f1A", "fA4", "fA2", "f2A",
                           "fEA", "fE8", "f22", "f24", "fA8", "fAA", "fFA", "fB2", "fEE",
                           "fA6", "fE2", "f1F", "f26", "g124", "g30", "f12", "fAE",
                           kart == 1000 ? "gE00" : "gE02", "fAC", "f10" };
    enum { C_KART, C_C4, C_X, C_XF, C_Y, C_YF, C_A4, C_A2, C_2A, C_EA, C_E8, C_VX, C_VY,
           C_A8, C_AA, C_FA, C_B2, C_EE, C_A6, C_E2, C_Z, C_ZV, C_TRACK, C_CLASS, C_CHAR, C_AE,
           C_COINS, C_AC, C_F10, C_N };
    for (int w = 0; w < C_N; w++) {
        idx[w] = -1;
        for (int i = 0; i < ncol; i++) if (!strcmp(cols[i], want[w])) idx[w] = i;
        if (idx[w] < 0) { fclose(f); return false; }
    }
    int cap = 4096;
    out->f = calloc((size_t)cap, sizeof *out->f);
    out->kart = kart;
    int v[MAXC];
    while (fgets(line, sizeof line, f)) {
        int i = 0;
        for (char *t = strtok(line, ","); t && i < ncol; t = strtok(NULL, ","))
            v[i++] = (int)strtol(t, NULL, 10);
        if (i < ncol || v[idx[C_KART]] != kart) continue;
        if (out->n == cap) {
            cap *= 2;
            out->f = realloc(out->f, (size_t)cap * sizeof *out->f);
        }
        smk_demo_frame *r = &out->f[out->n++];
        r->c4 = (uint16_t)v[idx[C_C4]];
        r->x = ((int32_t)v[idx[C_X]] << 16) | (v[idx[C_XF]] & 0xFFFF);
        r->y = ((int32_t)v[idx[C_Y]] << 16) | (v[idx[C_YF]] & 0xFFFF);
        r->a4 = (uint16_t)v[idx[C_A4]]; r->a2 = (uint16_t)v[idx[C_A2]]; r->pose = (uint16_t)v[idx[C_2A]];
        r->speed = s16(v[idx[C_EA]]); r->frac = (uint16_t)v[idx[C_E8]];
        r->vx = s16(v[idx[C_VX]]); r->vy = s16(v[idx[C_VY]]);
        r->vlag = s16(v[idx[C_A8]]); r->plag = s16(v[idx[C_AA]]); r->spin = s16(v[idx[C_FA]]);
        r->turn = s16(v[idx[C_B2]]); r->accel = s16(v[idx[C_EE]]);
        r->state = v[idx[C_A6]]; r->drive = v[idx[C_AC]]; r->flags = (uint16_t)v[idx[C_E2]];
        r->flags10 = (uint16_t)v[idx[C_F10]];
        r->z = v[idx[C_Z]]; r->zvel = s16(v[idx[C_ZV]]);
        r->coins = v[idx[C_COINS]];
        r->surf = (uint8_t)v[idx[C_AE]];
        if (out->n == 1) {
            out->track = v[idx[C_TRACK]];
            out->engine_class = v[idx[C_CLASS]] / 2;
            out->character = v[idx[C_CHAR]] / 2;
        }
    }
    fclose(f);
    if (out->n < 10) return false;
    /* the first moving frame: the game holds the grid until the lights */
    out->start = 0;
    while (out->start < out->n - 1 && out->f[out->start + 1].speed == 0) out->start++;
    return true;
}

void smk_demolog_free(smk_demolog *d)
{
    free(d->f);
    d->f = NULL;
    d->n = 0;
}

void smk_demolog_sync(const smk_demolog *d, int i, smk_player *p, smk_kart *k)
{
    const smk_demo_frame *r = &d->f[i];
    k->x = r->x; k->y = r->y;
    k->vx = r->vx; k->vy = r->vy;
    k->speed = r->speed; k->speed_frac = r->frac;
    k->z = (int32_t)r->z << 8; k->zvel = r->zvel;
    k->airborne = (r->flags & 0x8000) != 0;
    k->bounce_cool = 0; k->bvx = k->bvy = 0;
    smk_player_reset(p, r->a4);
    p->vlag = r->vlag; p->plag = r->plag; p->spin = r->spin; p->turn = r->turn;
    p->state = r->state;
    p->flags = (uint16_t)(r->flags & 0x802C);
    p->pad = r->c4;
    p->vel_angle = (uint16_t)(p->heading + p->vlag);
    p->pose = (uint16_t)(p->heading - p->plag);
    p->coins = r->coins;
    p->accel32 = (int32_t)r->accel << 16;
    k->angle = p->heading;
}

void smk_demolog_pad(const smk_demo_frame *r, uint16_t *held, uint16_t *pressed)
{
    *held = (uint16_t)(r->c4 & 0xFFF0);
    *pressed = (uint16_t)(((r->c4 & 3) << 8) | ((r->c4 & 0xC) << 2));
}
