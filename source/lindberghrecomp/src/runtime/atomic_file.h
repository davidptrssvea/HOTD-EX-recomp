#pragma once
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <process.h>
#else
#include <unistd.h>
#endif

/* Write beside the destination, then replace it only after a complete flush.
 * A process killed during fwrite must leave the previous cabinet save intact. */
typedef struct { FILE *stream; const char *path; char temporary[1100]; } LgAtomicFile;
static int lg_atomic_file_begin(LgAtomicFile *file, const char *path)
{
    memset(file, 0, sizeof *file);
    file->path = path;
#ifdef _WIN32
    long pid = (long)_getpid();
#else
    long pid = (long)getpid();
#endif
    int n = snprintf(file->temporary, sizeof file->temporary, "%s.tmp.%ld", path, pid);
    if (n < 0 || (size_t)n >= sizeof file->temporary) return 0;
    file->stream = fopen(file->temporary, "wb");
    return file->stream != NULL;
}
static void lg_atomic_file_abort(LgAtomicFile *file)
{
    if (file->stream) fclose(file->stream);
    file->stream = NULL;
    if (*file->temporary) remove(file->temporary);
}
static int lg_atomic_file_commit(LgAtomicFile *file)
{
    if (!file->stream) return 0;
    int ok = !ferror(file->stream) && fflush(file->stream) == 0;
#ifdef _WIN32
    if (ok) ok = _commit(_fileno(file->stream)) == 0;
#else
    if (ok) ok = fsync(fileno(file->stream)) == 0;
#endif
    if (fclose(file->stream) != 0) ok = 0;
    file->stream = NULL;
    if (ok) {
#ifdef _WIN32
        ok = MoveFileExA(file->temporary, file->path,
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        ok = rename(file->temporary, file->path) == 0;
#endif
    }
    if (!ok) remove(file->temporary);
    return ok;
}
