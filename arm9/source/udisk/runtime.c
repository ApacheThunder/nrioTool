#include "runtime.h"

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

/* Installed runtime ARM9 0x0200388C. Dispatch is separated from endpoint
   control and command side effects. */
enum udisk_usb_command udisk_usb_decode_wrapper(const uint8_t *packet, size_t length,
                                                struct udisk_usb_wrapper *wrapper)
{
    if (length != 31 || read_le32(packet) != UINT32_C(0x43425355))
        return UDISK_USB_BAD_WRAPPER;

    wrapper->tag = read_le32(packet + 4);
    wrapper->transfer_bytes = read_le32(packet + 8);
    wrapper->flags = packet[12];
    if (packet[13] != 0 || packet[14] == 0 || packet[14] > 16)
        return UDISK_USB_UNSUPPORTED;

    switch (packet[15]) {
    case 0x00: return UDISK_USB_TEST_UNIT_READY;
    case 0x03: return UDISK_USB_REQUEST_SENSE;
    case 0x12: return UDISK_USB_INQUIRY;
    case 0x1a: return UDISK_USB_MODE_SENSE_6;
    case 0x23: return UDISK_USB_READ_FORMAT_CAPACITIES;
    case 0x25: return UDISK_USB_READ_CAPACITY;
    case 0x28: return UDISK_USB_READ_10;
    case 0x2a: return UDISK_USB_WRITE_10;
    case 0x2f: return UDISK_USB_VERIFY_10;
    case 0x5a: return UDISK_USB_MODE_SENSE_10;
    default: return UDISK_USB_UNSUPPORTED;
    }
}

/* ARM9 0x020037C4. The original uses only the low 24 bits of the CDB LBA. */
bool udisk_usb_rw10_range(const uint8_t packet[31], uint32_t *byte_offset, uint32_t *byte_count)
{
    uint32_t lba = ((uint32_t)packet[18] << 16) | ((uint32_t)packet[19] << 8) | packet[20];
    uint32_t sectors = ((uint32_t)packet[22] << 8) | packet[23];
    *byte_offset = lba * 512;
    *byte_count = sectors * 512;
    return *byte_count == read_le32(packet + 8);
}

static void write_le(uint8_t *dest, uint32_t value, size_t length)
{
    for (size_t byte = 0; byte < length; ++byte)
        dest[byte] = (uint8_t)(value >> (byte * 8));
}

/* The boot sector assembled by ARM9 0x02000E54. The NAND operations and
   subsequent reads of the new filesystem are outside this function. */
bool udisk_build_volume_boot_sector(uint32_t sectors, uint8_t boot_sector[512])
{
    if (sectors < 0x100 || sectors > 0x7fffff)
        return false;

    uint32_t entries = ((sectors - 0x100) >> 7) + 2;
    bool fat16 = entries > 0xff7;
    uint32_t bytes = fat16 ? entries * 2 : (entries * 3 + 1) / 2;
    entries -= (bytes + 511) / 512;
    bytes = fat16 ? entries * 2 : (entries * 3 + 1) / 2;
    uint32_t fat_sectors = (bytes + 511) / 512;
    uint32_t reserved = 0x80 - (fat_sectors * 2 & 0x7f);

    for (size_t byte = 0; byte < 512; ++byte)
        boot_sector[byte] = 0;
    boot_sector[0] = 0xeb;
    boot_sector[1] = 0x3c;
    boot_sector[2] = 0x90;
    const char oem[] = "DSGBAFLS";
    const char label[] = "FLASHFORMAT";
    const char format[] = "FAT1?   ";
    for (size_t byte = 0; byte < sizeof(oem) - 1; ++byte)
        boot_sector[3 + byte] = (uint8_t)oem[byte];
    write_le(boot_sector + 11, 512, 2);
    boot_sector[13] = 0x80;
    write_le(boot_sector + 14, reserved, 2);
    boot_sector[16] = 2;
    write_le(boot_sector + 17, 2048, 2);
    if (sectors * 512 > 0x2000000)
        write_le(boot_sector + 32, sectors, 4);
    else
        write_le(boot_sector + 19, sectors, 2);
    boot_sector[21] = 0xf8;
    write_le(boot_sector + 22, fat_sectors, 2);
    boot_sector[24] = 0x20;
    boot_sector[26] = 2;
    boot_sector[38] = 0x29;
    boot_sector[39] = 0x91;
    for (size_t byte = 0; byte < sizeof(label) - 1; ++byte)
        boot_sector[43 + byte] = (uint8_t)label[byte];
    for (size_t byte = 0; byte < sizeof(format) - 1; ++byte)
        boot_sector[54 + byte] = (uint8_t)format[byte];
    boot_sector[58] = fat16 ? '6' : '2';
    boot_sector[510] = 0x55;
    boot_sector[511] = 0xaa;
    return true;
}

bool udisk_format_volume(const struct udisk_format_io *io, uint32_t sectors,
                         uint8_t workspace[UDISK_FORMAT_WORKSPACE_BYTES])
{
    uint8_t boot_sector[512];
    if (!io || !io->write_sectors || !workspace ||
        !udisk_build_volume_boot_sector(sectors, boot_sector))
        return false;

    enum { CHUNK_SECTORS = UDISK_FORMAT_WORKSPACE_BYTES / 512 };
    uint32_t reserved = boot_sector[14] | (uint32_t)boot_sector[15] << 8;
    uint32_t fat_sectors = boot_sector[22] | (uint32_t)boot_sector[23] << 8;
    uint32_t root = reserved + 2 * fat_sectors;
    if (root > sectors || CHUNK_SECTORS > sectors - root)
        return false;

    /* Stock writes both FATs in 64 KiB chunks, followed by the root
       directory and boot sector. The first chunk seeds the reserved entries. */
    for (size_t i = 0; i < UDISK_FORMAT_WORKSPACE_BYTES; ++i) workspace[i] = 0;
    for (unsigned copy = 0; copy < 2; ++copy) {
        uint32_t start = reserved + copy * fat_sectors;
        workspace[0] = boot_sector[21];
        workspace[1] = 0xff;
        workspace[2] = 0xff;
        workspace[3] = boot_sector[58] == '6' ? 0xff : 0;
        for (uint32_t offset = 0; offset <= fat_sectors; offset += CHUNK_SECTORS) {
            if (!io->write_sectors(io->context, start + offset, CHUNK_SECTORS, workspace))
                return false;
            workspace[0] = workspace[1] = workspace[2] = workspace[3] = 0;
        }
    }
    return io->write_sectors(io->context, root, CHUNK_SECTORS, workspace) &&
           io->write_sectors(io->context, 0, 1, boot_sector);
}

bool udisk_ftl_metadata_ranges(uint32_t data_base, uint32_t erase_bytes,
                               struct udisk_ftl_metadata *metadata)
{
    const uint32_t region = 0x100000;
    if (!metadata || (erase_bytes != 0x20000 && erase_bytes != 0x40000) ||
        data_base < 2 * region || data_base % erase_bytes)
        return false;
    *metadata = (struct udisk_ftl_metadata){
        data_base - 2 * region, data_base - region, data_base
    };
    return true;
}

static void store_physical_entry(uint8_t *entry, uint32_t address)
{
    entry[0] = (uint8_t)(address >> 24);
    entry[1] = (uint8_t)(address >> 16);
}

bool udisk_encode_physical_table(uint32_t data_base, uint32_t erase_bytes,
                                 const uint16_t *blocks, size_t count,
                                 uint8_t table[UDISK_FTL_TABLE_BYTES])
{
    struct udisk_ftl_metadata metadata;
    const size_t entries = UDISK_FTL_TABLE_BYTES / 2;
    if (!table || count > entries || (count && !blocks) ||
        !udisk_ftl_metadata_ranges(data_base, erase_bytes, &metadata))
        return false;

    for (size_t i = 0; i < entries; ++i) {
        uint32_t block = i < count ? blocks[i] : (uint32_t)i;
        store_physical_entry(table + 2 * i, metadata.data_begin + block * erase_bytes);
    }
    return true;
}

static bool bad_block(const uint8_t *bitmap, uint32_t block)
{
    return (bitmap[block / 8] & (1u << (block % 8))) != 0;
}

bool udisk_prepare_physical_table(uint32_t data_base, uint32_t erase_bytes,
                                  uint16_t physical_blocks,
                                  const uint8_t bad[UDISK_FTL_BAD_BITMAP_BYTES],
                                  uint8_t table[UDISK_FTL_TABLE_BYTES],
                                  uint32_t *logical_sectors)
{
    const size_t entries = UDISK_FTL_TABLE_BYTES / 2;
    const uint32_t blocks_per_zone = 64, usable_per_zone = 62;
    if (!bad || !logical_sectors || physical_blocks >= entries ||
        (uint64_t)data_base + (uint64_t)physical_blocks * erase_bytes > UINT32_MAX + UINT64_C(1) ||
        !udisk_encode_physical_table(data_base, erase_bytes, NULL, 0, table))
        return false;

    uint32_t end = physical_blocks;
    for (uint32_t slot = 0; slot < end; ++slot) {
        if (!bad_block(bad, slot)) continue;
        do {
            --end;
        } while (end > slot && bad_block(bad, end));
        if (end > slot)
            store_physical_entry(table + 2 * slot, data_base + end * erase_bytes);
    }
    const uint32_t zones = end / blocks_per_zone;
    if (!zones) return false;

    /* Stock stores the physical block count in its final table entry. */
    store_physical_entry(table + UDISK_FTL_TABLE_BYTES - 2, data_base + end * erase_bytes);
    *logical_sectors = zones * usable_per_zone * (erase_bytes / 512) -
                       usable_per_zone * (erase_bytes / 2048);
    return true;
}

static bool write_metadata_range(const struct udisk_metadata_io *io,
                                 uint32_t begin, uint32_t end, uint32_t erase_bytes,
                                 uint8_t marker, const uint8_t *data, size_t bytes)
{
    for (uint32_t address = begin; address < end; address += erase_bytes) {
        const enum udisk_metadata_write_result result =
            io->replace_block(io->context, address, marker, data, bytes);
        if (result != UDISK_METADATA_RETRY)
            return result == UDISK_METADATA_VERIFIED;
    }
    return false;
}

bool udisk_write_ftl_metadata(const struct udisk_metadata_io *io,
                               uint32_t data_base, uint32_t erase_bytes,
                               const uint8_t bad[UDISK_FTL_BAD_BITMAP_BYTES],
                               const uint8_t table[UDISK_FTL_TABLE_BYTES])
{
    struct udisk_ftl_metadata metadata;
    if (!io || !io->replace_block || !bad || !table ||
        !udisk_ftl_metadata_ranges(data_base, erase_bytes, &metadata))
        return false;
    return write_metadata_range(io, metadata.bad_block_begin, metadata.physical_table_begin,
                                erase_bytes, 0xaa, bad, UDISK_FTL_BAD_BITMAP_BYTES) &&
           write_metadata_range(io, metadata.physical_table_begin, metadata.data_begin,
                                erase_bytes, 0xab, table, UDISK_FTL_TABLE_BYTES);
}

/* Installed runtime ARM9 0x020008E4. File lookup and cluster translation
   use the mounted FAT volume. The resulting map is kept in host memory. */
enum udisk_launch_result udisk_build_launch_map(const struct udisk_launch_io *io,
                                                uint32_t sectors_per_cluster,
                                                uint32_t root_sector, uint32_t data_sector,
                                                const char **selected_path,
                                                uint32_t *map, size_t map_capacity,
                                                size_t *map_entries)
{
    static const char *const paths[] = {
        "/autorun1.nds", "/autorun2.nds", "/autorun3.nds", "/xmenu.dat",
    };
    if (!io || !io->find_file || !io->next_cluster || !io->physical_address ||
        !selected_path || !map || !map_entries || sectors_per_cluster < 32)
        return UDISK_LAUNCH_INVALID_INPUT;

    *map_entries = 0;
    *selected_path = NULL;
    uint32_t cluster = 0;
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        if (io->find_file(io->context, paths[i], &cluster)) {
            *selected_path = paths[i];
            break;
        }
    }
    if (!*selected_path)
        return UDISK_LAUNCH_FILE_MISSING;

    size_t entries_per_cluster = sectors_per_cluster / 32;
    for (;;) {
        if (map_capacity - *map_entries < entries_per_cluster)
            return UDISK_LAUNCH_MAP_FULL;

        uint32_t sector = cluster < 2 ? root_sector : data_sector + (cluster - 2) * sectors_per_cluster;
        uint32_t logical = sector * 512;
        for (size_t i = 0; i < entries_per_cluster; ++i) {
            uint32_t physical = io->physical_address(io->context, logical);
            if (!*map_entries && physical % 0x4000)
                return UDISK_LAUNCH_UNALIGNED;
            map[(*map_entries)++] = physical;
            logical += 0x4000;
        }

        cluster = io->next_cluster(io->context, cluster);
        if (cluster == UINT32_C(0x0FFFFFFF))
            return UDISK_LAUNCH_READY;
    }
}
