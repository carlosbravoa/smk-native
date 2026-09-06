/* Course packages and the registry (docs/TRACKS.md sections 2 and 3).
 *
 * A package is a directory: course.txt (key value lines), map.bin (the
 * 128x128 tile indices before stamping), sectors.bin (the 64x64 sector
 * map, $7F unpainted, no finish bit), line.txt (one waypoint per line)
 * and optionally style/ (an author's own tiles, palette and classes).
 * No ROM bytes: the indices are the author's arrangement of the tiles
 * that stay in the user's ROM.
 *
 * The registry numbers courses: 0..23 are the ROM's slots, 24 and up the
 * packages found at startup, so every `int track` in the port keeps
 * working and the two loaders dispatch on the index.
 *
 * OURS, all of it - the format and the numbering.  Every FIELD in a
 * package is a ROM structure, and the selftest round-trips the twenty
 * GP courses through it byte for byte.
 */
#include "smk.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ---- files ------------------------------------------------------------ */

static bool read_file(const char *path, void *buf, size_t want, char *err, size_t errsz)
{
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, errsz, "%s: missing", path); return false; }
    size_t n = fread(buf, 1, want, f);
    int extra = fgetc(f);
    fclose(f);
    if (n != want || extra != EOF) {
        snprintf(err, errsz, "%s: expected %zu bytes", path, want);
        return false;
    }
    return true;
}

static bool write_file(const char *path, const void *buf, size_t n, char *err, size_t errsz)
{
    FILE *f = fopen(path, "wb");
    if (!f) { snprintf(err, errsz, "%s: cannot write", path); return false; }
    bool ok = fwrite(buf, 1, n, f) == n;
    fclose(f);
    if (!ok) snprintf(err, errsz, "%s: short write", path);
    return ok;
}

static void mkdirs(const char *dir)
{
    char path[1024];
    snprintf(path, sizeof path, "%s", dir);
    for (char *p = path + 1; *p; p++) {
        if (*p != '/') continue;
        *p = 0; mkdir(path, 0755); *p = '/';
    }
    mkdir(path, 0755);
}

static void slug_of_dir(const char *dir, char *out, size_t n)
{
    /* the last path component, trailing slashes ignored */
    size_t len = strlen(dir);
    while (len > 1 && dir[len - 1] == '/') len--;
    size_t start = len;
    while (start > 0 && dir[start - 1] != '/') start--;
    size_t k = len - start;
    if (k >= n) k = n - 1;
    memcpy(out, dir + start, k);
    out[k] = 0;
}

/* ---- the reader ------------------------------------------------------- */

bool smk_src_from_pkg(const char *dir, smk_course_src *out, char *err, size_t errsz)
{
    char path[1024];
    memset(out, 0, sizeof *out);
    out->rom_track = -1;
    out->theme = -1;
    out->item_block = 1;
    /* the ROM's own spawn-offset table, so a package's segments behave
     * as a slot's do (0, 8, 16, 24, then back to the first window) */
    static const uint16_t SEG_OFF[8] = { 0, 8, 16, 24, 0, 0, 0, 0 };
    memcpy(out->seg_off, SEG_OFF, sizeof out->seg_off);
    slug_of_dir(dir, out->id, sizeof out->id);
    snprintf(out->name, sizeof out->name, "%s", out->id);
    for (char *p = out->name; *p; p++)
        if (*p >= 'a' && *p <= 'z') *p -= 'a' - 'A';

    snprintf(path, sizeof path, "%s/course.txt", dir);
    FILE *f = fopen(path, "r");
    if (!f) { snprintf(err, errsz, "%s: missing", path); return false; }
    char line[256];
    int lineno = 0, format = 0;
    bool have_grid = false, have_fin = false, have_seg = false;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *h = strchr(line, '#');
        if (h) *h = 0;
        char key[32];
        int at = 0;
        if (sscanf(line, "%31s%n", key, &at) != 1) continue;
        const char *v = line + at;
        while (*v == ' ' || *v == '\t') v++;
        char *e = (char *)v + strlen(v);
        while (e > v && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ')) *--e = 0;
        int a, b, c, d;
        if (!strcmp(key, "format")) {
            format = atoi(v);
        } else if (!strcmp(key, "name")) {
            snprintf(out->name, sizeof out->name, "%s", v);
        } else if (!strcmp(key, "theme")) {
            out->theme = atoi(v);
        } else if (!strcmp(key, "music")) {
            snprintf(out->music, sizeof out->music, "%s", v);
        } else if (!strcmp(key, "items")) {
            out->item_block = atoi(v);
        } else if (!strcmp(key, "lapword")) {
            out->lap_word = (uint16_t)strtol(v, NULL, 0);
        } else if (!strcmp(key, "grid")) {
            if (sscanf(v, "%d %d %d", &a, &b, &c) != 3) goto bad;
            out->grid_x = (int16_t)a; out->grid_y = (int16_t)b; out->grid_step = (int16_t)c;
            have_grid = true;
        } else if (!strcmp(key, "finish")) {
            if (sscanf(v, "%d %d %d %d", &a, &b, &c, &d) != 4) goto bad;
            if (a < 0 || a > 63 || b < 0 || b > 63 || c < 1 || d < 1) goto bad;
            out->fin_cell = (uint16_t)(b * 64 + a);
            out->fin_w = (uint8_t)c; out->fin_h = (uint8_t)d;
            have_fin = true;
        } else if (!strcmp(key, "object")) {
            char kind[32];
            if (sscanf(v, "%31s %d %d", kind, &a, &b) != 3) goto bad;
            if (a < 0 || a > 127 || b < 0 || b > 127) goto bad;
            if (out->nstamp >= SMK_STAMPS_MAX) { snprintf(err, errsz, "%s:%d: more than %d objects", path, lineno, SMK_STAMPS_MAX); fclose(f); return false; }
            out->stamp[out->nstamp].kind = (uint8_t)strtol(kind, NULL, 0);
            out->stamp[out->nstamp].cell = (uint16_t)(b * 128 + a);
            out->nstamp++;
        } else if (!strcmp(key, "entity")) {
            int k = 0;
            int n = sscanf(v, "%d %d %d", &a, &b, &k);
            if (n < 2 || a < 0 || a > 127 || b < 0 || b > 127 || k < 0 || k > 3) goto bad;
            if (out->nent >= SMK_SRC_ENTS) { snprintf(err, errsz, "%s:%d: more than %d entities", path, lineno, SMK_SRC_ENTS); fclose(f); return false; }
            out->ent[out->nent++] = (uint16_t)((k << 14) | (b << 7) | a);
        } else if (!strcmp(key, "segment")) {
            if (sscanf(v, "%d", &a) != 1 || a < 0 || a > 254) goto bad;
            if (out->nseg >= SMK_SRC_SEGS - 1) { snprintf(err, errsz, "%s:%d: more than %d segments", path, lineno, SMK_SRC_SEGS - 1); fclose(f); return false; }
            out->seg_thresh[out->nseg++] = (uint8_t)a;
            have_seg = true;
        } else if (!strcmp(key, "segoff")) {
            int vv[8];
            if (sscanf(v, "%d %d %d %d %d %d %d %d", &vv[0], &vv[1], &vv[2], &vv[3], &vv[4], &vv[5], &vv[6], &vv[7]) != 8) goto bad;
            for (int i = 0; i < 8; i++) out->seg_off[i] = (uint16_t)vv[i];
        } else if (!strcmp(key, "style")) {
            out->has_style = true;
        } else {
            snprintf(err, errsz, "%s:%d: unknown key '%s'", path, lineno, key);
            fclose(f); return false;
        }
        continue;
    bad:
        snprintf(err, errsz, "%s:%d: bad '%s' line", path, lineno, key);
        fclose(f); return false;
    }
    fclose(f);
    if (format != 1) { snprintf(err, errsz, "%s: format %d is not 1", path, format); return false; }
    if (out->theme < 0 || out->theme >= SMK_THEME_COUNT) { snprintf(err, errsz, "%s: theme must be 0..7", path); return false; }
    if (!have_grid) { snprintf(err, errsz, "%s: no grid line", path); return false; }
    if (!have_fin)  { snprintf(err, errsz, "%s: no finish line", path); return false; }
    if (out->item_block < 0 || out->item_block > 7) { snprintf(err, errsz, "%s: items must be 0..7", path); return false; }
    /* the threshold list's terminator; no `segment` at all means one
     * window for the whole lap (still a list: entities need one) */
    if (out->nent > 0 || have_seg) out->seg_thresh[out->nseg++] = 0xFF;

    snprintf(path, sizeof path, "%s/map.bin", dir);
    if (!read_file(path, out->map, SMK_MAP_BYTES, err, errsz)) return false;
    snprintf(path, sizeof path, "%s/sectors.bin", dir);
    if (!read_file(path, out->sect, SMK_SECT_CELLS, err, errsz)) return false;

    snprintf(path, sizeof path, "%s/line.txt", dir);
    f = fopen(path, "r");
    if (!f) { snprintf(err, errsz, "%s: missing", path); return false; }
    lineno = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *h = strchr(line, '#');
        if (h) *h = 0;
        int x, y, attr = 0;
        int n = sscanf(line, "%d %d %i", &x, &y, &attr);
        if (n < 2) continue;
        if (x < 0 || x > 1016 || y < 0 || y > 1016 || (x & 7) || (y & 7) || attr < 0 || attr > 255) {
            snprintf(err, errsz, "%s:%d: a waypoint is x y attr, x and y multiples of 8", path, lineno);
            fclose(f); return false;
        }
        if (out->sectors >= SMK_MAX_SECTORS - 1) { snprintf(err, errsz, "%s: more than %d waypoints", path, SMK_MAX_SECTORS - 1); fclose(f); return false; }
        out->wp[out->sectors][0] = (uint8_t)(x / 8);
        out->wp[out->sectors][1] = (uint8_t)(y / 8);
        out->wp[out->sectors][2] = (uint8_t)attr;
        out->sectors++;
    }
    fclose(f);
    if (out->sectors == 0) { snprintf(err, errsz, "%s: no waypoints", path); return false; }
    /* every painted cell must name a sector that has a waypoint */
    for (int i = 0; i < SMK_SECT_CELLS; i++)
        if (out->sect[i] != SMK_SECT_OFF && (out->sect[i] & 0x7F) >= out->sectors) {
            snprintf(err, errsz, "%s/sectors.bin: cell %d names sector %d, the line has %d",
                     dir, i, out->sect[i] & 0x7F, out->sectors);
            return false;
        }

    if (out->has_style) {
        snprintf(path, sizeof path, "%s/style/tiles.bin", dir);
        if (!read_file(path, out->style_tiles, sizeof out->style_tiles, err, errsz)) return false;
        snprintf(path, sizeof path, "%s/style/palette.bin", dir);
        if (!read_file(path, out->style_palette, sizeof out->style_palette, err, errsz)) return false;
        snprintf(path, sizeof path, "%s/style/surface.bin", dir);
        if (!read_file(path, out->style_surface, sizeof out->style_surface, err, errsz)) return false;
    }
    return true;
}

/* ---- the writer ------------------------------------------------------- */

bool smk_src_write_pkg(const smk_course_src *src, const char *dir, char *err, size_t errsz)
{
    char path[1024];
    mkdirs(dir);
    snprintf(path, sizeof path, "%s/course.txt", dir);
    FILE *f = fopen(path, "w");
    if (!f) { snprintf(err, errsz, "%s: cannot write", path); return false; }
    fprintf(f, "# smk-port course package (docs/TRACKS.md)\n");
    fprintf(f, "format   1\n");
    fprintf(f, "name     %s\n", src->name);
    fprintf(f, "theme    %d\n", src->theme);
    if (src->music[0]) fprintf(f, "music    %s\n", src->music);
    fprintf(f, "items    %d\n", src->item_block);
    if (src->lap_word) fprintf(f, "lapword  0x%04X\n", src->lap_word);
    fprintf(f, "grid     %d %d %d\n", src->grid_x, src->grid_y, src->grid_step);
    fprintf(f, "finish   %d %d %d %d\n", src->fin_cell % 64, src->fin_cell / 64, src->fin_w, src->fin_h);
    if (src->has_style) fprintf(f, "style    style/\n");
    fprintf(f, "# stamps: the ROM's kind byte, then the tile column and row\n");
    for (int i = 0; i < src->nstamp; i++)
        fprintf(f, "object   0x%02X %d %d\n", src->stamp[i].kind,
                src->stamp[i].cell % 128, src->stamp[i].cell / 128);
    fprintf(f, "# sprite obstacles: tile column, row, and the word's kind bits\n");
    for (int i = 0; i < src->nent; i++)
        fprintf(f, "entity   %d %d %d\n", src->ent[i] & 0x7F, (src->ent[i] >> 7) & 0x7F, src->ent[i] >> 14);
    fprintf(f, "# entity spawn windows open at these sector indices\n");
    for (int i = 0; i < src->nseg; i++)
        if (src->seg_thresh[i] != 0xFF) fprintf(f, "segment  %d\n", src->seg_thresh[i]);
    bool std_off = true;
    static const uint16_t SEG_OFF[8] = { 0, 8, 16, 24, 0, 0, 0, 0 };
    for (int i = 0; i < 8; i++) if (src->seg_off[i] != SEG_OFF[i]) std_off = false;
    if (!std_off) {
        fprintf(f, "segoff  ");
        for (int i = 0; i < 8; i++) fprintf(f, " %d", src->seg_off[i]);
        fprintf(f, "\n");
    }
    fclose(f);

    snprintf(path, sizeof path, "%s/map.bin", dir);
    if (!write_file(path, src->map, SMK_MAP_BYTES, err, errsz)) return false;
    snprintf(path, sizeof path, "%s/sectors.bin", dir);
    if (!write_file(path, src->sect, SMK_SECT_CELLS, err, errsz)) return false;

    snprintf(path, sizeof path, "%s/line.txt", dir);
    f = fopen(path, "w");
    if (!f) { snprintf(err, errsz, "%s: cannot write", path); return false; }
    fprintf(f, "# one waypoint per sector: x y attr (pixels; attr bits 0-1 the AI speed row, bit 7 airborne-reject)\n");
    for (int i = 0; i < src->sectors; i++)
        fprintf(f, "%4d %4d %d\n", src->wp[i][0] * 8, src->wp[i][1] * 8, src->wp[i][2]);
    fclose(f);

    if (src->has_style) {
        snprintf(path, sizeof path, "%s/style", dir);
        mkdirs(path);
        snprintf(path, sizeof path, "%s/style/tiles.bin", dir);
        if (!write_file(path, src->style_tiles, sizeof src->style_tiles, err, errsz)) return false;
        snprintf(path, sizeof path, "%s/style/palette.bin", dir);
        if (!write_file(path, src->style_palette, sizeof src->style_palette, err, errsz)) return false;
        snprintf(path, sizeof path, "%s/style/surface.bin", dir);
        if (!write_file(path, src->style_surface, sizeof src->style_surface, err, errsz)) return false;
    }
    return true;
}

/* ---- the registry ----------------------------------------------------- */

typedef struct { smk_course_src *src; char dir[512]; } reg_entry;
static reg_entry reg[SMK_TRACKS_MAX - SMK_TRACK_COUNT];
static int nreg;
static char last_err[512];

int smk_tracks_total(void)  { return SMK_TRACK_COUNT + nreg; }
int smk_tracks_custom(void) { return nreg; }
const char *smk_tracks_error(void) { return last_err; }

const smk_course_src *smk_tracks_src(int track)
{
    int i = track - SMK_TRACK_COUNT;
    if (i < 0 || i >= nreg) return NULL;
    return reg[i].src;
}

const char *smk_tracks_id(int track)
{
    static char buf[32];
    if (track >= 0 && track < SMK_TRACK_COUNT) {
        snprintf(buf, sizeof buf, "rom%02d", track);
        return buf;
    }
    const smk_course_src *s = smk_tracks_src(track);
    return s ? s->id : "?";
}

int smk_tracks_add_dir(const char *dir)
{
    char real[512];
    snprintf(real, sizeof real, "%s", dir);
    size_t len = strlen(real);
    while (len > 1 && real[len - 1] == '/') real[--len] = 0;
    for (int i = 0; i < nreg; i++)
        if (!strcmp(reg[i].dir, real)) return SMK_TRACK_COUNT + i;
    if (nreg >= SMK_TRACKS_MAX - SMK_TRACK_COUNT) {
        snprintf(last_err, sizeof last_err, "%s: the registry is full (%d courses)", dir, SMK_TRACKS_MAX);
        return -1;
    }
    smk_course_src *src = calloc(1, sizeof *src);
    if (!src) return -1;
    if (!smk_src_from_pkg(real, src, last_err, sizeof last_err)) { free(src); return -1; }
    for (int i = 0; i < nreg; i++)
        if (!strcmp(reg[i].src->id, src->id)) {
            snprintf(last_err, sizeof last_err, "%s: a course called '%s' is already registered from %s",
                     dir, src->id, reg[i].dir);
            free(src);
            return -1;
        }
    reg[nreg].src = src;
    snprintf(reg[nreg].dir, sizeof reg[nreg].dir, "%s", real);
    return SMK_TRACK_COUNT + nreg++;
}

static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

int smk_tracks_scan(const char *parent)
{
    DIR *d = opendir(parent);
    if (!d) return 0;
    char *names[256];
    int n = 0;
    struct dirent *de;
    while ((de = readdir(d)) && n < 256) {
        if (de->d_name[0] == '.') continue;
        char path[1024];
        snprintf(path, sizeof path, "%s/%s/course.txt", parent, de->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        names[n++] = strdup(de->d_name);
    }
    closedir(d);
    qsort(names, (size_t)n, sizeof names[0], cmp_str);
    int added = 0;
    for (int i = 0; i < n; i++) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", parent, names[i]);
        if (smk_tracks_add_dir(path) >= 0) added++;
        else fprintf(stderr, "course %s skipped: %s\n", path, last_err);
        free(names[i]);
    }
    return added;
}

void smk_tracks_scan_default(void)
{
    smk_tracks_scan("tracks");
    const char *env = getenv("SMK_TRACKS");
    if (env && *env) {
        char buf[2048];
        snprintf(buf, sizeof buf, "%s", env);
        for (char *tok = strtok(buf, ":"); tok; tok = strtok(NULL, ":"))
            smk_tracks_scan(tok);
    }
    char path[1024];
    const char *xdg = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    if (xdg && *xdg) snprintf(path, sizeof path, "%s/smk-port/tracks", xdg);
    else if (home && *home) snprintf(path, sizeof path, "%s/.local/share/smk-port/tracks", home);
    else path[0] = 0;
    if (path[0]) smk_tracks_scan(path);
}

int smk_tracks_find(const char *id)
{
    if (!id || !*id) return -1;
    if (!strncmp(id, "rom", 3) && id[3] >= '0' && id[3] <= '9') {
        int t = atoi(id + 3);
        return t >= 0 && t < SMK_TRACK_COUNT ? t : -1;
    }
    for (int i = 0; i < nreg; i++)
        if (!strcmp(reg[i].src->id, id)) return SMK_TRACK_COUNT + i;
    /* a path: register it on the spot */
    char path[1024];
    snprintf(path, sizeof path, "%s/course.txt", id);
    struct stat st;
    if (stat(path, &st) == 0) return smk_tracks_add_dir(id);
    return -1;
}
