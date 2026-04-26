#pragma once

#include "mantis/codecs/codec.hpp"

namespace mantis::codecs {

class XzCodec : public CompressionCodec {
public:
    std::string name() const override { return "xz"; }
    std::string extension() const override { return ".xz"; }

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

}
