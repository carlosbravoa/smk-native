/* smkos.h: the five things Windows does not get from POSIX.
 *
 * The Unix side is what the port always did, moved here unchanged; the
 * Windows side is the underscore-prefixed CRT equivalent.  No windows.h:
 * the CRT and the environment answer every question asked here, and
 * pulling in the platform header would drag its macros through a file
 * that only wants to make a directory.
 */
#include "smkos.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

#ifdef _WIN32
#  include <direct.h>
#  include <process.h>
#  define smk_rmdir_ _rmdir
#else
#  include <unistd.h>
#  define smk_rmdir_ rmdir
#endif

bool smk_is_sep(char c)
{
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

const char *smk_path_list_sep(void)
{
#ifdef _WIN32
    return ";";
#else
    return ":";
#endif
}

bool smk_mkdir(const char *path)
{
#ifdef _WIN32
    int r = _mkdir(path);
#else
    int r = mkdir(path, 0755);
#endif
    return r == 0 || errno == EEXIST;
}

void smk_mkdirs(const char *dir)
{
    char path[1024];
    snprintf(path, sizeof path, "%s", dir);
    for (char *p = path + 1; *p; p++) {
        if (!smk_is_sep(*p)) continue;
        char sep = *p;
        *p = 0;
        smk_mkdir(path);
        *p = sep;
    }
    smk_mkdir(path);
}

bool smk_realpath(const char *path, char *out, size_t n)
{
#ifdef _WIN32
    char *rp = _fullpath(NULL, path, 0);
#else
    char *rp = realpath(path, NULL);
#endif
    if (!rp) return false;
    snprintf(out, n, "%s", rp);
    free(rp);
    return true;
}

bool smk_scratch_dir(char *out, size_t n)
{
#ifdef _WIN32
    /* no mkdtemp in the Windows CRT: the process id and a counter are
     * unique enough for a directory this process makes and deletes */
    const char *base = getenv("TEMP");
    if (!base || !*base) base = getenv("TMP");
    if (!base || !*base) base = ".";
    for (int try = 0; try < 4096; try++) {
        snprintf(out, n, "%s/smk%u_%d", base, (unsigned)_getpid(), try);
        if (_mkdir(out) == 0) return true;
    }
    return false;
#else
    const char *base = getenv("TMPDIR");
    if (!base || !*base) base = "/tmp";
    char tmpl[1024];
    snprintf(tmpl, sizeof tmpl, "%s/smkXXXXXX", base);
    if (!mkdtemp(tmpl)) return false;
    snprintf(out, n, "%s", tmpl);
    return true;
#endif
}

void smk_rmtree(const char *path)
{
    DIR *d = opendir(path);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char sub[1024];
            snprintf(sub, sizeof sub, "%s/%s", path, e->d_name);
            smk_rmtree(sub);
        }
        closedir(d);
    }
    /* a file goes with remove(), a directory with rmdir(); trying both
     * and ignoring the failure is shorter than asking which it is */
    if (remove(path) != 0) smk_rmdir_(path);
}

void smk_data_dir(char *out, size_t n)
{
    const char *xdg = getenv("XDG_DATA_HOME");
    if (xdg && *xdg) { snprintf(out, n, "%s/smk-port", xdg); return; }
#ifdef _WIN32
    const char *app = getenv("APPDATA");
    if (app && *app) { snprintf(out, n, "%s/smk-port", app); return; }
#endif
    const char *home = getenv("HOME");
    if (home && *home) { snprintf(out, n, "%s/.local/share/smk-port", home); return; }
    snprintf(out, n, ".smk-port");
}
