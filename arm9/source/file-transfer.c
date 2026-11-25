#include "file-transfer.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

enum { CHUNK = 32768, PATH_BYTES = 1024 };

bool nrio_file_valid_name(const char *name)
{
    size_t n = strlen(name);
    if (!n || n > 255 || name[0] == ' ' || name[n-1] == ' ' || name[n-1] == '.')
        return false;
    for (size_t i = 0; i < n; ++i)
        if ((unsigned char)name[i] < 32 || strchr("\\/:*?\"<>|", name[i])) return false;
    return true;
}

/* Reserve a new name in the destination directory, never truncate a user file.
   8.3 names also work with the retained version of libfat. */
static int reserve(const char *dest, const char *ext, char path[PATH_BYTES])
{
    const char *slash = strrchr(dest, '/');
    int prefix = slash ? (int)(slash - dest + 1) : 0;
    for (unsigned i = 0; i < 256; ++i) {
        int n = snprintf(path, PATH_BYTES, "%.*sNRIO%04X.%s", prefix, dest, i, ext);
        if (n < 0 || n >= PATH_BYTES) return -1;
        if (!strcasecmp(path, dest)) continue;
        int fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0666);
        if (fd >= 0 || errno != EEXIST) return fd;
    }
    return -1;
}

enum nrio_file_result nrio_file_transfer(const char *source, const char *dest,
                                        bool move, bool replace,
                                        nrio_file_progress progress, void *context)
{
    struct stat src, old;
    if (!strcasecmp(source, dest) || stat(source, &src) || !S_ISREG(src.st_mode) ||
        src.st_size < 0) return NRIO_FILE_FAILED;
    bool exists = stat(dest, &old) == 0;
    if (!exists && errno != ENOENT) return NRIO_FILE_FAILED;
    if (exists && !replace) return NRIO_FILE_EXISTS;
    if (exists && !S_ISREG(old.st_mode)) return NRIO_FILE_FAILED;

    /* A non-replacing move on one mounted volume just renames the file. */
    const char *sdev = strchr(source, ':'), *ddev = strchr(dest, ':');
    bool same_volume = sdev && ddev && sdev - source == ddev - dest &&
        !strncasecmp(source, dest, sdev - source);
    if (exists && (same_volume || (!sdev && !ddev)) &&
        src.st_ino && src.st_ino == old.st_ino && src.st_dev == old.st_dev)
        return NRIO_FILE_FAILED;
    if (move && !exists && same_volume)
        return rename(source, dest) == 0 ? NRIO_FILE_OK : NRIO_FILE_FAILED;

    enum nrio_file_result result = NRIO_FILE_FAILED;
    char temp[PATH_BYTES], backup[PATH_BYTES];
    uint8_t *buf = malloc(2 * CHUNK);
    if (!buf) return result;
    FILE *in = fopen(source, "rb"), *out = NULL;
    bool temporary = false;
    uint64_t total = (uint64_t)src.st_size;
    if (!in) goto done;
    int fd = reserve(dest, "TMP", temp);
    if (fd < 0) goto done;
    temporary = true;
    out = fdopen(fd, "wb");
    if (!out) { close(fd); goto done; }

    /* Close before reopening, then compare every byte against the source. */
    for (unsigned verify = 0; verify < 2; ++verify) {
        if (verify) {
            if (fclose(out)) { out = NULL; goto done; }
            out = fopen(temp, "rb");
            if (!out || fseek(in, 0, SEEK_SET)) goto done;
        }
        uint64_t pos = 0;
        do {
            if (progress && !progress(context, verify, pos, total)) {
                result = NRIO_FILE_CANCELLED;
                goto done;
            }
            if (pos == total) break;
            size_t n = total - pos < CHUNK ? (size_t)(total - pos) : CHUNK;
            if (fread(buf, 1, n, in) != n) goto done;
            if (verify) {
                if (fread(buf + CHUNK, 1, n, out) != n ||
                    memcmp(buf, buf + CHUNK, n)) goto done;
            } else if (fwrite(buf, 1, n, out) != n) goto done;
            pos += n;
        } while (true);
        if (fgetc(in) != EOF || ferror(in) ||
            (verify && (fgetc(out) != EOF || ferror(out)))) goto done;
    }
    if (fclose(out)) { out = NULL; goto done; }
    out = NULL;
    fclose(in);
    in = NULL;

    if (exists) {
        /* Keep the old file until the verified copy has its final name.
           libfat won't rename over an existing entry, so remove only our
           reserved empty placeholder first. */
        int oldfd = reserve(dest, "BAK", backup);
        if (oldfd < 0) goto done;
        int closed = close(oldfd);
        int removed = unlink(backup);
        if (closed || removed || rename(dest, backup)) goto done;
        if (rename(temp, dest)) {
            if (rename(backup, dest)) result = NRIO_FILE_RESTORE_FAILED;
            goto done;
        }
        temporary = false;
        if (unlink(backup)) {
            result = NRIO_FILE_OLD_KEPT;
            goto done;
        }
    } else {
        struct stat check;
        if (stat(dest, &check) == 0) { result = NRIO_FILE_EXISTS; goto done; }
        if (errno != ENOENT || rename(temp, dest)) goto done;
        temporary = false;
    }
    result = move && unlink(source) ? NRIO_FILE_SOURCE_KEPT : NRIO_FILE_OK;

done:
    if (out) fclose(out);
    if (in) fclose(in);
    if (temporary) unlink(temp);
    free(buf);
    return result;
}
