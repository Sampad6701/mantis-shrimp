#include "core/engine.hpp"

#include <array>
#include <cstddef>
#include <fstream>
#include <span>
#include <system_error>

#include "algorithms/zstd_codec.hpp"
#include "archive/tar_writer.hpp"

namespace mantis::core {

namespace {

constexpr std::size_t kStreamChunkSize = 64 * 1024;

OperationResult make_error(const std::filesystem::path& input_path, std::string message) {
  return OperationResult{
      .ok = false,
      .input_path = input_path,
      .message = std::move(message),
  };
}

std::filesystem::path default_output_path(const std::filesystem::path& input_path, InputKind kind) {
  if (kind == InputKind::Directory) {
    return input_path.filename().string() + ".tar.zst";
  }

  return input_path.string() + ".zst";
}

bool compress_file_streaming(const std::filesystem::path& input_path,
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

  algorithms::ZstdCompressor compressor(output, compression_level);
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

bool compress_directory_streaming(const std::filesystem::path& input_path,
                                  const std::filesystem::path& output_path,
                                  int compression_level,
                                  std::string& error) {
  std::ofstream output(output_path, std::ios::binary);
  if (!output) {
    error = "failed to open output file";
    return false;
  }

  algorithms::ZstdCompressor compressor(output, compression_level);
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

}  // namespace

Analysis Engine::analyze(const std::filesystem::path& path) const {
  Analysis analysis;
  analysis.input_path = path;

  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    analysis.kind = InputKind::Missing;
    return analysis;
  }

  if (std::filesystem::is_regular_file(path, ec)) {
    analysis.kind = InputKind::File;
    analysis.size = std::filesystem::file_size(path, ec);
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

    return analysis;
  }

  analysis.kind = InputKind::Other;
  return analysis;
}

OperationResult Engine::compress(const std::filesystem::path& path,
                                 const std::filesystem::path& output_path,
                                 int compression_level) const {
  const Analysis analysis = analyze(path);

  if (analysis.kind == InputKind::Missing) {
    return make_error(path, "input path does not exist");
  }

  if (analysis.kind == InputKind::Other) {
    return make_error(path, "unsupported input type");
  }

  const std::filesystem::path resolved_output =
      output_path.empty() ? default_output_path(path, analysis.kind) : output_path;

  std::string error;
  const bool ok = analysis.kind == InputKind::File
                      ? compress_file_streaming(path, resolved_output, compression_level, error)
                      : compress_directory_streaming(path, resolved_output, compression_level, error);

  return OperationResult{
      .ok = ok,
      .input_path = path,
      .output_path = resolved_output,
      .message = ok ? "compression completed" : error,
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
