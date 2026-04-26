#pragma once

#include "mantis/codecs/codec.hpp"

#include <cstddef>
#include <filesystem>
#include <iosfwd>
#include <span>
#include <string>
#include <vector>

namespace mantis::codecs {

class GzipStreamCompressor {
public:
    explicit GzipStreamCompressor(std::ostream& output, int compression_level);
    ~GzipStreamCompressor();

    GzipStreamCompressor(const GzipStreamCompressor&) = delete;
    GzipStreamCompressor& operator=(const GzipStreamCompressor&) = delete;

    bool write(std::span<const std::byte> input, std::string& error);
    bool finish(std::string& error);

private:
    bool deflate_bytes(const std::byte* input,
                       std::size_t input_size,
                       int flush_mode,
                       std::string& error);
    bool write_output_chunk(std::size_t produced, std::string& error);
    bool set_error(int code, std::string& error);
    bool end_stream();

    std::ostream& output_;
    void* stream_;
    std::vector<unsigned char> out_buffer_;
    bool initialized_{false};
    bool finished_{false};
};

class GzipCodec : public CompressionCodec {
public:
    std::string name() const override { return "gzip"; }
    std::string extension() const override { return ".gz"; }

    bool compress(
        const fs::path& input,
        const fs::path& output,
        int level,
        std::string& error
    ) override;

    bool decompress(
        const fs::path& input,
        const fs::path& output,
        std::string& error
    ) override;

    CompressionStats stats(const fs::path& input, int level) override;
};

bool decompress_gzip_file(const std::filesystem::path& archive_path,
                          std::vector<std::byte>& output,
                          std::string& error);

}
