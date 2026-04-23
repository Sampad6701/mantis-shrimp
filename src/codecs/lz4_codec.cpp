#include "lz4_codec.hpp"
#include <fstream>
#include <lz4.h>
#include <lz4hc.h>
#include <cstring>

namespace mantis::codecs {

bool Lz4Codec::compress(
    const fs::path& input,
    const fs::path& output,
    int level,
    std::string& error
) {
    try {
        std::ifstream in(input, std::ios::binary);
        if (!in) {
            error = "Failed to open input file";
            return false;
        }

        std::vector<uint8_t> input_buffer(
            (std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>()
        );
        in.close();

        int max_compressed_size = LZ4_compressBound(input_buffer.size());
        std::vector<uint8_t> compressed_buffer(max_compressed_size + 4);

        int compressed_size = 0;
        if (level <= 3) {
            compressed_size = LZ4_compress_default(
                reinterpret_cast<const char*>(input_buffer.data()),
                reinterpret_cast<char*>(compressed_buffer.data() + 4),
                input_buffer.size(),
                max_compressed_size
            );
        } else {
            compressed_size = LZ4_compress_HC(
                reinterpret_cast<const char*>(input_buffer.data()),
                reinterpret_cast<char*>(compressed_buffer.data() + 4),
                input_buffer.size(),
                max_compressed_size,
                std::min(level, LZ4HC_CLEVEL_MAX)
            );
        }

        if (compressed_size <= 0) {
            error = "LZ4 compression failed";
            return false;
        }

        uint32_t original_size = input_buffer.size();
        std::memcpy(compressed_buffer.data(), &original_size, 4);

        std::ofstream out(output, std::ios::binary);
        if (!out) {
            error = "Failed to open output file";
            return false;
        }

        out.write(
            reinterpret_cast<const char*>(compressed_buffer.data()),
            compressed_size + 4
        );
        out.close();

        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

bool Lz4Codec::decompress(
    const fs::path& input,
    const fs::path& output,
    std::string& error
) {
    try {
        std::ifstream in(input, std::ios::binary);
        if (!in) {
            error = "Failed to open input file";
            return false;
        }

        std::vector<uint8_t> compressed_buffer(
            (std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>()
        );
        in.close();

        if (compressed_buffer.size() < 4) {
            error = "Invalid LZ4 file format";
            return false;
        }

        uint32_t decompressed_size = 0;
        std::memcpy(&decompressed_size, compressed_buffer.data(), 4);

        std::vector<uint8_t> decompressed_buffer(decompressed_size);

        int result = LZ4_decompress_safe(
            reinterpret_cast<const char*>(compressed_buffer.data() + 4),
            reinterpret_cast<char*>(decompressed_buffer.data()),
            compressed_buffer.size() - 4,
            decompressed_size
        );

        if (result < 0) {
            error = "LZ4 decompression failed";
            return false;
        }

        std::ofstream out(output, std::ios::binary);
        if (!out) {
            error = "Failed to open output file";
            return false;
        }

        out.write(reinterpret_cast<const char*>(decompressed_buffer.data()), decompressed_size);
        out.close();

        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

CompressionStats Lz4Codec::stats(const fs::path& input, int level) {
    CompressionStats stats;
    stats.algorithm = name();
    stats.original_size = fs::file_size(input);
    stats.compression_level = level;

    fs::path temp_output = input.string() + ".lz4.tmp";
    std::string error;

    if (compress(input, temp_output, level, error)) {
        stats.compressed_size = fs::file_size(temp_output);
        stats.compression_ratio = 100.0 * (1.0 - (double)stats.compressed_size / stats.original_size);
        fs::remove(temp_output);
    }

    return stats;
}

}
