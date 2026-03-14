#include "mantis/api.hpp"

#include "core/engine.hpp"

namespace mantis {

Analysis analyze(const std::filesystem::path& path) {
  return core::Engine{}.analyze(path);
}

OperationResult compress(const std::filesystem::path& path,
                         const std::filesystem::path& output_path,
                         int compression_level) {
  return core::Engine{}.compress(path, output_path, compression_level);
}

OperationResult extract(const std::filesystem::path& archive_path,
                        const std::filesystem::path& destination) {
  return core::Engine{}.extract(archive_path, destination);
}

}  // namespace mantis
