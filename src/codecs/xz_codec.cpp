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
    return compress(input, output, level, 1, error);
}

bool XzCodec::compress(
    const fs::path& input,
    const fs::path& output,
    int level,
    int threads,
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

        if (threads > 1) {
            std::ofstream out(output, std::ios::binary);
            if (!out) {
                error = "Failed to open output file";
                return false;
            }

            lzma_mt mt{};
            mt.threads = static_cast<uint32_t>(threads);
            mt.block_size = 0;
            mt.timeout = 0;
            mt.preset = static_cast<uint32_t>(level);
            mt.filters = nullptr;
            mt.check = LZMA_CHECK_CRC64;

            lzma_stream stream = LZMA_STREAM_INIT;
            lzma_ret ret = lzma_stream_encoder_mt(&stream, &mt);
            if (ret != LZMA_OK) {
                error = "Failed to initialize threaded LZMA encoder";
                return false;
            }

            std::vector<uint8_t> out_buffer(64 * 1024);
            stream.next_in = input_buffer.data();
            stream.avail_in = input_buffer.size();

            do {
                stream.next_out = out_buffer.data();
                stream.avail_out = out_buffer.size();

                ret = lzma_code(&stream, LZMA_FINISH);
                if (ret != LZMA_OK && ret != LZMA_STREAM_END) {
                    lzma_end(&stream);
                    error = "LZMA threaded compression failed";
                    return false;
                }

                const size_t produced = out_buffer.size() - stream.avail_out;
                if (produced > 0) {
                    out.write(reinterpret_cast<const char*>(out_buffer.data()), produced);
                    if (!out) {
                        lzma_end(&stream);
                        error = "Failed to write output file";
                        return false;
                    }
                }
            } while (ret != LZMA_STREAM_END);

            lzma_end(&stream);
            return true;
        }

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

        std::vector<uint8_t> decompressed_buffer;
        std::vector<uint8_t> out_chunk(64 * 1024);

        lzma_stream stream = LZMA_STREAM_INIT;
        lzma_ret ret = lzma_stream_decoder(&stream, UINT64_MAX, 0);
        if (ret != LZMA_OK) {
            error = "Failed to initialize LZMA decompressor";
            return false;
        }

        stream.next_in = compressed_buffer.data();
        stream.avail_in = compressed_buffer.size();

        do {
            stream.next_out = out_chunk.data();
            stream.avail_out = out_chunk.size();

            ret = lzma_code(&stream, LZMA_FINISH);
            if (ret != LZMA_OK && ret != LZMA_STREAM_END) {
                lzma_end(&stream);
                error = "LZMA decompression failed";
                return false;
            }

            const size_t produced = out_chunk.size() - stream.avail_out;
            decompressed_buffer.insert(
                decompressed_buffer.end(),
                out_chunk.data(),
                out_chunk.data() + produced
            );
        } while (ret != LZMA_STREAM_END);

        lzma_end(&stream);

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
