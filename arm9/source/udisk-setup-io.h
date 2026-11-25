#ifndef NRIO_UDISK_SETUP_IO_H
#define NRIO_UDISK_SETUP_IO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

struct nrio_file_id {
    size_t bytes;
    uint32_t crc;
};

enum nrio_file_result {
    NRIO_FILE_OK,
    NRIO_FILE_EXISTS,
    NRIO_FILE_MISSING,
    NRIO_FILE_ERROR,
};

bool nrio_file_identify(const char *path, size_t limit, struct nrio_file_id *id);
bool nrio_file_matches(const char *path, struct nrio_file_id expected);
typedef bool (*nrio_file_source)(void *context, FILE *output);
enum nrio_file_result nrio_write_new_file(const char *destination, const char *temporary,
                                         struct nrio_file_id expected, nrio_file_source write,
                                         void *context);
enum nrio_file_result nrio_copy_new_file(const char *source, const char *destination,
                                        const char *temporary, struct nrio_file_id expected);

#endif
