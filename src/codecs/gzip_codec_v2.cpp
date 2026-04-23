#include "gzip_codec_v2.hpp"
#include <fstream>
#include <zlib.h>
#include <cstring>

namespace mantis::codecs {

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

}
