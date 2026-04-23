#pragma once

#include <cstddef>
#include <filesystem>
#include <iosfwd>
#include <span>
#include <string>
#include <vector>

namespace mantis::algorithms {

class GzipCompressor {
 public:
  explicit GzipCompressor(std::ostream& output, int compression_level);
  ~GzipCompressor();

  GzipCompressor(const GzipCompressor&) = delete;
  GzipCompressor& operator=(const GzipCompressor&) = delete;

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

bool decompress_gzip_file(const std::filesystem::path& archive_path,
                          std::vector<std::byte>& output,
                          std::string& error);

}  // namespace mantis::algorithms
