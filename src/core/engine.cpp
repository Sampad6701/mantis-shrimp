#include "core/engine.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cctype>
#include <cstring>
#include <fstream>
#include <optional>
#include <sstream>
#include <span>
#include <streambuf>
#include <string_view>
#include <system_error>
#include <vector>

#include "archive/tar_writer.hpp"
#include "codecs/gzip_codec.hpp"
#include "codecs/zstd_codec.hpp"

namespace mantis::core {

namespace {

constexpr std::size_t kStreamChunkSize = 64 * 1024;

enum class CompressionAlgorithm {
  Auto,
  Zstd,
  Gzip,
  Store,
};

struct AlgorithmRecommendation {
  CompressionAlgorithm algorithm{CompressionAlgorithm::Zstd};
  std::string reason;
};

OperationResult make_error(const std::filesystem::path& input_path, std::string message) {
  return OperationResult{
      .ok = false,
      .input_path = input_path,
      .algorithm = {},
      .message = std::move(message),
  };
}

std::string to_lower_ascii(std::string_view value) {
  std::string lowered(value);
  std::transform(lowered.begin(),
                 lowered.end(),
                 lowered.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return lowered;
}

std::optional<CompressionAlgorithm> parse_algorithm(std::string_view input) {
  const std::string normalized = to_lower_ascii(input);
  if (normalized == "auto") {
    return CompressionAlgorithm::Auto;
  }
  if (normalized == "zstd") {
    return CompressionAlgorithm::Zstd;
  }
  if (normalized == "gzip") {
    return CompressionAlgorithm::Gzip;
  }
  if (normalized == "store") {
    return CompressionAlgorithm::Store;
  }

  return std::nullopt;
}

std::string_view algorithm_name(CompressionAlgorithm algorithm) {
  switch (algorithm) {
    case CompressionAlgorithm::Auto:
      return "auto";
    case CompressionAlgorithm::Zstd:
      return "zstd";
    case CompressionAlgorithm::Gzip:
      return "gzip";
    case CompressionAlgorithm::Store:
      return "store";
  }

  return "unknown";
}

class CountingStreamBuf final : public std::streambuf {
 public:
  std::uintmax_t bytes_written() const {
    return bytes_written_;
  }

 protected:
  std::streamsize xsputn(const char_type*, std::streamsize count) override {
    if (count > 0) {
      bytes_written_ += static_cast<std::uintmax_t>(count);
    }
    return count;
  }

  int_type overflow(int_type ch) override {
    if (!traits_type::eq_int_type(ch, traits_type::eof())) {
      ++bytes_written_;
      return ch;
    }
    return traits_type::not_eof(ch);
  }

 private:
  std::uintmax_t bytes_written_{0};
};

class CountingOStream final : public std::ostream {
 public:
  CountingOStream() : std::ostream(&buffer_) {}

  std::uintmax_t bytes_written() const {
    return buffer_.bytes_written();
  }

 private:
  CountingStreamBuf buffer_;
};

struct BenchmarkResult {
  CompressionAlgorithm algorithm{CompressionAlgorithm::Zstd};
  std::uintmax_t bytes{0};
};

int algorithm_rank(CompressionAlgorithm algorithm) {
  switch (algorithm) {
    case CompressionAlgorithm::Zstd:
      return 0;
    case CompressionAlgorithm::Gzip:
      return 1;
    case CompressionAlgorithm::Store:
      return 2;
    case CompressionAlgorithm::Auto:
      return 3;
  }

  return 4;
}

std::uintmax_t benchmark_size_for(std::span<const BenchmarkResult> results,
                                  CompressionAlgorithm algorithm) {
  const auto it = std::find_if(results.begin(), results.end(), [algorithm](const BenchmarkResult& result) {
    return result.algorithm == algorithm;
  });
  return it != results.end() ? it->bytes : 0;
}

bool has_suffix(std::string_view value, std::string_view suffix) {
  return value.size() >= suffix.size() &&
         value.substr(value.size() - suffix.size()) == suffix;
}

std::uintmax_t parse_tar_octal(const char* value, std::size_t size) {
  std::uintmax_t result = 0;
  for (std::size_t index = 0; index < size; ++index) {
    const char ch = value[index];
    if (ch == '\0' || ch == ' ') {
      break;
    }
    if (ch < '0' || ch > '7') {
      continue;
    }
    result = (result * 8) + static_cast<std::uintmax_t>(ch - '0');
  }
  return result;
}

bool is_zero_block(std::span<const std::byte> block) {
  return std::all_of(block.begin(), block.end(), [](std::byte value) {
    return value == std::byte{0};
  });
}

std::string tar_string_field(const char* value, std::size_t size) {
  const auto* end = static_cast<const char*>(std::memchr(value, '\0', size));
  return std::string(value, end == nullptr ? size : static_cast<std::size_t>(end - value));
}

bool is_safe_archive_path(const std::filesystem::path& path) {
  if (path.empty() || path.is_absolute()) {
    return false;
  }

  for (const auto& part : path) {
    if (part == "..") {
      return false;
    }
  }

  return true;
}

std::filesystem::path remove_first_path_component(const std::filesystem::path& path) {
  std::filesystem::path stripped;
  bool skipped_first = false;
  for (const auto& part : path) {
    if (!skipped_first) {
      skipped_first = true;
      continue;
    }
    stripped /= part;
  }
  return stripped;
}

bool read_file_bytes(const std::filesystem::path& path,
                     std::vector<std::byte>& output,
                     std::string& error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "failed to open archive";
    return false;
  }

  std::array<char, kStreamChunkSize> buffer{};
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize read_count = input.gcount();
    if (read_count <= 0) {
      break;
    }

    const auto* begin = reinterpret_cast<const std::byte*>(buffer.data());
    output.insert(output.end(), begin, begin + read_count);
  }

  if (!input.eof() && input.fail()) {
    error = "failed while reading archive";
    return false;
  }

  return true;
}

bool decode_archive_stream(const std::filesystem::path& archive_path,
                           std::vector<std::byte>& output,
                           std::string& algorithm,
                           std::string& error) {
  const std::string path = archive_path.string();
  if (has_suffix(path, ".zst")) {
    algorithm = "zstd";
    return codecs::decompress_zstd_file(archive_path, output, error);
  }
  if (has_suffix(path, ".gz")) {
    algorithm = "gzip";
    return codecs::decompress_gzip_file(archive_path, output, error);
  }
  if (has_suffix(path, ".tar")) {
    algorithm = "store";
    return read_file_bytes(archive_path, output, error);
  }

  error = "unsupported archive extension; expected .tar.zst, .tar.gz, or .tar";
  return false;
}

bool extract_tar_stream(std::span<const std::byte> tar_data,
                        const std::filesystem::path& destination,
                        std::string& error) {
  constexpr std::size_t kTarBlockSize = 512;
  const std::filesystem::path base = destination.empty() ? std::filesystem::path{"."} : destination;
  const bool strip_archive_root = !destination.empty();

  std::error_code ec;
  std::filesystem::create_directories(base, ec);
  if (ec) {
    error = "failed to create output directory: " + ec.message();
    return false;
  }

  std::size_t offset = 0;
  while (offset + kTarBlockSize <= tar_data.size()) {
    const auto block = tar_data.subspan(offset, kTarBlockSize);
    if (is_zero_block(block)) {
      return true;
    }

    const auto* header = reinterpret_cast<const char*>(block.data());
    const std::string name = tar_string_field(header, 100);
    const std::string link_target = tar_string_field(header + 157, 100);
    const std::string prefix = tar_string_field(header + 345, 155);
    const std::filesystem::path archive_path = prefix.empty() ? std::filesystem::path{name}
                                                              : std::filesystem::path{prefix} / name;
    const char typeflag = header[156];
    const std::uintmax_t file_size = parse_tar_octal(header + 124, 12);

    if (!is_safe_archive_path(archive_path)) {
      error = "unsafe archive path: " + archive_path.string();
      return false;
    }

    offset += kTarBlockSize;
    if (offset + file_size > tar_data.size()) {
      error = "tar archive ended unexpectedly";
      return false;
    }

    const std::filesystem::path relative_output_path =
        strip_archive_root ? remove_first_path_component(archive_path) : archive_path;
    if (relative_output_path.empty()) {
      const std::size_t padding = (kTarBlockSize - (file_size % kTarBlockSize)) % kTarBlockSize;
      offset += static_cast<std::size_t>(file_size) + padding;
      continue;
    }

    const std::filesystem::path output_path = base / relative_output_path;
    if (typeflag == '5' || archive_path.string().ends_with('/')) {
      std::filesystem::create_directories(output_path, ec);
      if (ec) {
        error = "failed to create directory: " + ec.message();
        return false;
      }
    } else if (typeflag == '\0' || typeflag == '0') {
      std::filesystem::create_directories(output_path.parent_path(), ec);
      if (ec) {
        error = "failed to create parent directory: " + ec.message();
        return false;
      }

      std::ofstream output(output_path, std::ios::binary);
      if (!output) {
        error = "failed to create output file";
        return false;
      }

      output.write(reinterpret_cast<const char*>(tar_data.data() + offset),
                   static_cast<std::streamsize>(file_size));
      if (!output) {
        error = "failed to write output file";
        return false;
      }
    } else if (typeflag == '2') {
      std::filesystem::create_directories(output_path.parent_path(), ec);
      if (ec) {
        error = "failed to create parent directory: " + ec.message();
        return false;
      }

      std::filesystem::remove(output_path, ec);
      if (ec && std::filesystem::exists(output_path)) {
        error = "failed to replace existing symlink path: " + ec.message();
        return false;
      }

      std::filesystem::create_symlink(link_target, output_path, ec);
      if (ec) {
        error = "failed to create symlink: " + ec.message();
        return false;
      }
    } else {
      error = "unsupported tar entry type";
      return false;
    }

    const std::size_t padding = (kTarBlockSize - (file_size % kTarBlockSize)) % kTarBlockSize;
    offset += static_cast<std::size_t>(file_size) + padding;
  }

  error = "tar archive is missing end marker";
  return false;
}

std::filesystem::path default_output_path(const std::filesystem::path& input_path,
                                          InputKind kind,
                                          CompressionAlgorithm algorithm) {
  if (kind == InputKind::Directory) {
    switch (algorithm) {
      case CompressionAlgorithm::Zstd:
        return input_path.filename().string() + ".tar.zst";
      case CompressionAlgorithm::Gzip:
        return input_path.filename().string() + ".tar.gz";
      case CompressionAlgorithm::Store:
        return input_path.filename().string() + ".tar";
      case CompressionAlgorithm::Auto:
        break;
    }
  }

  switch (algorithm) {
    case CompressionAlgorithm::Zstd:
      return input_path.string() + ".zst";
    case CompressionAlgorithm::Gzip:
      return input_path.string() + ".gz";
    case CompressionAlgorithm::Store:
      return input_path.string() + ".raw";
    case CompressionAlgorithm::Auto:
      break;
  }

  return input_path.string();
}

template <typename CompressorT>
bool compress_file_with_codec(const std::filesystem::path& input_path,
                              const std::filesystem::path& output_path,
                              int compression_level,
                              std::string& error) {
  std::ifstream input(input_path, std::ios::binary);
  if (!input) {
    error = "failed to open input file";
    return false;
  }

  std::ofstream output(output_path, std::ios::binary);
  if (!output) {
    error = "failed to open output file";
    return false;
  }

  CompressorT compressor(output, compression_level);
  std::array<char, kStreamChunkSize> buffer{};

  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize read_count = input.gcount();
    if (read_count <= 0) {
      break;
    }

    const auto bytes = std::as_bytes(std::span(buffer.data(), static_cast<std::size_t>(read_count)));
    if (!compressor.write(bytes, error)) {
      return false;
    }
  }

  if (!input.eof() && input.fail()) {
    error = "failed while reading input file";
    return false;
  }

  return compressor.finish(error);
}

bool compress_file_store(const std::filesystem::path& input_path,
                         const std::filesystem::path& output_path,
                         std::string& error) {
  std::ifstream input(input_path, std::ios::binary);
  if (!input) {
    error = "failed to open input file";
    return false;
  }

  std::ofstream output(output_path, std::ios::binary);
  if (!output) {
    error = "failed to open output file";
    return false;
  }

  std::array<char, kStreamChunkSize> buffer{};
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize read_count = input.gcount();
    if (read_count <= 0) {
      break;
    }

    output.write(buffer.data(), read_count);
    if (!output) {
      error = "failed to write output file";
      return false;
    }
  }

  if (!input.eof() && input.fail()) {
    error = "failed while reading input file";
    return false;
  }

  return true;
}

template <typename CompressorT>
bool compress_directory_with_codec(const std::filesystem::path& input_path,
                                   const std::filesystem::path& output_path,
                                   int compression_level,
                                   std::string& error) {
  std::ofstream output(output_path, std::ios::binary);
  if (!output) {
    error = "failed to open output file";
    return false;
  }

  CompressorT compressor(output, compression_level);
  archive::TarWriter writer([&compressor](std::span<const std::byte> bytes, std::string& sink_error) {
    return compressor.write(bytes, sink_error);
  });

  if (!writer.add_directory_tree(input_path, error)) {
    return false;
  }
  if (!writer.finish(error)) {
    return false;
  }

  return compressor.finish(error);
}

bool compress_directory_store(const std::filesystem::path& input_path,
                              const std::filesystem::path& output_path,
                              std::string& error) {
  std::ofstream output(output_path, std::ios::binary);
  if (!output) {
    error = "failed to open output file";
    return false;
  }

  archive::TarWriter writer([&output](std::span<const std::byte> bytes, std::string& sink_error) {
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) {
      sink_error = "failed to write archive output";
      return false;
    }
    return true;
  });

  if (!writer.add_directory_tree(input_path, error)) {
    return false;
  }

  return writer.finish(error);
}

bool compress_directory_streaming(const std::filesystem::path& input_path,
                                  const std::filesystem::path& output_path,
                                  int compression_level,
                                  CompressionAlgorithm algorithm,
                                  std::string& error) {
  switch (algorithm) {
    case CompressionAlgorithm::Zstd:
      return compress_directory_with_codec<codecs::ZstdStreamCompressor>(
          input_path, output_path, compression_level, error);
    case CompressionAlgorithm::Gzip:
      return compress_directory_with_codec<codecs::GzipStreamCompressor>(
          input_path, output_path, compression_level, error);
    case CompressionAlgorithm::Store:
      return compress_directory_store(input_path, output_path, error);
    case CompressionAlgorithm::Auto:
      error = "internal error: unresolved compression algorithm";
      return false;
  }

  error = "internal error: unsupported compression algorithm";
  return false;
}

bool compress_file_streaming(const std::filesystem::path& input_path,
                             const std::filesystem::path& output_path,
                             int compression_level,
                             CompressionAlgorithm algorithm,
                             std::string& error) {
  switch (algorithm) {
    case CompressionAlgorithm::Zstd:
      return compress_file_with_codec<codecs::ZstdStreamCompressor>(
          input_path, output_path, compression_level, error);
    case CompressionAlgorithm::Gzip:
      return compress_file_with_codec<codecs::GzipStreamCompressor>(
          input_path, output_path, compression_level, error);
    case CompressionAlgorithm::Store:
      return compress_file_store(input_path, output_path, error);
    case CompressionAlgorithm::Auto:
      error = "internal error: unresolved compression algorithm";
      return false;
  }

  error = "internal error: unsupported compression algorithm";
  return false;
}

template <typename CompressorT>
bool benchmark_file_with_codec(const std::filesystem::path& input_path,
                               int compression_level,
                               std::uintmax_t& output_size,
                               std::string& error) {
  std::ifstream input(input_path, std::ios::binary);
  if (!input) {
    error = "failed to open input file";
    return false;
  }

  CountingOStream output;
  CompressorT compressor(output, compression_level);
  std::array<char, kStreamChunkSize> buffer{};

  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize read_count = input.gcount();
    if (read_count <= 0) {
      break;
    }

    const auto bytes = std::as_bytes(std::span(buffer.data(), static_cast<std::size_t>(read_count)));
    if (!compressor.write(bytes, error)) {
      return false;
    }
  }

  if (!input.eof() && input.fail()) {
    error = "failed while reading input file";
    return false;
  }

  if (!compressor.finish(error)) {
    return false;
  }

  output_size = output.bytes_written();
  return true;
}

bool benchmark_file_store(const std::filesystem::path& input_path,
                          std::uintmax_t& output_size,
                          std::string& error) {
  std::ifstream input(input_path, std::ios::binary);
  if (!input) {
    error = "failed to open input file";
    return false;
  }

  std::array<char, kStreamChunkSize> buffer{};
  std::uintmax_t total = 0;
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize read_count = input.gcount();
    if (read_count <= 0) {
      break;
    }
    total += static_cast<std::uintmax_t>(read_count);
  }

  if (!input.eof() && input.fail()) {
    error = "failed while reading input file";
    return false;
  }

  output_size = total;
  return true;
}

template <typename CompressorT>
bool benchmark_directory_with_codec(const std::filesystem::path& input_path,
                                    int compression_level,
                                    std::uintmax_t& output_size,
                                    std::string& error) {
  CountingOStream output;
  CompressorT compressor(output, compression_level);
  archive::TarWriter writer([&compressor](std::span<const std::byte> bytes, std::string& sink_error) {
    return compressor.write(bytes, sink_error);
  });

  if (!writer.add_directory_tree(input_path, error)) {
    return false;
  }
  if (!writer.finish(error)) {
    return false;
  }
  if (!compressor.finish(error)) {
    return false;
  }

  output_size = output.bytes_written();
  return true;
}

bool benchmark_directory_store(const std::filesystem::path& input_path,
                               std::uintmax_t& output_size,
                               std::string& error) {
  CountingOStream output;
  archive::TarWriter writer([&output](std::span<const std::byte> bytes, std::string& sink_error) {
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) {
      sink_error = "failed to write benchmark tar stream";
      return false;
    }
    return true;
  });

  if (!writer.add_directory_tree(input_path, error)) {
    return false;
  }
  if (!writer.finish(error)) {
    return false;
  }

  output_size = output.bytes_written();
  return true;
}

bool benchmark_file_algorithm(const std::filesystem::path& input_path,
                              int compression_level,
                              CompressionAlgorithm algorithm,
                              std::uintmax_t& output_size,
                              std::string& error) {
  switch (algorithm) {
    case CompressionAlgorithm::Zstd:
      return benchmark_file_with_codec<codecs::ZstdStreamCompressor>(
          input_path, compression_level, output_size, error);
    case CompressionAlgorithm::Gzip:
      return benchmark_file_with_codec<codecs::GzipStreamCompressor>(
          input_path, compression_level, output_size, error);
    case CompressionAlgorithm::Store:
      return benchmark_file_store(input_path, output_size, error);
    case CompressionAlgorithm::Auto:
      error = "internal error: unresolved compression algorithm";
      return false;
  }

  error = "internal error: unsupported compression algorithm";
  return false;
}

bool benchmark_directory_algorithm(const std::filesystem::path& input_path,
                                   int compression_level,
                                   CompressionAlgorithm algorithm,
                                   std::uintmax_t& output_size,
                                   std::string& error) {
  switch (algorithm) {
    case CompressionAlgorithm::Zstd:
      return benchmark_directory_with_codec<codecs::ZstdStreamCompressor>(
          input_path, compression_level, output_size, error);
    case CompressionAlgorithm::Gzip:
      return benchmark_directory_with_codec<codecs::GzipStreamCompressor>(
          input_path, compression_level, output_size, error);
    case CompressionAlgorithm::Store:
      return benchmark_directory_store(input_path, output_size, error);
    case CompressionAlgorithm::Auto:
      error = "internal error: unresolved compression algorithm";
      return false;
  }

  error = "internal error: unsupported compression algorithm";
  return false;
}

bool recommend_algorithm(const std::filesystem::path& input_path,
                         InputKind kind,
                         int compression_level,
                         AlgorithmRecommendation& recommendation,
                         std::string& error) {
  constexpr std::array<CompressionAlgorithm, 3> candidates{
      CompressionAlgorithm::Zstd,
      CompressionAlgorithm::Gzip,
      CompressionAlgorithm::Store,
  };

  std::array<BenchmarkResult, 3> results{};
  for (std::size_t index = 0; index < candidates.size(); ++index) {
    results[index].algorithm = candidates[index];
    const bool ok = kind == InputKind::File
                        ? benchmark_file_algorithm(
                              input_path, compression_level, candidates[index], results[index].bytes, error)
                        : benchmark_directory_algorithm(
                              input_path, compression_level, candidates[index], results[index].bytes, error);
    if (!ok) {
      return false;
    }
  }

  const auto best_it = std::min_element(
      results.begin(), results.end(), [](const BenchmarkResult& left, const BenchmarkResult& right) {
        if (left.bytes != right.bytes) {
          return left.bytes < right.bytes;
        }
        return algorithm_rank(left.algorithm) < algorithm_rank(right.algorithm);
      });

  recommendation.algorithm = best_it->algorithm;

  std::ostringstream reason;
  reason << "benchmark output sizes (bytes): "
         << "zstd=" << benchmark_size_for(results, CompressionAlgorithm::Zstd) << ", "
         << "gzip=" << benchmark_size_for(results, CompressionAlgorithm::Gzip) << ", "
         << "store=" << benchmark_size_for(results, CompressionAlgorithm::Store)
         << "; selected " << algorithm_name(recommendation.algorithm);
  recommendation.reason = reason.str();
  return true;
}

}  // namespace

Analysis Engine::analyze(const std::filesystem::path& path, int compression_level) const {
  Analysis analysis;
  analysis.input_path = path;

  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    analysis.kind = InputKind::Missing;
    analysis.recommended_algorithm.clear();
    analysis.recommendation_reason = "input path does not exist";
    return analysis;
  }

  if (std::filesystem::is_regular_file(path, ec)) {
    analysis.kind = InputKind::File;
    analysis.size = std::filesystem::file_size(path, ec);
    AlgorithmRecommendation recommendation;
    std::string error;
    if (recommend_algorithm(path, analysis.kind, compression_level, recommendation, error)) {
      analysis.recommended_algorithm = std::string(algorithm_name(recommendation.algorithm));
      analysis.recommendation_reason = recommendation.reason;
    } else {
      analysis.recommended_algorithm = "unavailable";
      analysis.recommendation_reason = "benchmark failed: " + error;
    }
    return analysis;
  }

  if (std::filesystem::is_directory(path, ec)) {
    analysis.kind = InputKind::Directory;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(path)) {
      analysis.entries.push_back(entry.path());
      if (entry.is_regular_file(ec)) {
        analysis.size += entry.file_size(ec);
      }
    }

    AlgorithmRecommendation recommendation;
    std::string error;
    if (recommend_algorithm(path, analysis.kind, compression_level, recommendation, error)) {
      analysis.recommended_algorithm = std::string(algorithm_name(recommendation.algorithm));
      analysis.recommendation_reason = recommendation.reason;
    } else {
      analysis.recommended_algorithm = "unavailable";
      analysis.recommendation_reason = "benchmark failed: " + error;
    }
    return analysis;
  }

  analysis.kind = InputKind::Other;
  analysis.recommended_algorithm.clear();
  analysis.recommendation_reason = "unsupported input type";
  return analysis;
}

OperationResult Engine::compress(const std::filesystem::path& path,
                                 const std::filesystem::path& output_path,
                                 int compression_level,
                                 std::string_view algorithm) const {
  const Analysis analysis = analyze(path, compression_level);

  if (analysis.kind == InputKind::Missing) {
    return make_error(path, "input path does not exist");
  }

  if (analysis.kind == InputKind::Other) {
    return make_error(path, "unsupported input type");
  }

  const auto parsed = parse_algorithm(algorithm);
  if (!parsed.has_value()) {
    return make_error(path, "unknown algorithm; supported values are: auto, zstd, gzip, store");
  }

  CompressionAlgorithm resolved_algorithm = parsed.value();
  std::string error;
  std::string selection_reason = "selected explicitly by user";
  if (resolved_algorithm == CompressionAlgorithm::Auto) {
    AlgorithmRecommendation recommendation;
    if (!recommend_algorithm(path, analysis.kind, compression_level, recommendation, error)) {
      return make_error(path, "benchmark failed: " + error);
    }
    resolved_algorithm = recommendation.algorithm;
    selection_reason = recommendation.reason;
  }

  const std::filesystem::path resolved_output =
      output_path.empty() ? default_output_path(path, analysis.kind, resolved_algorithm) : output_path;

  const bool ok = analysis.kind == InputKind::File
                      ? compress_file_streaming(path,
                                                resolved_output,
                                                compression_level,
                                                resolved_algorithm,
                                                error)
                      : compress_directory_streaming(path,
                                                     resolved_output,
                                                     compression_level,
                                                     resolved_algorithm,
                                                     error);

  return OperationResult{
      .ok = ok,
      .input_path = path,
      .output_path = resolved_output,
      .algorithm = std::string(algorithm_name(resolved_algorithm)),
      .message = ok ? "compression completed: " + selection_reason : error,
  };
}

OperationResult Engine::extract(const std::filesystem::path& archive_path,
                                const std::filesystem::path& destination) const {
  std::error_code ec;
  if (!std::filesystem::exists(archive_path, ec)) {
    return make_error(archive_path, "archive path does not exist");
  }
  if (!std::filesystem::is_regular_file(archive_path, ec)) {
    return make_error(archive_path, "archive path is not a regular file");
  }

  std::vector<std::byte> tar_data;
  std::string algorithm;
  std::string error;
  const std::filesystem::path output_path = destination.empty() ? std::filesystem::path{"."} : destination;

  const bool ok = decode_archive_stream(archive_path, tar_data, algorithm, error) &&
                  extract_tar_stream(tar_data, output_path, error);

  return OperationResult{
      .ok = ok,
      .input_path = archive_path,
      .output_path = output_path,
      .algorithm = algorithm,
      .message = ok ? "extraction completed" : error,
  };
}

}  // namespace mantis::core
