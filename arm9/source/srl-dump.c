#include "srl-dump.h"

#include <string.h>

enum { SECTOR = 512, MIN_ROM_ADDRESS = 0x20000, UDISK_ADDRESS = 0x80000 };

static uint32_t word(const uint8_t *bytes)
{
    return bytes[0] | (uint32_t)bytes[1] << 8 |
           (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static bool valid_address(uint32_t address, uint32_t sectors)
{
    const uint32_t block_sectors = NRIO_SRL_MAP_BLOCK / SECTOR;
    return address >= MIN_ROM_ADDRESS && address % NRIO_SRL_MAP_BLOCK == 0 &&
           sectors >= block_sectors && address / SECTOR <= sectors - block_sectors;
}

static uint32_t first_address(uint32_t table, uint32_t sectors)
{
    uint8_t data[SECTOR];
    uint32_t lba = table / SECTOR;
    if (!nrio_recovery_read(lba - 1, 1, data) || !memcmp(data + SECTOR - 4, "QAFE", 4))
        return 0;
    if (!nrio_recovery_read(lba, 1, data)) return 0;
    uint32_t address = word(data);
    return valid_address(address, sectors) ? address : 0;
}

bool nrio_srl_open(struct nrio_raw_info *info)
{
    if (nrio_recovery_startup() && nrio_recovery_select_mode(NRIO_RAW_MAIN_READ) &&
        nrio_recovery_get_info(info)) return true;
    nrio_recovery_shutdown();
    return false;
}

struct nrio_srl_layout nrio_srl_detect(uint32_t sectors)
{
    struct nrio_srl_layout layout = {0};
    uint32_t primary = first_address(NRIO_RAW_PRIMARY_TABLE, sectors);
    uint32_t secondary = first_address(NRIO_RAW_SECONDARY_TABLE, sectors);
    if (primary >= UDISK_ADDRESS) {
        layout.udisk_table = NRIO_RAW_PRIMARY_TABLE;
    } else {
        if (primary) {
            layout.stage2_table = NRIO_RAW_PRIMARY_TABLE;
            layout.bootleg = primary >= NRIO_RAW_SECONDARY_TABLE;
        }
        if (secondary && !layout.bootleg) layout.udisk_table = NRIO_RAW_SECONDARY_TABLE;
    }
    return layout;
}

static bool valid_section(const uint8_t *header, uint32_t size, uint32_t field)
{
    uint32_t start = word(header + field), length = word(header + field + 12);
    return start >= SECTOR && start < size && length && length <= size - start;
}

bool nrio_srl_plan(uint32_t table, uint32_t sectors,
                   uint32_t addresses[NRIO_SRL_MAP_ENTRIES], uint32_t *blocks)
{
    uint8_t data[SECTOR];
    if (table != NRIO_RAW_PRIMARY_TABLE && table != NRIO_RAW_SECONDARY_TABLE) return false;
    uint32_t first = first_address(table, sectors);
    if (!first || !nrio_recovery_read(first / SECTOR, 1, data)) return false;

    uint32_t size = word(data + 0x80), required = 0;
    if (size <= NRIO_SRL_MAP_ENTRIES * NRIO_SRL_MAP_BLOCK &&
        valid_section(data, size, 0x20) && valid_section(data, size, 0x30))
        required = (size + NRIO_SRL_MAP_BLOCK - 1) / NRIO_SRL_MAP_BLOCK;

    uint32_t limit = required ? required : NRIO_SRL_MAP_ENTRIES;
    const uint32_t entries_per_sector = SECTOR / 4;
    uint32_t count = 0;
    while (count < limit) {
        if (!nrio_recovery_read(table / SECTOR + count / entries_per_sector, 1, data))
            return false;
        for (uint32_t i = 0; i < entries_per_sector && count < limit; ++i) {
            uint32_t address = word(data + 4 * i);
            if (!valid_address(address, sectors) ||
                (!count && address != first) ||
                (count && address <= addresses[count - 1])) {
                if (required || !count) return false;
                *blocks = count;
                return true;
            }
            addresses[count++] = address;
        }
    }
    *blocks = count;
    return true;
}

bool nrio_srl_format_check(uint32_t table, uint32_t address, uint32_t bytes,
                           uint32_t metadata_start, uint32_t *fingerprint)
{
    uint8_t data[SECTOR];
    if (!fingerprint || (table != NRIO_RAW_PRIMARY_TABLE && table != NRIO_RAW_SECONDARY_TABLE) ||
        !bytes || bytes > NRIO_RAW_TABLE_BYTES / 4 * NRIO_SRL_MAP_BLOCK ||
        metadata_start < MIN_ROM_ADDRESS + NRIO_SRL_MAP_BLOCK)
        return false;

    uint32_t blocks = (bytes + NRIO_SRL_MAP_BLOCK - 1) / NRIO_SRL_MAP_BLOCK;
    uint32_t prior = 0, hash = 2166136261u;
    for (uint32_t i = 0; i < blocks; ++i) {
        enum { ENTRIES_PER_SECTOR = SECTOR / 4 };
        if (i % ENTRIES_PER_SECTOR == 0 &&
            !nrio_recovery_read(table / SECTOR + i / ENTRIES_PER_SECTOR, 1, data)) return false;
        uint32_t at = word(data + (i % ENTRIES_PER_SECTOR) * 4);
        if (at < MIN_ROM_ADDRESS || at > metadata_start - NRIO_SRL_MAP_BLOCK ||
            at % NRIO_SRL_MAP_BLOCK || (i ? at <= prior : at != address)) return false;
        prior = at;
        hash = (hash ^ at) * 16777619u;
    }
    if (!nrio_recovery_read(address / SECTOR, 1, data)) return false;
    for (unsigned i = 0; i < SECTOR; ++i) hash = (hash ^ data[i]) * 16777619u;
    *fingerprint = hash;
    return true;
}
