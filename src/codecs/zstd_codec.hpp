#pragma once

#include "mantis/codecs/codec.hpp"

#include <cstddef>
#include <filesystem>
#include <iosfwd>
#include <span>
#include <string>
#include <vector>

namespace mantis::codecs {

class ZstdStreamCompressor {
public:
    explicit ZstdStreamCompressor(std::ostream& output, int compression_level);
    ~ZstdStreamCompressor();

    ZstdStreamCompressor(const ZstdStreamCompressor&) = delete;
    ZstdStreamCompressor& operator=(const ZstdStreamCompressor&) = delete;

    bool write(std::span<const std::byte> input, std::string& error);
    bool finish(std::string& error);

private:
    bool drain(const void* input_data,
               std::size_t input_size,
               bool end_frame,
               std::string& error);

    std::ostream& output_;
    void* stream_;
    std::vector<char> out_buffer_;
    bool finished_{false};
};

class ZstdCodec : public CompressionCodec {
public:
    std::string name() const override { return "zstd"; }
    std::string extension() const override { return ".zst"; }

    bool compress(
        const fs::path& input,
        const fs::path& output,
        int level,
        std::string& error
    ) override;

    bool compress(
        const fs::path& input,
        const fs::path& output,
        int level,
        int threads,
        std::string& error
    ) override;

    bool decompress(
        const fs::path& input,
        const fs::path& output,
        std::string& error
    ) override;

    CompressionStats stats(const fs::path& input, int level) override;
};

bool decompress_zstd_file(const std::filesystem::path& archive_path,
                          std::vector<std::byte>& output,
                          std::string& error);

}
