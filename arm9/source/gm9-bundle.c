#include "gm9-bundle.h"
#include <stdint.h>
#include <string.h>
#include <zlib.h>

static uint32_t le32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

bool nrio_unpack_gm9(FILE *source, FILE *destination)
{
    uint8_t header[16], input[2048], output[2048];
    if (!source || !destination || fread(header, 1, sizeof(header), source) != sizeof(header) ||
        memcmp(header, "NRIOGM01", 8)) return false;
    const uint32_t length = le32(header + 8), expected_crc = le32(header + 12);
    if (length < 512 || length >= 0x200000) return false;

    z_stream stream = {0};
    if (inflateInit(&stream) != Z_OK) return false;
    bool ok = false;
    uLong crc = 0;
    for (;;) {
        if (!stream.avail_in) {
            stream.next_in = input;
            stream.avail_in = fread(input, 1, sizeof(input), source);
            if (!stream.avail_in && ferror(source)) break;
        }
        stream.next_out = output;
        stream.avail_out = sizeof(output);
        int status = inflate(&stream, Z_NO_FLUSH);
        size_t produced = sizeof(output) - stream.avail_out;
        if ((status != Z_OK && status != Z_STREAM_END) || stream.total_out > length ||
            (produced && fwrite(output, 1, produced, destination) != produced)) break;
        crc = crc32(crc, output, produced);
        if (status == Z_STREAM_END) {
            ok = stream.total_out == length && crc == expected_crc && !stream.avail_in &&
                 fgetc(source) == EOF && !ferror(source);
            break;
        }
    }
    inflateEnd(&stream);
    return ok;
}
