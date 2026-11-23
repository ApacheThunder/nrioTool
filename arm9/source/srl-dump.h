#ifndef NRIO_TOOL_SRL_DUMP_H
#define NRIO_TOOL_SRL_DUMP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "recovery_nand/recovery.h"

#define NRIO_SRL_MAP_BLOCK 0x4000u
#define NRIO_SRL_MAP_ENTRIES (NRIO_RAW_TABLE_BYTES / 4)

struct nrio_srl_layout {
    uint32_t stage2_table;
    uint32_t udisk_table;
    bool bootleg;
};

bool nrio_srl_open(struct nrio_raw_info *info);
struct nrio_srl_layout nrio_srl_detect(uint32_t sectors);
bool nrio_srl_plan(uint32_t table, uint32_t sectors,
                   uint32_t addresses[NRIO_SRL_MAP_ENTRIES], uint32_t *blocks);

#ifdef __cplusplus
}
#endif

#endif
