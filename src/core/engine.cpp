#include "core/engine.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cctype>
#include <fstream>
#include <optional>
#include <sstream>
#include <span>
#include <streambuf>
#include <string_view>
#include <system_error>
#include <vector>

#include "algorithms/gzip_codec.hpp"
#include "algorithms/zstd_codec.hpp"
#include "archive/tar_writer.hpp"

namespace mantis::core {

namespace {

constexpr std::size_t kStreamChunkSize = 64 * 1024;
constexpr int kBenchmarkCompressionLevel = 3;

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
      return compress_directory_with_codec<algorithms::ZstdCompressor>(
          input_path, output_path, compression_level, error);
    case CompressionAlgorithm::Gzip:
      return compress_directory_with_codec<algorithms::GzipCompressor>(
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
      return compress_file_with_codec<algorithms::ZstdCompressor>(
          input_path, output_path, compression_level, error);
    case CompressionAlgorithm::Gzip:
      return compress_file_with_codec<algorithms::GzipCompressor>(
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
      return benchmark_file_with_codec<algorithms::ZstdCompressor>(
          input_path, compression_level, output_size, error);
    case CompressionAlgorithm::Gzip:
      return benchmark_file_with_codec<algorithms::GzipCompressor>(
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
      return benchmark_directory_with_codec<algorithms::ZstdCompressor>(
          input_path, compression_level, output_size, error);
    case CompressionAlgorithm::Gzip:
      return benchmark_directory_with_codec<algorithms::GzipCompressor>(
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

Analysis Engine::analyze(const std::filesystem::path& path) const {
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
    if (recommend_algorithm(path, analysis.kind, kBenchmarkCompressionLevel, recommendation, error)) {
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
    if (recommend_algorithm(path, analysis.kind, kBenchmarkCompressionLevel, recommendation, error)) {
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
  const Analysis analysis = analyze(path);

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
  OperationResult result;
  result.input_path = archive_path;
  result.output_path = destination;
  result.message = "extract is reserved for a later version";
  return result;
}

}  // namespace mantis::core
