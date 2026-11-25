#ifndef NRIO_FILE_TRANSFER_H
#define NRIO_FILE_TRANSFER_H
#include <stdbool.h>
#include <stdint.h>

enum nrio_file_result {
    NRIO_FILE_OK, NRIO_FILE_CANCELLED, NRIO_FILE_EXISTS, NRIO_FILE_FAILED,
    NRIO_FILE_SOURCE_KEPT, NRIO_FILE_OLD_KEPT, NRIO_FILE_RESTORE_FAILED
};
typedef bool (*nrio_file_progress)(void *context, bool verifying,
                                  uint64_t complete, uint64_t total);

bool nrio_file_valid_name(const char *name);
enum nrio_file_result nrio_file_transfer(const char *source, const char *dest,
                                        bool move, bool replace,
                                        nrio_file_progress progress, void *context);
#endif
