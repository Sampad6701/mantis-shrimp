#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include <unistd.h>

#include "algorithms/zstd_codec.hpp"
#include "mantis/api.hpp"

namespace {

using TestFn = bool (*)(const std::filesystem::path&, std::string&);

struct TarEntry {
  bool is_directory{false};
  std::vector<std::byte> data;
};

struct TestCase {
  const char* name;
  TestFn fn;
};

std::string read_octal(const char* data, std::size_t width) {
  std::string result(data, width);
  const auto end = result.find('\0');
  if (end != std::string::npos) {
    result.resize(end);
  }
  result.erase(std::remove(result.begin(), result.end(), ' '), result.end());
  return result;
}

std::uintmax_t parse_octal(const char* data, std::size_t width) {
  const std::string text = read_octal(data, width);
  return text.empty() ? 0 : std::stoull(text, nullptr, 8);
}

std::string tar_name(const std::byte* block) {
  const auto* bytes = reinterpret_cast<const char*>(block);
  std::string name(bytes, 100);
  name.resize(name.find('\0'));

  std::string prefix(bytes + 345, 155);
  const auto prefix_end = prefix.find('\0');
  if (prefix_end != std::string::npos) {
    prefix.resize(prefix_end);
  }

  return prefix.empty() ? name : prefix + "/" + name;
}

std::map<std::string, TarEntry> parse_tar(const std::vector<std::byte>& buffer) {
  std::map<std::string, TarEntry> entries;
  std::size_t offset = 0;

  while (offset + 512 <= buffer.size()) {
    const std::byte* block = buffer.data() + offset;
    const bool zero_block =
        std::all_of(block, block + 512, [](std::byte byte) { return byte == std::byte{0}; });
    if (zero_block) {
      break;
    }

    const std::string name = tar_name(block);
    const bool is_directory = reinterpret_cast<const char*>(block)[156] == '5';
    const std::uintmax_t size = parse_octal(reinterpret_cast<const char*>(block) + 124, 12);
    offset += 512;

    TarEntry entry;
    entry.is_directory = is_directory;
    if (!is_directory) {
      entry.data.assign(buffer.begin() + static_cast<std::ptrdiff_t>(offset),
                        buffer.begin() + static_cast<std::ptrdiff_t>(offset + size));
    }

    entries.emplace(name, std::move(entry));
    offset += ((size + 511) / 512) * 512;
  }

  return entries;
}

std::vector<std::byte> slurp_bytes(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  std::vector<char> data((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  const auto* begin = reinterpret_cast<const std::byte*>(data.data());
  return std::vector<std::byte>(begin, begin + data.size());
}

bool write_text(const std::filesystem::path& path, const std::string& text) {
  std::ofstream output(path, std::ios::binary);
  output << text;
  return static_cast<bool>(output);
}

bool test_file_roundtrip(const std::filesystem::path& temp_root, std::string& error) {
  const auto input = temp_root / "hello.txt";
  const auto archive = temp_root / "hello.txt.zst";
  if (!write_text(input, "mantis shrimp file compression\n")) {
    error = "failed to create input file";
    return false;
  }

  const auto result = mantis::compress(input, archive, 3);
  if (!result.ok) {
    error = result.message;
    return false;
  }

  std::vector<std::byte> decoded;
  if (!mantis::algorithms::decompress_file(archive, decoded, error)) {
    return false;
  }

  const auto original = slurp_bytes(input);
  if (decoded != original) {
    error = "decoded file does not match original";
    return false;
  }

  return true;
}

bool test_directory_packaging(const std::filesystem::path& temp_root, std::string& error) {
  const auto dir = temp_root / "payload";
  std::filesystem::create_directories(dir / "nested");
  if (!write_text(dir / "top.txt", "top-level\n") ||
      !write_text(dir / "nested" / "deep.txt", "deep file\n")) {
    error = "failed to create directory payload";
    return false;
  }

  const auto archive = temp_root / "payload.tar.zst";
  const auto result = mantis::compress(dir, archive, 3);
  if (!result.ok) {
    error = result.message;
    return false;
  }

  std::vector<std::byte> decoded;
  if (!mantis::algorithms::decompress_file(archive, decoded, error)) {
    return false;
  }

  const auto entries = parse_tar(decoded);
  if (!entries.contains("payload/") || !entries.contains("payload/nested/") ||
      !entries.contains("payload/top.txt") || !entries.contains("payload/nested/deep.txt")) {
    error = "tar archive is missing expected entries";
    return false;
  }

  const auto expected = slurp_bytes(dir / "nested" / "deep.txt");
  if (entries.at("payload/nested/deep.txt").data != expected) {
    error = "nested file contents did not survive archive packaging";
    return false;
  }

  return true;
}

bool test_large_streaming_input(const std::filesystem::path& temp_root, std::string& error) {
  const auto input = temp_root / "large.bin";
  const auto archive = temp_root / "large.bin.zst";
  std::ofstream output(input, std::ios::binary);
  if (!output) {
    error = "failed to create large input";
    return false;
  }

  for (int i = 0; i < 200000; ++i) {
    output << "0123456789abcdef";
  }
  output.close();

  const auto result = mantis::compress(input, archive, 5);
  if (!result.ok) {
    error = result.message;
    return false;
  }

  std::vector<std::byte> decoded;
  if (!mantis::algorithms::decompress_file(archive, decoded, error)) {
    return false;
  }

  const auto original = slurp_bytes(input);
  if (decoded != original) {
    error = "large file roundtrip mismatch";
    return false;
  }

  return true;
}

}  // namespace

int main() {
  const auto temp_root =
      std::filesystem::temp_directory_path() / ("mantis_shrimp_tests_" + std::to_string(::getpid()));
  std::filesystem::create_directories(temp_root);

  const std::array<TestCase, 3> tests = {
      TestCase{"file_roundtrip", test_file_roundtrip},
      TestCase{"directory_packaging", test_directory_packaging},
      TestCase{"large_streaming_input", test_large_streaming_input},
  };

  bool ok = true;
  for (const auto& [name, fn] : tests) {
    std::string error;
    const bool passed = fn(temp_root, error);
    std::cout << (passed ? "[PASS] " : "[FAIL] ") << name;
    if (!passed) {
      std::cout << ": " << error;
      ok = false;
    }
    std::cout << '\n';
  }

  std::filesystem::remove_all(temp_root);
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
