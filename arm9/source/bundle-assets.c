#include "bundle-assets.h"
#include <string.h>
#include <zlib.h>

extern const uint8_t nrio_bundle_bin[] __attribute__((weak));
extern const uint8_t nrio_bundle_bin_end[] __attribute__((weak));

struct asset {
    const uint8_t *packed;
    uint32_t packed_length, length, crc;
};

static uint32_t word(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool find_asset(unsigned id, struct asset *asset)
{
    enum { header_length = 12, index_end = header_length + NRIO_ASSET_COUNT * 16 };
    const uintptr_t start = (uintptr_t)nrio_bundle_bin, end = (uintptr_t)nrio_bundle_bin_end;
    if (!start || end < start || end - start < index_end || id >= NRIO_ASSET_COUNT ||
        memcmp(nrio_bundle_bin, "NRIOSD01", 8) || word(nrio_bundle_bin + 8) != NRIO_ASSET_COUNT)
        return false;
    const uint8_t *entry = nrio_bundle_bin + header_length + id * 16;
    uint32_t pos = word(entry), packed = word(entry + 4), length = word(entry + 8);
    if (pos < index_end || pos > end - start || !packed || packed > end - start - pos ||
        !length || length > 0x200000) return false;
    *asset = (struct asset){nrio_bundle_bin + pos, packed, length, word(entry + 12)};
    return true;
}

bool nrio_asset_info(unsigned id, size_t *bytes, uint32_t *crc)
{
    struct asset a;
    if (!find_asset(id, &a)) return false;
    *bytes = a.length;
    *crc = a.crc;
    return true;
}

bool nrio_asset_expand(unsigned id, void *output, size_t length)
{
    struct asset a;
    if (!find_asset(id, &a) || length != a.length || !output) return false;
    uLongf bytes = length;
    return uncompress(output, &bytes, a.packed, a.packed_length) == Z_OK &&
           bytes == length && crc32(0L, output, bytes) == a.crc;
}

bool nrio_asset_stream(unsigned id, FILE *output)
{
    struct asset a;
    if (!find_asset(id, &a) || !output) return false;
    z_stream stream = {0};
    stream.next_in = (Bytef *)a.packed;
    stream.avail_in = a.packed_length;
    if (inflateInit(&stream) != Z_OK) return false;

    bool ok = false;
    uint8_t buffer[2048];
    uLong crc = 0;
    for (;;) {
        stream.next_out = buffer;
        stream.avail_out = sizeof(buffer);
        int status = inflate(&stream, Z_NO_FLUSH);
        size_t bytes = sizeof(buffer) - stream.avail_out;
        if (stream.total_out > a.length ||
            (status != Z_OK && status != Z_STREAM_END) ||
            (bytes && fwrite(buffer, 1, bytes, output) != bytes)) break;
        crc = crc32(crc, buffer, bytes);
        if (status == Z_STREAM_END) {
            ok = stream.total_in == a.packed_length && stream.total_out == a.length && crc == a.crc;
            break;
        }
    }
    inflateEnd(&stream);
    return ok;
}
