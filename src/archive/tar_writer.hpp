#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <span>
#include <string>

namespace mantis::archive {

class TarWriter {
 public:
  using Sink = std::function<bool(std::span<const std::byte>, std::string&)>;

  explicit TarWriter(Sink sink);

  bool add_directory_tree(const std::filesystem::path& root, std::string& error);
  bool finish(std::string& error);

 private:
  Sink sink_;

  bool add_directory_entry(const std::string& archive_name, std::string& error);
  bool add_symlink_entry(const std::string& archive_name,
                         const std::filesystem::path& link_target,
                         std::string& error);
  bool add_file_entry(const std::filesystem::path& disk_path,
                      const std::string& archive_name,
                      std::string& error);
  bool write_block(const void* data, std::size_t size, std::string& error);
  bool write_padding(std::size_t size, std::string& error);
};

}  // namespace mantis::archive
