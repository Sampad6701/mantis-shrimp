#include "brotli_codec.hpp"
#include <array>
#include <fstream>
#include <brotli/encode.h>
#include <brotli/decode.h>

namespace mantis::codecs {

bool BrotliCodec::compress(
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

        size_t encoded_size = 0;
        size_t max_encoded_size = BrotliEncoderMaxCompressedSize(input_buffer.size());
        std::vector<uint8_t> encoded_data(max_encoded_size);
        encoded_size = max_encoded_size;

        BrotliEncoderState* state = BrotliEncoderCreateInstance(nullptr, nullptr, nullptr);
        if (!state) {
            error = "Failed to create Brotli encoder";
            return false;
        }

        BROTLI_BOOL ok = BrotliEncoderCompress(
            level,
            BROTLI_DEFAULT_WINDOW,
            BROTLI_MODE_GENERIC,
            input_buffer.size(),
            input_buffer.data(),
            &encoded_size,
            encoded_data.data()
        );

        BrotliEncoderDestroyInstance(state);

        if (!ok) {
            error = "Brotli compression failed";
            return false;
        }

        std::ofstream out(output, std::ios::binary);
        if (!out) {
            error = "Failed to open output file";
            return false;
        }

        out.write(reinterpret_cast<char*>(encoded_data.data()), encoded_size);
        out.close();

        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

bool BrotliCodec::decompress(
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

        std::vector<uint8_t> compressed(
            (std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>()
        );
        in.close();

        BrotliDecoderState* state = BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);
        if (!state) {
            error = "Failed to create Brotli decoder";
            return false;
        }

        constexpr size_t kChunkSize = 64 * 1024;
        std::vector<uint8_t> decoded_data;
        std::array<uint8_t, kChunkSize> out_chunk{};

        const uint8_t* next_in = compressed.data();
        size_t avail_in = compressed.size();

        while (true) {
            uint8_t* next_out = out_chunk.data();
            size_t avail_out = out_chunk.size();

            const BrotliDecoderResult result = BrotliDecoderDecompressStream(
                state,
                &avail_in,
                &next_in,
                &avail_out,
                &next_out,
                nullptr
            );

            const size_t produced = out_chunk.size() - avail_out;
            decoded_data.insert(decoded_data.end(), out_chunk.data(), out_chunk.data() + produced);

            if (result == BROTLI_DECODER_RESULT_SUCCESS) {
                break;
            }
            if (result == BROTLI_DECODER_RESULT_ERROR) {
                BrotliDecoderDestroyInstance(state);
                error = "Brotli decompression failed";
                return false;
            }
            if (result == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT && avail_in == 0) {
                BrotliDecoderDestroyInstance(state);
                error = "Brotli stream ended unexpectedly";
                return false;
            }
        }

        BrotliDecoderDestroyInstance(state);

        std::ofstream out(output, std::ios::binary);
        if (!out) {
            error = "Failed to open output file";
            return false;
        }

        out.write(reinterpret_cast<const char*>(decoded_data.data()), decoded_data.size());
        out.close();

        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

CompressionStats BrotliCodec::stats(const fs::path& input, int level) {
    CompressionStats stats;
    stats.algorithm = name();
    stats.original_size = fs::file_size(input);
    stats.compression_level = level;

    fs::path temp_output = input.string() + ".br.tmp";
    std::string error;

    if (compress(input, temp_output, level, error)) {
        stats.compressed_size = fs::file_size(temp_output);
        stats.compression_ratio = 100.0 * (1.0 - (double)stats.compressed_size / stats.original_size);
        fs::remove(temp_output);
    }

    return stats;
}

}
