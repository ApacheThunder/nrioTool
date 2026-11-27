#include "rom-dump-ui.h"

#include <nds.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <zlib.h>

#include "nrio_card.h"
#include "read_card.h"

static bool confirm(void)
{
    do {
        swiWaitForVBlank();
        scanKeys();
        if (keysDown() & KEY_B) return false;
    } while (!(keysDown() & KEY_A));
    return true;
}

static bool file_missing(const char *path)
{
    struct stat st;
    return stat(path, &st) != 0 && errno == ENOENT;
}

bool nrio_dump_cart_rom(const char *root, bool numbered, struct nrio_rom_dump *result)
{
    char name[24], path[64], temporary[72];
    static u32 header_window[NRIO_ROM_HEADER_BYTES / sizeof(u32)];
    const uint8_t *header_bytes = (const uint8_t *)header_window;
    static u32 buffer[4096 / sizeof(u32)];
    static sNDSHeaderExt header __attribute__((aligned(4)));
    bool available = false;
    for (unsigned n = 0; n < (numbered ? 1000u : 1u); ++n) {
        if (numbered) snprintf(name, sizeof(name), "cart-rom-%03u.nds", n);
        else snprintf(name, sizeof(name), "cart-rom.nds");
        int len = snprintf(path, sizeof(path), "%s/%s", root, name);
        if (len < 0 || (size_t)len >= sizeof(path)) break;
        snprintf(temporary, sizeof(temporary), "%s.part", path);
        if (file_missing(path) && file_missing(temporary)) {
            available = true;
            break;
        }
    }
    consoleClear();
    if (!available) {
        puts("No available ROM dump name.");
        return false;
    }
    puts("Dump cartridge ROM\n\nRead-only Slot-1 capture.");
    if (!isDSiMode()) puts("Reinsert the same Slot-1 cart.");
    puts("\nA Continue  B Cancel");
    if (!confirm()) {
        puts("ROM dump skipped.");
        return false;
    }
    puts("Reading cartridge header...");
    const u32 status = cardInitForRomDump(&header, header_window);
    if (status) {
        printf("ROM init stopped (%02lX).\n", (unsigned long)status);
        return false;
    }

    struct nrio_cart_rom_plan plan;
    bool ok = false;
    if (!nrio_plan_cart_rom((const uint8_t *)header_window, &plan)) {
        puts("ROM header layout unsupported.");
        goto restore_mode;
    }
    struct statvfs fs;
    if (statvfs(root, &fs) == 0 &&
        (uint64_t)fs.f_bavail * (fs.f_frsize ? fs.f_frsize : fs.f_bsize) < plan.bytes) {
        puts("Not enough destination space.");
        goto restore_mode;
    }
    FILE *dst = fopen(temporary, "wb");
    if (!dst) {
        puts("Could not create ROM dump.");
        goto restore_mode;
    }
    bool cancelled = false;
    uint16_t secure_crc = 0xffff;
    uint32_t crc = crc32(0, (const Bytef *)header_window, NRIO_ROM_HEADER_BYTES);
    ok = fwrite(header_window, 1, NRIO_ROM_HEADER_BYTES, dst) == NRIO_ROM_HEADER_BYTES;
    for (uint32_t pos = NRIO_ROM_HEADER_BYTES; ok && pos < plan.bytes;) {
        scanKeys();
        if (keysDown() & KEY_B) { cancelled = true; break; }
        if (pos == NRIO_ROM_HEADER_BYTES || !(pos % 0x10000)) {
            consoleClear();
            printf("Dumping %s\n%lu / %lu KiB\n\nB Cancel\n", name,
                   (unsigned long)(pos / 1024), (unsigned long)((plan.bytes + 1023) / 1024));
        }
        const size_t used = plan.bytes - pos < sizeof(buffer) ? plan.bytes - pos : sizeof(buffer);
        for (size_t off = 0; ok && off < used; off += CARD_DATA_BLOCK_SIZE)
            ok = cardReadDumpSector(pos + off, buffer + off / sizeof(u32));
        if (ok) ok = fwrite(buffer, 1, used, dst) == used;
        if (ok) {
            crc = crc32(crc, (const Bytef *)buffer, used);
            if (pos >= CARD_SECURE_AREA_OFFSET && pos < CARD_DATA_OFFSET)
                secure_crc = swiCRC16(secure_crc, buffer, used);
        }
        pos += used;
    }
    if (fclose(dst)) ok = false;
    if (ok && !cancelled) ok = file_missing(path) && rename(temporary, path) == 0;
    consoleClear();
    if (!ok || cancelled) {
        remove(temporary);
        puts(cancelled ? "ROM dump cancelled." : "Could not read or save ROM.");
        ok = false;
    } else {
        *result = (struct nrio_rom_dump){
            .bytes = plan.bytes, .crc = crc, .chip_id = cardGetId(),
            .header_secure_crc = header_bytes[0x6c] | (uint16_t)header_bytes[0x6d] << 8,
            .dump_secure_crc = secure_crc, .uses_declared_size = plan.uses_declared_size,
        };
        printf("Saved %s\n%lu bytes, CRC32 %08lX\n", name,
               (unsigned long)plan.bytes, (unsigned long)crc);
    }

restore_mode:;
    /* Return from the cartridge ROM session to the N-Card NAND mode. */
    const uint8_t *bytes = header_bytes + 8;
    const u32 card_type = bytes[0] | (u32)bytes[1] << 8 | (u32)bytes[2] << 16 | (u32)bytes[3] << 24;
    InitCartNandReadMode(card_type);
    return ok;
}
