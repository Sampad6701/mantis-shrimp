#include "zstd_codec.hpp"
#include <fstream>
#include <ostream>
#include <zstd.h>

namespace mantis::codecs {

namespace {

std::string zstd_error(std::size_t code) {
    return ZSTD_getErrorName(code);
}

}

ZstdStreamCompressor::ZstdStreamCompressor(std::ostream& output, int compression_level)
    : output_(output),
      stream_(ZSTD_createCStream()),
      out_buffer_(ZSTD_CStreamOutSize()) {
    ZSTD_initCStream(static_cast<ZSTD_CStream*>(stream_), compression_level);
}

ZstdStreamCompressor::~ZstdStreamCompressor() {
    ZSTD_freeCStream(static_cast<ZSTD_CStream*>(stream_));
}

bool ZstdStreamCompressor::write(std::span<const std::byte> input, std::string& error) {
    return drain(input.data(), input.size(), false, error);
}

bool ZstdStreamCompressor::finish(std::string& error) {
    if (finished_) {
        return true;
    }

    finished_ = drain(nullptr, 0, true, error);
    return finished_;
}

bool ZstdStreamCompressor::drain(const void* input_data,
                                 std::size_t input_size,
                                 bool end_frame,
                                 std::string& error) {
    ZSTD_inBuffer input{input_data, input_size, 0};

    do {
        ZSTD_outBuffer output_buffer{out_buffer_.data(), out_buffer_.size(), 0};
        const std::size_t result =
            end_frame ? ZSTD_compressStream2(static_cast<ZSTD_CStream*>(stream_),
                                             &output_buffer,
                                             &input,
                                             ZSTD_e_end)
                      : ZSTD_compressStream2(static_cast<ZSTD_CStream*>(stream_),
                                             &output_buffer,
                                             &input,
                                             ZSTD_e_continue);

        if (ZSTD_isError(result)) {
            error = zstd_error(result);
            return false;
        }

        output_.write(out_buffer_.data(), static_cast<std::streamsize>(output_buffer.pos));
        if (!output_) {
            error = "failed to write compressed output";
            return false;
        }

        if (!end_frame && input.pos == input.size) {
            return true;
        }

        if (end_frame && result == 0) {
            return true;
        }
    } while (true);
}

bool ZstdCodec::compress(
    const fs::path& input,
    const fs::path& output,
    int level,
    std::string& error
) {
    return compress(input, output, level, 1, error);
}

bool ZstdCodec::compress(
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

        ZSTD_CCtx* context = ZSTD_createCCtx();
        if (context == nullptr) {
            error = "Failed to create Zstandard context";
            return false;
        }

        size_t result = ZSTD_CCtx_setParameter(context, ZSTD_c_compressionLevel, level);
        if (ZSTD_isError(result)) {
            error = ZSTD_getErrorName(result);
            ZSTD_freeCCtx(context);
            return false;
        }

        if (threads > 1) {
            result = ZSTD_CCtx_setParameter(context, ZSTD_c_nbWorkers, threads);
            if (ZSTD_isError(result)) {
                error = ZSTD_getErrorName(result);
                ZSTD_freeCCtx(context);
                return false;
            }
        }

        size_t compressed_size = ZSTD_compressBound(input_buffer.size());
        std::vector<uint8_t> compressed_buffer(compressed_size);

        compressed_size = ZSTD_compress2(
            context,
            compressed_buffer.data(),
            compressed_buffer.size(),
            input_buffer.data(),
            input_buffer.size()
        );
        ZSTD_freeCCtx(context);

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

bool decompress_zstd_file(const std::filesystem::path& archive_path,
                          std::vector<std::byte>& output,
                          std::string& error) {
    std::ifstream input(archive_path, std::ios::binary);
    if (!input) {
        error = "failed to open compressed file";
        return false;
    }

    std::vector<char> in_buffer(ZSTD_DStreamInSize());
    std::vector<char> out_buffer(ZSTD_DStreamOutSize());

    ZSTD_DStream* stream = ZSTD_createDStream();
    if (stream == nullptr) {
        error = "failed to create zstd decompressor";
        return false;
    }

    const std::size_t init_result = ZSTD_initDStream(stream);
    if (ZSTD_isError(init_result)) {
        error = zstd_error(init_result);
        ZSTD_freeDStream(stream);
        return false;
    }

    while (input) {
        input.read(in_buffer.data(), static_cast<std::streamsize>(in_buffer.size()));
        const std::streamsize read_count = input.gcount();
        if (read_count <= 0) {
            break;
        }

        ZSTD_inBuffer input_buffer{in_buffer.data(), static_cast<std::size_t>(read_count), 0};
        while (input_buffer.pos < input_buffer.size) {
            ZSTD_outBuffer output_buffer{out_buffer.data(), out_buffer.size(), 0};
            const std::size_t result = ZSTD_decompressStream(stream, &output_buffer, &input_buffer);
            if (ZSTD_isError(result)) {
                error = zstd_error(result);
                ZSTD_freeDStream(stream);
                return false;
            }

            const auto* begin = reinterpret_cast<const std::byte*>(out_buffer.data());
            output.insert(output.end(), begin, begin + output_buffer.pos);
        }
    }

    if (!input.eof() && input.fail()) {
        error = "failed while reading compressed file";
        ZSTD_freeDStream(stream);
        return false;
    }

    ZSTD_freeDStream(stream);
    return true;
}

}
