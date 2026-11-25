#ifndef NRIO_NAND_HEALTH_H
#define NRIO_NAND_HEALTH_H

#include <stdbool.h>
#include <stdint.h>

enum nrio_health_flags {
    NRIO_HEALTH_LISTED = 1, NRIO_HEALTH_SPARE = 2,
    NRIO_HEALTH_ECC = 4, NRIO_HEALTH_IO = 8, NRIO_HEALTH_RETRIED = 16,
};
enum nrio_health_area { NRIO_HEALTH_BOOT, NRIO_HEALTH_RESERVED, NRIO_HEALTH_METADATA, NRIO_HEALTH_DATA };
enum nrio_health_bitmap { NRIO_BITMAP_ABSENT, NRIO_BITMAP_FOUND, NRIO_BITMAP_READ_ERROR };

struct nrio_health_report {
    uint32_t scanned, total, boot_issues, data_issues, listed, spare, ecc, io, retried;
    uint32_t bitmap_address;
    enum nrio_health_bitmap bitmap;
    bool cancelled;
};

struct nrio_health_io {
    void *context;
    bool (*read_spare)(void *, uint32_t page, uint8_t spare[64]);
    bool (*read_main)(void *, uint32_t page, uint8_t main[2048]);
    void (*event)(void *, uint32_t address, enum nrio_health_area area, unsigned flags);
    bool (*progress)(void *, uint32_t scanned, uint32_t total);
};

bool nrio_scan_nand_health(const struct nrio_health_io *io, uint32_t sectors,
                           unsigned erase_pages, struct nrio_health_report *report);
bool nrio_recheck_nand_block(const struct nrio_health_io *io, uint32_t sectors,
                              unsigned erase_pages, uint32_t address,
                              enum nrio_health_area area, unsigned *flags);
void nrio_nand_health_ui(const char *root);
#endif
