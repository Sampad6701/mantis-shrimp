#include "mantis/codecs/registry.hpp"
#include "brotli_codec.hpp"
#include "lz4_codec.hpp"
#include "xz_codec.hpp"
#include "zip_codec.hpp"
#include "zstd_codec.hpp"
#include "gzip_codec.hpp"
#include <algorithm>

namespace mantis::codecs {

CodecRegistry& CodecRegistry::instance() {
    static CodecRegistry registry;
    return registry;
}

CodecRegistry::CodecRegistry() {
    registerCodec(std::make_unique<ZstdCodec>());
    registerCodec(std::make_unique<GzipCodec>());
    registerCodec(std::make_unique<BrotliCodec>());
    registerCodec(std::make_unique<Lz4Codec>());
    registerCodec(std::make_unique<XzCodec>());
    registerCodec(std::make_unique<ZipCodec>());
}

void CodecRegistry::registerCodec(std::unique_ptr<CompressionCodec> codec) {
    if (codec) {
        codecs_[codec->name()] = std::move(codec);
    }
}

CompressionCodec* CodecRegistry::getCodec(const std::string& name) {
    auto it = codecs_.find(name);
    return it != codecs_.end() ? it->second.get() : nullptr;
}

std::vector<std::string> CodecRegistry::listAvailable() const {
    std::vector<std::string> names;
    for (const auto& [name, _] : codecs_) {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool CodecRegistry::isAvailable(const std::string& name) const {
    return codecs_.find(name) != codecs_.end();
}

}
