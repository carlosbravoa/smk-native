/* The OS-specific corners of the port, in one place.
 *
 * Everything else here is plain C11 and SDL2, which Windows already has;
 * what it does not have is POSIX.  Rather than sprinkle #ifdef _WIN32
 * through the course loader and the records file, the five things that
 * actually differ live behind these calls: making a directory tree,
 * resolving a path, a scratch directory, deleting one, and where the
 * user's own data belongs.
 *
 * OURS - none of this is a ROM structure.
 */
#ifndef SMKOS_H
#define SMKOS_H

#include <stdbool.h>
#include <stddef.h>

/* true when c separates path components on this system (Windows takes
 * both, and its own APIs accept the forward slash we write) */
bool smk_is_sep(char c);

/* what separates the entries of a path LIST, as $SMK_TRACKS is: ":" on
 * Unix, ";" on Windows, where a colon is part of every absolute path */
const char *smk_path_list_sep(void);

/* one directory, mode 0755 where modes exist; true if it exists after */
bool smk_mkdir(const char *path);

/* mkdir -p: every missing component.  The data directory may not exist
 * at all yet, and creating only the last component silently fails then
 * (which is how the first save of a fresh install went missing). */
void smk_mkdirs(const char *path);

/* the absolute, normalised path; false leaves out untouched */
bool smk_realpath(const char *path, char *out, size_t n);

/* a fresh empty directory to build a package in, under the system's
 * temporary directory.  The caller deletes it with smk_rmtree. */
bool smk_scratch_dir(char *out, size_t n);

/* rm -rf, without a shell */
void smk_rmtree(const char *path);

/* where this user's lap times and installed courses live, no trailing
 * slash and not created.  $XDG_DATA_HOME or $HOME on Unix, %APPDATA% on
 * Windows; ".smk-port" beside the binary when the environment says
 * nothing at all. */
void smk_data_dir(char *out, size_t n);

#endif
