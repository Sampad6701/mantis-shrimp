#include "gzip_codec.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <ostream>
#include <zlib.h>
#include <cstring>

namespace mantis::codecs {

namespace {

constexpr std::size_t kBufferSize = 64 * 1024;

std::string zlib_error(int code) {
    switch (code) {
        case Z_MEM_ERROR:
            return "zlib memory allocation failed";
        case Z_BUF_ERROR:
            return "zlib buffer error";
        case Z_DATA_ERROR:
            return "zlib data error";
        case Z_STREAM_ERROR:
            return "zlib stream error";
        case Z_VERSION_ERROR:
            return "zlib version mismatch";
        default:
            return "zlib error";
    }
}

}

GzipStreamCompressor::GzipStreamCompressor(std::ostream& output, int compression_level)
    : output_(output), stream_(new z_stream{}), out_buffer_(kBufferSize) {
    auto* stream = static_cast<z_stream*>(stream_);
    const int level = compression_level < 0 ? Z_DEFAULT_COMPRESSION
                                            : std::min(std::max(compression_level, 0), 9);
    const int init_result = deflateInit2(
        stream, level, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
    initialized_ = init_result == Z_OK;
}

GzipStreamCompressor::~GzipStreamCompressor() {
    end_stream();
    delete static_cast<z_stream*>(stream_);
}

bool GzipStreamCompressor::set_error(int code, std::string& error) {
    error = zlib_error(code);
    return false;
}

bool GzipStreamCompressor::write_output_chunk(std::size_t produced, std::string& error) {
    output_.write(reinterpret_cast<const char*>(out_buffer_.data()),
                  static_cast<std::streamsize>(produced));
    if (!output_) {
        error = "failed to write compressed output";
        return false;
    }

    return true;
}

bool GzipStreamCompressor::deflate_bytes(const std::byte* input,
                                         std::size_t input_size,
                                         int flush_mode,
                                         std::string& error) {
    if (!initialized_) {
        error = "failed to initialize gzip compressor";
        return false;
    }

    auto* stream = static_cast<z_stream*>(stream_);
    stream->next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(input));
    stream->avail_in = static_cast<uInt>(input_size);

    do {
        stream->next_out = out_buffer_.data();
        stream->avail_out = static_cast<uInt>(out_buffer_.size());

        const int result = deflate(stream, flush_mode);
        if (result != Z_OK && result != Z_STREAM_END) {
            return set_error(result, error);
        }

        const std::size_t produced = out_buffer_.size() - stream->avail_out;
        if (produced > 0 && !write_output_chunk(produced, error)) {
            return false;
        }

        if (flush_mode == Z_FINISH && result == Z_STREAM_END) {
            return true;
        }
    } while (stream->avail_in > 0 || stream->avail_out == 0);

    return flush_mode != Z_FINISH;
}

bool GzipStreamCompressor::write(std::span<const std::byte> input, std::string& error) {
    if (finished_) {
        error = "gzip stream already finished";
        return false;
    }

    return deflate_bytes(input.data(), input.size(), Z_NO_FLUSH, error);
}

bool GzipStreamCompressor::end_stream() {
    if (!initialized_) {
        return false;
    }

    initialized_ = false;
    return deflateEnd(static_cast<z_stream*>(stream_)) == Z_OK;
}

bool GzipStreamCompressor::finish(std::string& error) {
    if (finished_) {
        return true;
    }

    if (!deflate_bytes(nullptr, 0, Z_FINISH, error)) {
        return false;
    }

    finished_ = true;
    end_stream();
    return true;
}

bool GzipCodec::compress(
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

        z_stream stream{};
        stream.avail_in = input_buffer.size();
        stream.next_in = input_buffer.data();

        if (deflateInit2(
            &stream, level, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY
        ) != Z_OK) {
            error = "Failed to initialize compression";
            return false;
        }

        std::vector<uint8_t> compressed_buffer(deflateBound(&stream, input_buffer.size()));
        stream.avail_out = compressed_buffer.size();
        stream.next_out = compressed_buffer.data();

        if (deflate(&stream, Z_FINISH) != Z_STREAM_END) {
            deflateEnd(&stream);
            error = "Compression failed";
            return false;
        }

        size_t compressed_size = stream.total_out;
        deflateEnd(&stream);

        std::ofstream out(output, std::ios::binary);
        if (!out) {
            error = "Failed to open output file";
            return false;
        }

        out.write(reinterpret_cast<const char*>(compressed_buffer.data()), compressed_size);
        out.close();

        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

bool GzipCodec::decompress(
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

        z_stream stream{};
        stream.avail_in = compressed_buffer.size();
        stream.next_in = compressed_buffer.data();

        if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) {
            error = "Failed to initialize decompression";
            return false;
        }

        std::vector<uint8_t> decompressed_buffer;
        const size_t chunk_size = 65536;
        uint8_t chunk[chunk_size];

        int ret = Z_OK;
        while (ret != Z_STREAM_END) {
            stream.avail_out = chunk_size;
            stream.next_out = chunk;

            ret = inflate(&stream, Z_NO_FLUSH);
            if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR) {
                inflateEnd(&stream);
                error = "Decompression failed";
                return false;
            }

            size_t have = chunk_size - stream.avail_out;
            decompressed_buffer.insert(
                decompressed_buffer.end(),
                chunk,
                chunk + have
            );
        }

        inflateEnd(&stream);

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

CompressionStats GzipCodec::stats(const fs::path& input, int level) {
    CompressionStats stats;
    stats.algorithm = name();
    stats.original_size = fs::file_size(input);
    stats.compression_level = level;

    fs::path temp_output = input.string() + ".gz.tmp";
    std::string error;

    if (compress(input, temp_output, level, error)) {
        stats.compressed_size = fs::file_size(temp_output);
        stats.compression_ratio = 100.0 * (1.0 - (double)stats.compressed_size / stats.original_size);
        fs::remove(temp_output);
    }

    return stats;
}

bool decompress_gzip_file(const std::filesystem::path& archive_path,
                          std::vector<std::byte>& output,
                          std::string& error) {
    std::ifstream input(archive_path, std::ios::binary);
    if (!input) {
        error = "failed to open compressed file";
        return false;
    }

    z_stream stream{};
    if (inflateInit2(&stream, 15 + 16) != Z_OK) {
        error = "failed to initialize gzip decompressor";
        return false;
    }

    std::array<unsigned char, kBufferSize> in_buffer{};
    std::array<unsigned char, kBufferSize> out_buffer{};
    bool reached_stream_end = false;

    while (input) {
        input.read(reinterpret_cast<char*>(in_buffer.data()),
                   static_cast<std::streamsize>(in_buffer.size()));
        const std::streamsize read_count = input.gcount();
        if (read_count <= 0) {
            break;
        }

        stream.next_in = in_buffer.data();
        stream.avail_in = static_cast<uInt>(read_count);

        do {
            stream.next_out = out_buffer.data();
            stream.avail_out = static_cast<uInt>(out_buffer.size());

            const int result = inflate(&stream, Z_NO_FLUSH);
            if (result != Z_OK && result != Z_STREAM_END) {
                error = zlib_error(result);
                inflateEnd(&stream);
                return false;
            }

            const std::size_t produced = out_buffer.size() - stream.avail_out;
            const auto* begin = reinterpret_cast<const std::byte*>(out_buffer.data());
            output.insert(output.end(), begin, begin + produced);

            if (result == Z_STREAM_END) {
                reached_stream_end = true;
                break;
            }
        } while (stream.avail_in > 0);

        if (reached_stream_end) {
            break;
        }
    }

    if (!input.eof() && input.fail()) {
        error = "failed while reading compressed file";
        inflateEnd(&stream);
        return false;
    }

    inflateEnd(&stream);
    if (!reached_stream_end) {
        error = "gzip stream ended unexpectedly";
        return false;
    }

    return true;
}

}
