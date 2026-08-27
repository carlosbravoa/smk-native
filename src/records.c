/* Best lap times, five per course, kept on disk.
 *
 * This is the one file here that is NOT the game's: the SNES keeps its
 * records in cartridge SRAM in its own packed format, and we have not
 * decoded it (nor should the port write to the user's save).  These are
 * OUR records of OUR laps, in a plain text file under the user's data
 * directory, so nothing about them is presented as a ROM fact.
 *
 * The unit is the one the game counts in: FRAMES.  Formatting to
 * M'SS"HH is the display's job (hundredths = frames * 100 / 60), so a
 * stored time is exact and never accumulates rounding.
 */
#include "smk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

/* mkdir -p: the data directory may not exist at all yet, and creating
 * only the last component silently fails then (which is how the first
 * save of a fresh install went missing). */
static void mkdirs(char *path)
{
    for (char *p = path + 1; *p; p++) {
        if (*p != '/') continue;
        *p = 0;
        mkdir(path, 0755);
        *p = '/';
    }
    mkdir(path, 0755);
}

const char *smk_records_path(void)
{
    static char path[1024];
    const char *xdg = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    if (xdg && *xdg)
        snprintf(path, sizeof path, "%s/smk-port", xdg);
    else if (home && *home)
        snprintf(path, sizeof path, "%s/.local/share/smk-port", home);
    else
        snprintf(path, sizeof path, ".smk-port");
    mkdirs(path);
    size_t n = strlen(path);
    snprintf(path + n, sizeof path - n, "/laptimes.txt");
    return path;
}

void smk_records_clear(smk_records *r)
{
    memset(r, 0, sizeof *r);
    for (int t = 0; t < SMK_TRACK_COUNT; t++)
        for (int s = 0; s < SMK_RECORD_SLOTS; s++)
            r->best[t][s].frames = 0;      /* 0 = empty slot */
}

void smk_records_load(smk_records *r)
{
    smk_records_clear(r);
    FILE *f = fopen(smk_records_path(), "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        int track = 0, ch = 0;
        long frames = 0;
        if (line[0] == '#') continue;
        if (sscanf(line, "%d %ld %d", &track, &frames, &ch) != 3) continue;
        if (track < 0 || track >= SMK_TRACK_COUNT || frames <= 0) continue;
        if (ch < 0 || ch >= SMK_CHARACTERS) ch = 0;
        smk_records_add(r, track, frames, ch);
    }
    fclose(f);
}

bool smk_records_save(const smk_records *r)
{
    FILE *f = fopen(smk_records_path(), "w");
    if (!f) return false;
    fprintf(f, "# smk-port best lap times: track frames character\n");
    for (int t = 0; t < SMK_TRACK_COUNT; t++)
        for (int s = 0; s < SMK_RECORD_SLOTS; s++)
            if (r->best[t][s].frames > 0)
                fprintf(f, "%d %ld %d\n", t, r->best[t][s].frames,
                        r->best[t][s].character);
    fclose(f);
    return true;
}

int smk_records_add(smk_records *r, int track, long frames, int character)
{
    if (track < 0 || track >= SMK_TRACK_COUNT || frames <= 0) return -1;
    smk_record *b = r->best[track];
    int slot = -1;
    for (int s = 0; s < SMK_RECORD_SLOTS; s++)
        if (b[s].frames == 0 || frames < b[s].frames) { slot = s; break; }
    if (slot < 0) return -1;
    for (int s = SMK_RECORD_SLOTS - 1; s > slot; s--) b[s] = b[s - 1];
    b[slot].frames = frames;
    b[slot].character = character;
    return slot;
}

void smk_time_text(long frames, char *out, size_t n)
{
    if (frames <= 0) { snprintf(out, n, "  '  \"  "); return; }
    /* the game's own formatting: it counts frames and shows hundredths */
    long cs = frames * 100 / 60;
    int hh = (int)(cs % 100);
    long secs = cs / 100;
    int ss = (int)(secs % 60);
    int mm = (int)(secs / 60);
    if (mm > 9) mm = 9;
    snprintf(out, n, "%d'%02d\"%02d", mm, ss, hh);
}
