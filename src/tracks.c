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
#include "smkos.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

static void slug_of_dir(const char *dir, char *out, size_t n)
{
    /* the last path component, trailing slashes ignored */
    size_t len = strlen(dir);
    while (len > 1 && smk_is_sep(dir[len - 1])) len--;
    size_t start = len;
    while (start > 0 && !smk_is_sep(dir[start - 1])) start--;
    size_t k = len - start;
    if (k >= n) k = n - 1;
    memcpy(out, dir + start, k);
    out[k] = 0;
}

/* ---- .smkt: a ZIP of the package, entries STORED ----------------------
 *
 * The one shareable file (docs/TRACKS.md 2).  Read here without a ZIP
 * library: local file headers walked in order, method 0 only, sizes in
 * the header (no data descriptors) - what the editor and trackgen.py
 * write.  A deflated entry is refused with a message, not a crash. */
typedef struct { char name[64]; const uint8_t *data; size_t size; } zip_entry;
typedef struct {
    bool is_zip;
    char dir[1024];
    uint8_t *blob;
    size_t blobsz;
    zip_entry ent[16];
    int nent;
} pkg_ctx;

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static bool zip_open(pkg_ctx *c, const char *path, char *err, size_t errsz)
{
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, errsz, "%s: cannot open", path); return false; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 8 * 1024 * 1024) { fclose(f); snprintf(err, errsz, "%s: not a course file", path); return false; }
    c->blob = malloc((size_t)n);
    c->blobsz = (size_t)n;
    if (!c->blob || fread(c->blob, 1, (size_t)n, f) != (size_t)n) { fclose(f); snprintf(err, errsz, "%s: short read", path); return false; }
    fclose(f);
    size_t p = 0;
    c->nent = 0;
    while (p + 30 <= c->blobsz && rd32(c->blob + p) == 0x04034b50u) {
        uint16_t flags = rd16(c->blob + p + 6), method = rd16(c->blob + p + 8);
        uint32_t csize = rd32(c->blob + p + 18), usize = rd32(c->blob + p + 22);
        uint16_t nlen = rd16(c->blob + p + 26), xlen = rd16(c->blob + p + 28);
        if (method != 0 || (flags & 8) || csize != usize) {
            snprintf(err, errsz, "%s: a compressed entry - a course file stores its entries plain", path);
            return false;
        }
        if (p + 30 + nlen + xlen + csize > c->blobsz) { snprintf(err, errsz, "%s: truncated", path); return false; }
        if (c->nent < 16 && nlen < sizeof c->ent[0].name) {
            zip_entry *e = &c->ent[c->nent++];
            memcpy(e->name, c->blob + p + 30, nlen);
            e->name[nlen] = 0;
            e->data = c->blob + p + 30 + nlen + xlen;
            e->size = csize;
        }
        p += 30 + nlen + xlen + csize;
    }
    if (c->nent == 0) { snprintf(err, errsz, "%s: not a course file (no entries)", path); return false; }
    return true;
}

static void pkg_close(pkg_ctx *c) { free(c->blob); c->blob = NULL; }

/* A member's BYTES, not a FILE*: the ZIP entry where it already sits in
 * the blob, the directory's file read whole.  One shape for both, so the
 * parsers below need no fmemopen - which POSIX has and Windows does not. */
typedef struct { const uint8_t *p; size_t n; uint8_t *owned; } member;

static bool member_get(pkg_ctx *c, const char *name, member *m)
{
    memset(m, 0, sizeof *m);
    if (c->is_zip) {
        for (int i = 0; i < c->nent; i++)
            if (!strcmp(c->ent[i].name, name)) {
                m->p = c->ent[i].data; m->n = c->ent[i].size;
                return true;
            }
        return false;
    }
    char path[1200];
    snprintf(path, sizeof path, "%s/%s", c->dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return false; }
    uint8_t *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return false; }
    m->n = fread(buf, 1, (size_t)n, f);
    fclose(f);
    m->owned = buf; m->p = buf;
    return true;
}

static void member_free(member *m) { free(m->owned); memset(m, 0, sizeof *m); }

/* fgets over a member: the next line, the newline kept, NUL terminated,
 * split at bufsz - 1 exactly as fgets splits an over-long line */
static bool member_line(const member *m, size_t *pos, char *buf, size_t bufsz)
{
    if (*pos >= m->n) return false;
    size_t i = *pos, k = 0;
    while (i < m->n && k + 1 < bufsz) {
        char ch = (char)m->p[i++];
        buf[k++] = ch;
        if (ch == '\n') break;
    }
    buf[k] = 0;
    *pos = i;
    return true;
}

static bool member_read(pkg_ctx *c, const char *name, void *buf, size_t want, char *err, size_t errsz)
{
    member m;
    if (!member_get(c, name, &m)) { snprintf(err, errsz, "%s/%s: missing", c->dir, name); return false; }
    bool ok = m.n == want;
    if (ok) memcpy(buf, m.p, want);
    else    snprintf(err, errsz, "%s/%s: expected %zu bytes", c->dir, name, want);
    member_free(&m);
    return ok;
}

static bool ends_with(const char *s, const char *suffix)
{
    size_t n = strlen(s), m = strlen(suffix);
    return n >= m && !strcmp(s + n - m, suffix);
}

/* ---- the reader ------------------------------------------------------- */

bool smk_src_from_pkg(const char *dir, smk_course_src *out, char *err, size_t errsz)
{
    char path[1024];
    pkg_ctx ctx;
    memset(&ctx, 0, sizeof ctx);
    snprintf(ctx.dir, sizeof ctx.dir, "%s", dir);
    ctx.is_zip = ends_with(dir, ".smkt");
    if (ctx.is_zip && !zip_open(&ctx, dir, err, errsz)) { pkg_close(&ctx); return false; }
    memset(out, 0, sizeof *out);
    out->rom_track = -1;
    out->theme = -1;
    out->item_block = 1;
    /* the ROM's own spawn-offset table, so a package's segments behave
     * as a slot's do (0, 8, 16, 24, then back to the first window) */
    static const uint16_t SEG_OFF[8] = { 0, 8, 16, 24, 0, 0, 0, 0 };
    memcpy(out->seg_off, SEG_OFF, sizeof out->seg_off);
    slug_of_dir(dir, out->id, sizeof out->id);
    if (ctx.is_zip) {
        char *dot = strrchr(out->id, '.');
        if (dot) *dot = 0;
    }
    snprintf(out->name, sizeof out->name, "%s", out->id);
    for (char *p = out->name; *p; p++)
        if (*p >= 'a' && *p <= 'z') *p -= 'a' - 'A';

    snprintf(path, sizeof path, "%s/course.txt", dir);
    member m;
    size_t mpos = 0;
    if (!member_get(&ctx, "course.txt", &m)) { snprintf(err, errsz, "%s: missing", path); pkg_close(&ctx); return false; }
    char line[256];
    int lineno = 0, format = 0;
    bool have_grid = false, have_fin = false, have_seg = false;
    while (member_line(&m, &mpos, line, sizeof line)) {
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
        } else if (!strcmp(key, "creator")) {
            snprintf(out->author, sizeof out->author, "%s", v);
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
            if (out->nstamp >= SMK_STAMPS_MAX) { snprintf(err, errsz, "%s:%d: more than %d objects", path, lineno, SMK_STAMPS_MAX); member_free(&m); return false; }
            out->stamp[out->nstamp].kind = (uint8_t)strtol(kind, NULL, 0);
            out->stamp[out->nstamp].cell = (uint16_t)(b * 128 + a);
            out->nstamp++;
        } else if (!strcmp(key, "entity")) {
            int k = 0;
            int n = sscanf(v, "%d %d %d", &a, &b, &k);
            if (n < 2 || a < 0 || a > 127 || b < 0 || b > 127 || k < 0 || k > 3) goto bad;
            if (out->nent >= SMK_SRC_ENTS) { snprintf(err, errsz, "%s:%d: more than %d entities", path, lineno, SMK_SRC_ENTS); member_free(&m); return false; }
            out->ent[out->nent++] = (uint16_t)((k << 14) | (b << 7) | a);
        } else if (!strcmp(key, "segment")) {
            if (sscanf(v, "%d", &a) != 1 || a < 0 || a > 254) goto bad;
            if (out->nseg >= SMK_SRC_SEGS - 1) { snprintf(err, errsz, "%s:%d: more than %d segments", path, lineno, SMK_SRC_SEGS - 1); member_free(&m); return false; }
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
            member_free(&m); return false;
        }
        continue;
    bad:
        snprintf(err, errsz, "%s:%d: bad '%s' line", path, lineno, key);
        member_free(&m); return false;
    }
    member_free(&m);
    #define FAIL_PKG(...) do { snprintf(err, errsz, __VA_ARGS__); pkg_close(&ctx); return false; } while (0)
    if (format != 1) FAIL_PKG("%s: format %d is not 1", path, format);
    if (out->theme < 0 || out->theme >= SMK_THEME_COUNT) FAIL_PKG("%s: theme must be 0..7", path);
    if (!have_grid) FAIL_PKG("%s: no grid line", path);
    if (!have_fin)  FAIL_PKG("%s: no finish line", path);
    if (out->item_block < 0 || out->item_block > 7) FAIL_PKG("%s: items must be 0..7", path);
    /* the threshold list's terminator; no `segment` at all means one
     * window for the whole lap (still a list: entities need one) */
    if (out->nent > 0 || have_seg) out->seg_thresh[out->nseg++] = 0xFF;

    if (!member_read(&ctx, "map.bin", out->map, SMK_MAP_BYTES, err, errsz)) { pkg_close(&ctx); return false; }
    if (!member_read(&ctx, "sectors.bin", out->sect, SMK_SECT_CELLS, err, errsz)) { pkg_close(&ctx); return false; }

    snprintf(path, sizeof path, "%s/line.txt", dir);
    if (!member_get(&ctx, "line.txt", &m)) { snprintf(err, errsz, "%s: missing", path); pkg_close(&ctx); return false; }
    mpos = 0;
    lineno = 0;
    while (member_line(&m, &mpos, line, sizeof line)) {
        lineno++;
        char *h = strchr(line, '#');
        if (h) *h = 0;
        int x, y, attr = 0;
        int n = sscanf(line, "%d %d %i", &x, &y, &attr);
        if (n < 2) continue;
        if (x < 0 || x > 1016 || y < 0 || y > 1016 || (x & 7) || (y & 7) || attr < 0 || attr > 255) {
            snprintf(err, errsz, "%s:%d: a waypoint is x y attr, x and y multiples of 8", path, lineno);
            member_free(&m); return false;
        }
        if (out->sectors >= SMK_MAX_SECTORS - 1) { snprintf(err, errsz, "%s: more than %d waypoints", path, SMK_MAX_SECTORS - 1); member_free(&m); return false; }
        out->wp[out->sectors][0] = (uint8_t)(x / 8);
        out->wp[out->sectors][1] = (uint8_t)(y / 8);
        out->wp[out->sectors][2] = (uint8_t)attr;
        out->sectors++;
    }
    member_free(&m);
    if (out->sectors == 0) FAIL_PKG("%s: no waypoints", path);
    /* every painted cell must name a sector that has a waypoint */
    for (int i = 0; i < SMK_SECT_CELLS; i++)
        if (out->sect[i] != SMK_SECT_OFF && (out->sect[i] & 0x7F) >= out->sectors)
            FAIL_PKG("%s/sectors.bin: cell %d names sector %d, the line has %d",
                     dir, i, out->sect[i] & 0x7F, out->sectors);
    #undef FAIL_PKG

    if (out->has_style) {
        if (!member_read(&ctx, "style/tiles.bin", out->style_tiles, sizeof out->style_tiles, err, errsz)
            || !member_read(&ctx, "style/palette.bin", out->style_palette, sizeof out->style_palette, err, errsz)
            || !member_read(&ctx, "style/surface.bin", out->style_surface, sizeof out->style_surface, err, errsz)) {
            pkg_close(&ctx); return false;
        }
    }
    pkg_close(&ctx);
    return true;
}

/* ---- the writer ------------------------------------------------------- */

bool smk_src_write_pkg(const smk_course_src *src, const char *dir, char *err, size_t errsz)
{
    char path[1024];
    smk_mkdirs(dir);
    snprintf(path, sizeof path, "%s/course.txt", dir);
    FILE *f = fopen(path, "w");
    if (!f) { snprintf(err, errsz, "%s: cannot write", path); return false; }
    fprintf(f, "# smk-port course package (docs/TRACKS.md)\n");
    fprintf(f, "format   1\n");
    fprintf(f, "name     %s\n", src->name);
    if (src->author[0]) fprintf(f, "creator  %s\n", src->author);
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
        smk_mkdirs(path);
        snprintf(path, sizeof path, "%s/style/tiles.bin", dir);
        if (!write_file(path, src->style_tiles, sizeof src->style_tiles, err, errsz)) return false;
        snprintf(path, sizeof path, "%s/style/palette.bin", dir);
        if (!write_file(path, src->style_palette, sizeof src->style_palette, err, errsz)) return false;
        snprintf(path, sizeof path, "%s/style/surface.bin", dir);
        if (!write_file(path, src->style_surface, sizeof src->style_surface, err, errsz)) return false;
    }
    return true;
}

/* The same package as one .smkt: a ZIP with every entry stored.  Written
 * through a directory (the writer above) so the two never differ. */
static const uint32_t CRC_POLY = 0xEDB88320u;
static uint32_t crc32_of(const uint8_t *d, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (CRC_POLY & (0u - (c & 1u)));
    }
    return c ^ 0xFFFFFFFFu;
}
static void wr16(FILE *f, unsigned v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }
static void wr32(FILE *f, uint32_t v) { wr16(f, v & 0xFFFF); wr16(f, v >> 16); }

bool smk_src_write_smkt(const smk_course_src *src, const char *file, char *err, size_t errsz)
{
    char dir[1024];
    if (!smk_scratch_dir(dir, sizeof dir)) { snprintf(err, errsz, "cannot make a scratch directory"); return false; }
    if (!smk_src_write_pkg(src, dir, err, errsz)) { smk_rmtree(dir); return false; }
    static const char *names[] = { "course.txt", "map.bin", "sectors.bin", "line.txt", "roles.txt",
                                   "style/tiles.bin", "style/palette.bin", "style/surface.bin" };
    FILE *out = fopen(file, "wb");
    if (!out) { snprintf(err, errsz, "%s: cannot write", file); smk_rmtree(dir); return false; }
    long offs[8]; size_t sizes[8]; uint32_t crcs[8]; int written = 0, idx[8];
    for (int i = 0; i < 8; i++) {
        char path[1100];
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        FILE *in = fopen(path, "rb");
        if (!in) continue;
        fseek(in, 0, SEEK_END); long n = ftell(in); fseek(in, 0, SEEK_SET);
        uint8_t *buf = malloc((size_t)n + 1);
        if (!buf || fread(buf, 1, (size_t)n, in) != (size_t)n) { fclose(in); free(buf); continue; }
        fclose(in);
        offs[written] = ftell(out); sizes[written] = (size_t)n; crcs[written] = crc32_of(buf, (size_t)n); idx[written] = i;
        wr32(out, 0x04034b50u); wr16(out, 20); wr16(out, 0); wr16(out, 0); wr16(out, 0); wr16(out, 0x21);
        wr32(out, crcs[written]); wr32(out, (uint32_t)n); wr32(out, (uint32_t)n);
        wr16(out, (unsigned)strlen(names[i])); wr16(out, 0);
        fwrite(names[i], 1, strlen(names[i]), out);
        fwrite(buf, 1, (size_t)n, out);
        free(buf);
        written++;
    }
    long cd = ftell(out);
    for (int k = 0; k < written; k++) {
        const char *nm = names[idx[k]];
        wr32(out, 0x02014b50u); wr16(out, 20); wr16(out, 20); wr16(out, 0); wr16(out, 0); wr16(out, 0); wr16(out, 0x21);
        wr32(out, crcs[k]); wr32(out, (uint32_t)sizes[k]); wr32(out, (uint32_t)sizes[k]);
        wr16(out, (unsigned)strlen(nm)); wr16(out, 0); wr16(out, 0); wr16(out, 0); wr16(out, 0); wr32(out, 0);
        wr32(out, (uint32_t)offs[k]);
        fwrite(nm, 1, strlen(nm), out);
    }
    long cdend = ftell(out);
    wr32(out, 0x06054b50u); wr16(out, 0); wr16(out, 0); wr16(out, (unsigned)written); wr16(out, (unsigned)written);
    wr32(out, (uint32_t)(cdend - cd)); wr32(out, (uint32_t)cd); wr16(out, 0);
    fclose(out);
    smk_rmtree(dir);
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
    /* one directory is one course however it is named: the game scans
     * ./tracks/x at startup and the editor launches /abs/path/tracks/x,
     * and the two used to collide as "a course called 'x' is already
     * registered" */
    char real[512];
    if (!smk_realpath(dir, real, sizeof real)) snprintf(real, sizeof real, "%s", dir);
    size_t len = strlen(real);
    while (len > 1 && smk_is_sep(real[len - 1])) real[--len] = 0;
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
        struct stat st;
        if (ends_with(de->d_name, ".smkt")) {
            snprintf(path, sizeof path, "%s/%s", parent, de->d_name);
            if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) names[n++] = strdup(de->d_name);
            continue;
        }
        snprintf(path, sizeof path, "%s/%s/course.txt", parent, de->d_name);
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
        const char *sep = smk_path_list_sep();
        for (char *tok = strtok(buf, sep); tok; tok = strtok(NULL, sep))
            smk_tracks_scan(tok);
    }
    char path[1024], data[512];
    smk_data_dir(data, sizeof data);
    snprintf(path, sizeof path, "%s/tracks", data);
    smk_tracks_scan(path);
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
    struct stat st;
    if (ends_with(id, ".smkt") && stat(id, &st) == 0) return smk_tracks_add_dir(id);
    snprintf(path, sizeof path, "%s/course.txt", id);
    if (stat(path, &st) == 0) return smk_tracks_add_dir(id);
    return -1;
}
