#include "zstd_codec_v2.hpp"
#include <fstream>
#include <zstd.h>

namespace mantis::codecs {

bool ZstdCodec::compress(
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

        size_t compressed_size = ZSTD_compressBound(input_buffer.size());
        std::vector<uint8_t> compressed_buffer(compressed_size);

        compressed_size = ZSTD_compress(
            compressed_buffer.data(),
            compressed_buffer.size(),
            input_buffer.data(),
            input_buffer.size(),
            level
        );

        if (ZSTD_isError(compressed_size)) {
            error = ZSTD_getErrorName(compressed_size);
            return false;
        }

        std::ofstream out(output, std::ios::binary);
        if (!out) {
            error = "Failed to open output file";
            return false;
        }

        out.write(
            reinterpret_cast<const char*>(compressed_buffer.data()),
            compressed_size
        );
        out.close();

        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

bool ZstdCodec::decompress(
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

        unsigned long long decompressed_size = ZSTD_getFrameContentSize(
            compressed_buffer.data(),
            compressed_buffer.size()
        );

        if (ZSTD_isError(decompressed_size)) {
            error = ZSTD_getErrorName(decompressed_size);
            return false;
        }

        std::vector<uint8_t> decompressed_buffer(decompressed_size);

        size_t result = ZSTD_decompress(
            decompressed_buffer.data(),
            decompressed_buffer.size(),
            compressed_buffer.data(),
            compressed_buffer.size()
        );

        if (ZSTD_isError(result)) {
            error = ZSTD_getErrorName(result);
            return false;
        }

        std::ofstream out(output, std::ios::binary);
        if (!out) {
            error = "Failed to open output file";
            return false;
        }

        out.write(reinterpret_cast<const char*>(decompressed_buffer.data()), result);
        out.close();

        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

CompressionStats ZstdCodec::stats(const fs::path& input, int level) {
    CompressionStats stats;
    stats.algorithm = name();
    stats.original_size = fs::file_size(input);
    stats.compression_level = level;

    fs::path temp_output = input.string() + ".zst.tmp";
    std::string error;

    if (compress(input, temp_output, level, error)) {
        stats.compressed_size = fs::file_size(temp_output);
        stats.compression_ratio = 100.0 * (1.0 - (double)stats.compressed_size / stats.original_size);
        fs::remove(temp_output);
    }

    return stats;
}

}
