#ifndef NRIO_LAUNCH_CHECK_H
#define NRIO_LAUNCH_CHECK_H

#include <stdbool.h>
#include <stdint.h>

enum { NRIO_LAUNCH_CHUNK = 0x4000, NRIO_LAUNCH_MAX = 0x10000 / 4 };
enum nrio_launch_chain_result {
    NRIO_LAUNCH_CHAIN_OK, NRIO_LAUNCH_CHAIN_IO, NRIO_LAUNCH_CHAIN_LAYOUT,
    NRIO_LAUNCH_CHAIN_INVALID, NRIO_LAUNCH_CHAIN_CANCELLED,
};
struct nrio_launch_chain {
    uint32_t first_sector, last_sector, runs;
};
typedef bool (*nrio_launch_read)(void *, uint32_t sector, uint8_t bytes[512]);
typedef bool (*nrio_launch_continue)(void *);
enum nrio_launch_chain_result nrio_check_launch_chain(nrio_launch_read, nrio_launch_continue,
    void *context, uint32_t first_cluster, uint32_t file_bytes, struct nrio_launch_chain *);
#endif
