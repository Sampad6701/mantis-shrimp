#include "mantis/core/smart_engine.hpp"
#include <algorithm>

namespace mantis::core {

SmartEngine& SmartEngine::instance() {
    static SmartEngine engine;
    return engine;
}

SmartEngine::SmartEngine() : registry_(mantis::codecs::CodecRegistry::instance()) {}

CompressionRecommendation SmartEngine::autoSelect(const fs::path& input) {
    auto stats = benchmarkAll(input);

    if (stats.empty()) {
        return {"zstd", 0.0, 0, "Default (no codecs available)"};
    }

    auto best = std::max_element(
        stats.begin(),
        stats.end(),
        [](const auto& a, const auto& b) {
            return a.compression_ratio < b.compression_ratio;
        }
    );

    return {
        best->algorithm,
        best->compression_ratio,
        best->compressed_size,
        "Best compression ratio"
    };
}

std::vector<mantis::codecs::CompressionStats> SmartEngine::benchmarkAll(
    const fs::path& input
) {
    std::vector<mantis::codecs::CompressionStats> results;

    for (const auto& codec_name : listCodecs()) {
        auto codec = registry_.getCodec(codec_name);
        if (codec) {
            results.push_back(codec->stats(input, 6));
        }
    }

    return results;
}

std::vector<std::string> SmartEngine::listCodecs() const {
    return registry_.listAvailable();
}

}
