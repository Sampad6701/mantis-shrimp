#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <unistd.h>

#include "mantis/api.hpp"
#include "mantis/codecs/registry.hpp"
#include "mantis/core/smart_engine.hpp"

namespace {

using TestFn = bool (*)(const std::filesystem::path&, std::string&);

struct TestCase {
  const char* name;
  TestFn fn;
};

bool write_text(const std::filesystem::path& path, const std::string& text) {
  std::ofstream output(path, std::ios::binary);
  output << text;
  return static_cast<bool>(output);
}

std::vector<uint8_t> read_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return std::vector<uint8_t>(
    (std::istreambuf_iterator<char>(input)),
    std::istreambuf_iterator<char>()
  );
}

bool test_codec_registry(const std::filesystem::path& temp_root, std::string& error) {
  auto& registry = mantis::codecs::CodecRegistry::instance();
  auto available = registry.listAvailable();

  if (available.size() != 6) {
    error = "Expected 6 codecs, got " + std::to_string(available.size());
    return false;
  }

  if (!registry.isAvailable("zstd") || !registry.isAvailable("gzip") ||
      !registry.isAvailable("brotli") || !registry.isAvailable("lz4") ||
      !registry.isAvailable("xz") || !registry.isAvailable("zip")) {
    error = "Not all expected codecs are registered";
    return false;
  }

  return true;
}

bool test_zstd_roundtrip(const std::filesystem::path& temp_root, std::string& error) {
  auto& registry = mantis::codecs::CodecRegistry::instance();
  auto codec = registry.getCodec("zstd");

  if (!codec) {
    error = "Failed to get zstd codec";
    return false;
  }

  const auto input = temp_root / "test.txt";
  const auto compressed = temp_root / "test.txt.zst";
  const auto decompressed = temp_root / "test.txt.out";

  if (!write_text(input, "Zstandard compression test\n")) {
    error = "Failed to create input file";
    return false;
  }

  if (!codec->compress(input, compressed, 6, error)) {
    return false;
  }

  if (!codec->decompress(compressed, decompressed, error)) {
    return false;
  }

  auto original = read_file(input);
  auto result = read_file(decompressed);

  if (original != result) {
    error = "Zstandard roundtrip mismatch";
    return false;
  }

  return true;
}

bool test_gzip_roundtrip(const std::filesystem::path& temp_root, std::string& error) {
  auto& registry = mantis::codecs::CodecRegistry::instance();
  auto codec = registry.getCodec("gzip");

  if (!codec) {
    error = "Failed to get gzip codec";
    return false;
  }

  const auto input = temp_root / "test_gz.txt";
  const auto compressed = temp_root / "test_gz.txt.gz";
  const auto decompressed = temp_root / "test_gz.txt.out";

  if (!write_text(input, "Gzip compression test\n")) {
    error = "Failed to create input file";
    return false;
  }

  if (!codec->compress(input, compressed, 6, error)) {
    return false;
  }

  if (!codec->decompress(compressed, decompressed, error)) {
    return false;
  }

  auto original = read_file(input);
  auto result = read_file(decompressed);

  if (original != result) {
    error = "Gzip roundtrip mismatch";
    return false;
  }

  return true;
}

bool test_brotli_roundtrip(const std::filesystem::path& temp_root, std::string& error) {
  auto& registry = mantis::codecs::CodecRegistry::instance();
  auto codec = registry.getCodec("brotli");

  if (!codec) {
    error = "Failed to get brotli codec";
    return false;
  }

  const auto input = temp_root / "test_br.txt";
  const auto compressed = temp_root / "test_br.txt.br";
  const auto decompressed = temp_root / "test_br.txt.out";

  if (!write_text(input, "Brotli compression test\n")) {
    error = "Failed to create input file";
    return false;
  }

  if (!codec->compress(input, compressed, 6, error)) {
    return false;
  }

  if (!codec->decompress(compressed, decompressed, error)) {
    return false;
  }

  auto original = read_file(input);
  auto result = read_file(decompressed);

  if (original != result) {
    error = "Brotli roundtrip mismatch";
    return false;
  }

  return true;
}

bool test_lz4_roundtrip(const std::filesystem::path& temp_root, std::string& error) {
  auto& registry = mantis::codecs::CodecRegistry::instance();
  auto codec = registry.getCodec("lz4");

  if (!codec) {
    error = "Failed to get lz4 codec";
    return false;
  }

  const auto input = temp_root / "test_lz4.txt";
  const auto compressed = temp_root / "test_lz4.txt.lz4";
  const auto decompressed = temp_root / "test_lz4.txt.out";

  if (!write_text(input, "LZ4 compression test data\n")) {
    error = "Failed to create input file";
    return false;
  }

  if (!codec->compress(input, compressed, 3, error)) {
    return false;
  }

  if (!codec->decompress(compressed, decompressed, error)) {
    return false;
  }

  auto original = read_file(input);
  auto result = read_file(decompressed);

  if (original != result) {
    error = "LZ4 roundtrip mismatch";
    return false;
  }

  return true;
}

bool test_xz_roundtrip(const std::filesystem::path& temp_root, std::string& error) {
  auto& registry = mantis::codecs::CodecRegistry::instance();
  auto codec = registry.getCodec("xz");

  if (!codec) {
    error = "Failed to get xz codec";
    return false;
  }

  const auto input = temp_root / "test_xz.txt";
  const auto compressed = temp_root / "test_xz.txt.xz";
  const auto decompressed = temp_root / "test_xz.txt.out";

  if (!write_text(input, "XZ compression test\n")) {
    error = "Failed to create input file";
    return false;
  }

  if (!codec->compress(input, compressed, 3, error)) {
    return false;
  }

  if (!codec->decompress(compressed, decompressed, error)) {
    return false;
  }

  auto original = read_file(input);
  auto result = read_file(decompressed);

  if (original != result) {
    error = "XZ roundtrip mismatch";
    return false;
  }

  return true;
}

bool test_zip_roundtrip(const std::filesystem::path& temp_root, std::string& error) {
  auto& registry = mantis::codecs::CodecRegistry::instance();
  auto codec = registry.getCodec("zip");

  if (!codec) {
    error = "Failed to get zip codec";
    return false;
  }

  const auto input = temp_root / "test_zip.txt";
  const auto compressed = temp_root / "test_zip.txt.zip";
  const auto decompressed = temp_root / "test_zip.txt.out";

  if (!write_text(input, "ZIP compression test\n")) {
    error = "Failed to create input file";
    return false;
  }

  if (!codec->compress(input, compressed, 6, error)) {
    return false;
  }

  if (!codec->decompress(compressed, decompressed, error)) {
    return false;
  }

  auto original = read_file(input);
  auto result = read_file(decompressed);

  if (original != result) {
    error = "ZIP roundtrip mismatch";
    return false;
  }

  return true;
}

bool test_smart_engine_benchmark(const std::filesystem::path& temp_root, std::string& error) {
  const auto input = temp_root / "benchmark.txt";

  if (!write_text(input, "This is a test file for benchmarking all compression algorithms\n")) {
    error = "Failed to create input file";
    return false;
  }

  auto stats = mantis::core::SmartEngine::instance().benchmarkAll(input);

  if (stats.size() != 6) {
    error = "Expected 6 codec stats, got " + std::to_string(stats.size());
    return false;
  }

  for (const auto& stat : stats) {
    if (stat.algorithm.empty() || stat.original_size == 0) {
      error = "Invalid stat data";
      return false;
    }
  }

  return true;
}

bool test_smart_engine_auto_select(const std::filesystem::path& temp_root, std::string& error) {
  const auto input = temp_root / "auto_test.txt";

  std::string large_text;
  for (int i = 0; i < 100; ++i) {
    large_text += "This is repetitive text for compression testing.\n";
  }

  if (!write_text(input, large_text)) {
    error = "Failed to create input file";
    return false;
  }

  auto rec = mantis::core::SmartEngine::instance().autoSelect(input);

  if (rec.algorithm.empty() || rec.compression_ratio <= 0.0) {
    error = "Invalid recommendation";
    return false;
  }

  return true;
}

bool test_empty_file(const std::filesystem::path& temp_root, std::string& error) {
  auto& registry = mantis::codecs::CodecRegistry::instance();
  auto codec = registry.getCodec("zstd");

  if (!codec) {
    error = "Failed to get zstd codec";
    return false;
  }

  const auto input = temp_root / "empty.txt";
  const auto compressed = temp_root / "empty.txt.zst";
  const auto decompressed = temp_root / "empty.txt.out";

  if (!write_text(input, "")) {
    error = "Failed to create empty file";
    return false;
  }

  if (!codec->compress(input, compressed, 6, error)) {
    return false;
  }

  if (!codec->decompress(compressed, decompressed, error)) {
    return false;
  }

  auto original = read_file(input);
  auto result = read_file(decompressed);

  if (original != result) {
    error = "Empty file roundtrip mismatch";
    return false;
  }

  return true;
}

bool test_large_file(const std::filesystem::path& temp_root, std::string& error) {
  auto& registry = mantis::codecs::CodecRegistry::instance();
  auto codec = registry.getCodec("zstd");

  if (!codec) {
    error = "Failed to get zstd codec";
    return false;
  }

  const auto input = temp_root / "large.bin";
  const auto compressed = temp_root / "large.bin.zst";
  const auto decompressed = temp_root / "large.bin.out";

  std::ofstream out(input, std::ios::binary);
  if (!out) {
    error = "Failed to create large file";
    return false;
  }

  for (int i = 0; i < 100000; ++i) {
    out << "0123456789ABCDEF";
  }
  out.close();

  if (!codec->compress(input, compressed, 6, error)) {
    return false;
  }

  if (!codec->decompress(compressed, decompressed, error)) {
    return false;
  }

  auto original = read_file(input);
  auto result = read_file(decompressed);

  if (original != result) {
    error = "Large file roundtrip mismatch";
    return false;
  }

  return true;
}

bool test_directory_extract_roundtrip(const std::filesystem::path& temp_root, std::string& error) {
  const auto input_dir = temp_root / "roundtrip_dir";
  const auto nested_dir = input_dir / "nested";
  const auto archive = temp_root / "roundtrip_dir.tar.zst";
  const auto extract_dir = temp_root / "extract";

  std::error_code ec;
  std::filesystem::create_directories(nested_dir, ec);
  if (ec) {
    error = "Failed to create input directory";
    return false;
  }

  if (!write_text(input_dir / "root.txt", "root file\n") ||
      !write_text(nested_dir / "child.txt", "nested file\n")) {
    error = "Failed to create directory test files";
    return false;
  }

  ec.clear();
  std::filesystem::create_symlink("root.txt", input_dir / "root_link.txt", ec);
  const bool created_symlink = !ec;

  const auto compressed = mantis::compress(input_dir, archive, 6, "zstd");
  if (!compressed.ok) {
    error = compressed.message;
    return false;
  }

  const auto extracted = mantis::extract(archive, extract_dir);
  if (!extracted.ok) {
    error = extracted.message;
    return false;
  }

  if (read_file(input_dir / "root.txt") != read_file(extract_dir / "root.txt") ||
      read_file(nested_dir / "child.txt") != read_file(extract_dir / "nested" / "child.txt")) {
    error = "Directory extract roundtrip mismatch";
    return false;
  }

  if (created_symlink) {
    const auto extracted_link = extract_dir / "root_link.txt";
    if (!std::filesystem::is_symlink(extracted_link)) {
      error = "Extracted symlink was not restored";
      return false;
    }
    if (std::filesystem::read_symlink(extracted_link) != std::filesystem::path{"root.txt"}) {
      error = "Extracted symlink target mismatch";
      return false;
    }
  }

  return true;
}

bool test_zip_directory_roundtrip(const std::filesystem::path& temp_root, std::string& error) {
  auto& registry = mantis::codecs::CodecRegistry::instance();
  auto codec = registry.getCodec("zip");

  if (!codec) {
    error = "Failed to get zip codec";
    return false;
  }

  const auto input_dir = temp_root / "zip_dir";
  const auto nested_dir = input_dir / "nested";
  const auto archive = temp_root / "zip_dir.zip";
  const auto extract_dir = temp_root / "zip_extract";

  std::error_code ec;
  std::filesystem::create_directories(nested_dir, ec);
  if (ec) {
    error = "Failed to create zip directory fixture";
    return false;
  }

  if (!write_text(input_dir / "root.txt", "zip root\n") ||
      !write_text(nested_dir / "child.txt", "zip child\n")) {
    error = "Failed to create zip directory files";
    return false;
  }

  if (!codec->compress(input_dir, archive, 6, error)) {
    return false;
  }

  if (!codec->decompress(archive, extract_dir, error)) {
    return false;
  }

  if (read_file(input_dir / "root.txt") != read_file(extract_dir / "root.txt") ||
      read_file(nested_dir / "child.txt") != read_file(extract_dir / "nested" / "child.txt")) {
    error = "ZIP directory roundtrip mismatch";
    return false;
  }

  return true;
}

}

int main() {
  const auto temp_root =
      std::filesystem::temp_directory_path() / ("mantis_tests_" + std::to_string(::getpid()));
  std::filesystem::remove_all(temp_root);
  std::filesystem::create_directories(temp_root);

  const std::array<TestCase, 13> tests = {
      TestCase{"codec_registry", test_codec_registry},
      TestCase{"zstd_roundtrip", test_zstd_roundtrip},
      TestCase{"gzip_roundtrip", test_gzip_roundtrip},
      TestCase{"brotli_roundtrip", test_brotli_roundtrip},
      TestCase{"lz4_roundtrip", test_lz4_roundtrip},
      TestCase{"xz_roundtrip", test_xz_roundtrip},
      TestCase{"zip_roundtrip", test_zip_roundtrip},
      TestCase{"smart_engine_benchmark", test_smart_engine_benchmark},
      TestCase{"smart_engine_auto_select", test_smart_engine_auto_select},
      TestCase{"empty_file", test_empty_file},
      TestCase{"large_file", test_large_file},
      TestCase{"directory_extract_roundtrip", test_directory_extract_roundtrip},
      TestCase{"zip_directory_roundtrip", test_zip_directory_roundtrip},
  };

  int passed = 0;
  int failed = 0;

  for (const auto& [name, fn] : tests) {
    std::string error;
    const bool test_passed = fn(temp_root, error);
    std::cout << (test_passed ? "[PASS] " : "[FAIL] ") << name;
    if (!test_passed) {
      std::cout << ": " << error;
      failed++;
    } else {
      passed++;
    }
    std::cout << '\n';
  }

  std::cout << "\nTotal: " << passed << " passed, " << failed << " failed\n";

  std::filesystem::remove_all(temp_root);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
