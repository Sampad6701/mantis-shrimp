#pragma once

#include <cstddef>
#include <filesystem>
#include <iosfwd>
#include <span>
#include <string>
#include <vector>

namespace mantis::algorithms {

class ZstdCompressor {
 public:
  explicit ZstdCompressor(std::ostream& output, int compression_level);
  ~ZstdCompressor();

  ZstdCompressor(const ZstdCompressor&) = delete;
  ZstdCompressor& operator=(const ZstdCompressor&) = delete;

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

bool decompress_file(const std::filesystem::path& archive_path,
                     std::vector<std::byte>& output,
                     std::string& error);

}  // namespace mantis::algorithms
