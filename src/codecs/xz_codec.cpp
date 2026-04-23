#include "xz_codec.hpp"
#include <algorithm>
#include <fstream>
#include <lzma.h>

namespace mantis::codecs {

bool XzCodec::compress(
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

        std::vector<uint8_t> compressed_buffer(
            lzma_stream_buffer_bound(input_buffer.size())
        );
        size_t out_pos = 0;

        const lzma_ret ret = lzma_easy_buffer_encode(
            static_cast<uint32_t>(level),
            LZMA_CHECK_CRC64,
            nullptr,
            input_buffer.data(),
            input_buffer.size(),
            compressed_buffer.data(),
            &out_pos,
            compressed_buffer.size()
        );

        if (ret != LZMA_OK) {
            error = "LZMA compression failed";
            return false;
        }

        std::ofstream out(output, std::ios::binary);
        if (!out) {
            error = "Failed to open output file";
            return false;
        }

        out.write(reinterpret_cast<const char*>(compressed_buffer.data()), out_pos);
        out.close();

        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

bool XzCodec::decompress(
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

        uint64_t memlimit = UINT64_MAX;
        size_t output_capacity = std::max<size_t>(compressed_buffer.size() * 4, 1024);
        std::vector<uint8_t> decompressed_buffer;
        lzma_ret ret = LZMA_OK;

        for (int attempt = 0; attempt < 10; ++attempt) {
            decompressed_buffer.assign(output_capacity, 0);
            size_t in_pos = 0;
            size_t out_pos = 0;

            ret = lzma_stream_buffer_decode(
                &memlimit,
                0,
                nullptr,
                compressed_buffer.data(),
                &in_pos,
                compressed_buffer.size(),
                decompressed_buffer.data(),
                &out_pos,
                decompressed_buffer.size()
            );

            if (ret == LZMA_OK) {
                decompressed_buffer.resize(out_pos);
                break;
            }

            if (ret == LZMA_BUF_ERROR && in_pos == compressed_buffer.size() && out_pos == decompressed_buffer.size()) {
                output_capacity *= 2;
                continue;
            }

            error = "LZMA decompression failed";
            return false;
        }

        if (ret != LZMA_OK) {
            error = "LZMA decompression failed (output buffer exhausted)";
            return false;
        }

        std::ofstream out(output, std::ios::binary);
        if (!out) {
            error = "Failed to open output file";
            return false;
        }

        out.write(reinterpret_cast<const char*>(decompressed_buffer.data()), decompressed_buffer.size());
        out.close();

        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

CompressionStats XzCodec::stats(const fs::path& input, int level) {
    CompressionStats stats;
    stats.algorithm = name();
    stats.original_size = fs::file_size(input);
    stats.compression_level = level;

    fs::path temp_output = input.string() + ".xz.tmp";
    std::string error;

    if (compress(input, temp_output, level, error)) {
        stats.compressed_size = fs::file_size(temp_output);
        stats.compression_ratio = 100.0 * (1.0 - (double)stats.compressed_size / stats.original_size);
        fs::remove(temp_output);
    }

    return stats;
}

}
