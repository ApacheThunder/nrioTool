#include "udisk-setup-io.h"

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>

uint32_t nrio_update_crc32(uint32_t crc, const uint8_t *data, size_t size);

bool nrio_file_identify(const char *path, size_t limit, struct nrio_file_id *id)
{
    struct stat st;
    if (stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
        (uintmax_t)st.st_size > limit) return false;
    FILE *file = fopen(path, "rb");
    if (!file) return false;

    uint8_t buffer[2048];
    uint32_t crc = UINT32_MAX;
    size_t remaining = (size_t)st.st_size;
    bool ok = true;
    while (remaining) {
        size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        if (fread(buffer, 1, chunk, file) != chunk) {
            ok = false;
            break;
        }
        crc = nrio_update_crc32(crc, buffer, chunk);
        remaining -= chunk;
    }
    if (ok) ok = fgetc(file) == EOF && !ferror(file);
    if (fclose(file)) ok = false;
    if (ok) *id = (struct nrio_file_id){(size_t)st.st_size, crc ^ UINT32_MAX};
    return ok;
}

bool nrio_file_matches(const char *path, struct nrio_file_id expected)
{
    struct nrio_file_id actual;
    return nrio_file_identify(path, expected.bytes, &actual) &&
           actual.bytes == expected.bytes && actual.crc == expected.crc;
}

static enum nrio_file_result unused_path(const char *path)
{
    struct stat st;
    if (!stat(path, &st)) return NRIO_FILE_EXISTS;
    return errno == ENOENT ? NRIO_FILE_OK : NRIO_FILE_ERROR;
}

enum nrio_file_result nrio_write_new_file(const char *destination, const char *temporary,
                                         struct nrio_file_id expected, nrio_file_source write,
                                         void *context)
{
    enum nrio_file_result result = unused_path(destination);
    if (result != NRIO_FILE_OK) return result;
    if (unused_path(temporary) != NRIO_FILE_OK) return NRIO_FILE_ERROR;
    FILE *out = fopen(temporary, "wb");
    if (!out) return NRIO_FILE_ERROR;
    bool ok = write(context, out);
    if (fclose(out)) ok = false;
    if (ok) ok = nrio_file_matches(temporary, expected);
    if (ok) ok = unused_path(destination) == NRIO_FILE_OK && rename(temporary, destination) == 0;
    if (!ok) {
        remove(temporary);
        return NRIO_FILE_ERROR;
    }
    return NRIO_FILE_OK;
}

struct file_input { FILE *file; size_t bytes; };

static bool copy_file_stream(void *input, FILE *out)
{
    struct file_input *source = input;
    uint8_t buffer[2048];
    size_t remaining = source->bytes;
    while (remaining) {
        size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        if (fread(buffer, 1, chunk, source->file) != chunk || fwrite(buffer, 1, chunk, out) != chunk)
            return false;
        remaining -= chunk;
    }
    return fgetc(source->file) == EOF && !ferror(source->file);
}

enum nrio_file_result nrio_copy_new_file(const char *source, const char *destination,
                                        const char *temporary, struct nrio_file_id expected)
{
    FILE *in = fopen(source, "rb");
    if (!in) return errno == ENOENT ? NRIO_FILE_MISSING : NRIO_FILE_ERROR;
    struct file_input input = {in, expected.bytes};
    enum nrio_file_result result = nrio_write_new_file(destination, temporary, expected,
                                                       copy_file_stream, &input);
    fclose(in);
    return result;
}
