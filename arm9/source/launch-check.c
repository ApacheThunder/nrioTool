#include "launch-check.h"

#include <string.h>

static uint32_t le(const uint8_t *p, unsigned n)
{
    uint32_t v = 0;
    for (unsigned i = 0; i < n; ++i) v |= (uint32_t)p[i] << (i * 8);
    return v;
}

static bool bpb(const uint8_t *b)
{
    return b[510] == 0x55 && b[511] == 0xaa && (b[0] == 0xeb || b[0] == 0xe9) &&
           le(b + 11, 2) == 512 && b[13] && !(b[13] & (b[13] - 1)) &&
           le(b + 14, 2) && (b[16] == 1 || b[16] == 2);
}

static bool fat_partition(uint8_t type)
{
    return type == 4 || type == 6 || type == 0xe || type == 0xb || type == 0xc;
}

enum nrio_launch_chain_result nrio_check_launch_chain(nrio_launch_read read,
    nrio_launch_continue more, void *context, uint32_t cluster, uint32_t bytes,
    struct nrio_launch_chain *out)
{
    uint8_t buf[512];
    uint32_t base = 0, partition_sectors = 0;
    memset(out, 0, sizeof(*out));
    if (!bytes || !read(context, 0, buf)) return NRIO_LAUNCH_CHAIN_IO;
    if (!bpb(buf)) {
        if (buf[510] != 0x55 || buf[511] != 0xaa) return NRIO_LAUNCH_CHAIN_LAYOUT;
        for (unsigned i = 0; i < 4; ++i) {
            const uint8_t *p = buf + 446 + i * 16;
            if (!fat_partition(p[4])) continue;
            if (base) return NRIO_LAUNCH_CHAIN_LAYOUT; /* ambiguous volume */
            base = le(p + 8, 4);
            partition_sectors = le(p + 12, 4);
            if (!base || !partition_sectors) return NRIO_LAUNCH_CHAIN_LAYOUT;
        }
        if (!base) return NRIO_LAUNCH_CHAIN_LAYOUT;
        if (!read(context, base, buf)) return NRIO_LAUNCH_CHAIN_IO;
        if (!bpb(buf)) return NRIO_LAUNCH_CHAIN_LAYOUT;
    }
    const uint32_t spc = buf[13], reserved = le(buf + 14, 2);
    const unsigned fats = buf[16];
    const uint32_t entries = le(buf + 17, 2);
    const uint32_t total16 = le(buf + 19, 2), fat16 = le(buf + 22, 2);
    const uint32_t total = total16 ? total16 : le(buf + 32, 4);
    const uint32_t fatlen = fat16 ? fat16 : le(buf + 36, 4);
    const uint64_t data = (uint64_t)reserved + (uint64_t)fats * fatlen + (entries + 15) / 16;
    if (data >= total || (uint64_t)base + total > UINT32_MAX ||
        (partition_sectors && total > partition_sectors) || !fatlen) return NRIO_LAUNCH_CHAIN_LAYOUT;
    const uint32_t clusters = (total - data) / spc;
    if (spc < 32 || clusters < 4085 || clusters >= 0x0ffffff5) return NRIO_LAUNCH_CHAIN_LAYOUT;
    const unsigned width = clusters < 65525 ? 2 : 4;
    if ((width == 2 && (!fat16 || !entries)) || (width == 4 && (fat16 || entries)) ||
        (uint64_t)fatlen * 512 < (uint64_t)(clusters + 2) * width) return NRIO_LAUNCH_CHAIN_LAYOUT;
    unsigned active = 0;
    if (width == 4 && (buf[40] & 0x80)) {
        active = buf[40] & 0xf;
        if (active >= fats) return NRIO_LAUNCH_CHAIN_LAYOUT;
    }

    const uint32_t needed = (bytes - 1) / (spc * 512u) + 1;
    if (needed > clusters) return NRIO_LAUNCH_CHAIN_INVALID;
    uint32_t cached = UINT32_MAX;
    for (uint32_t i = 0; i < needed; ++i) {
        if (more && !more(context)) return NRIO_LAUNCH_CHAIN_CANCELLED;
        if (cluster < 2 || cluster - 2 >= clusters ||
            cluster >= (width == 2 ? 0xfff0u : 0x0ffffff0u)) return NRIO_LAUNCH_CHAIN_INVALID;
        const uint32_t sector = base + (uint32_t)data + (cluster - 2) * spc;
        if (!i) out->first_sector = sector;
        if (!i || sector != out->last_sector + spc) ++out->runs;
        out->last_sector = sector;
        const uint32_t offset = cluster * width;
        const uint32_t at = base + reserved + active * fatlen + offset / 512;
        if (cached != at) {
            if (!read(context, at, buf)) return NRIO_LAUNCH_CHAIN_IO;
            cached = at;
        }
        cluster = le(buf + offset % 512, width);
        if (width == 4) cluster &= 0x0fffffff;
    }
    /* A chain must exit at the expected length. A cycle cannot reach EOC. */
    return cluster >= (width == 2 ? 0xfff8u : 0x0ffffff8u)
           ? NRIO_LAUNCH_CHAIN_OK : NRIO_LAUNCH_CHAIN_INVALID;
}
