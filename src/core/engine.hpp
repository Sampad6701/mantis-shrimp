#pragma once

#include <filesystem>

#include "mantis/api.hpp"

namespace mantis::core {

class Engine {
 public:
  Analysis analyze(const std::filesystem::path& path) const;
  OperationResult compress(const std::filesystem::path& path,
                           const std::filesystem::path& output_path,
                           int compression_level) const;
  OperationResult extract(const std::filesystem::path& archive_path,
                          const std::filesystem::path& destination) const;
};

}  // namespace mantis::core
