#include "archive/tar_writer.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string_view>
#include <system_error>

namespace mantis::archive {

namespace {

constexpr std::size_t kTarBlockSize = 512;

struct PosixHeader {
  char name[100];
  char mode[8];
  char uid[8];
  char gid[8];
  char size[12];
  char mtime[12];
  char checksum[8];
  char typeflag;
  char linkname[100];
  char magic[6];
  char version[2];
  char uname[32];
  char gname[32];
  char devmajor[8];
  char devminor[8];
  char prefix[155];
  char padding[12];
};

void write_octal(char* destination, std::size_t width, std::uintmax_t value) {
  std::snprintf(destination, width, "%0*jo", static_cast<int>(width - 1), value);
}

bool split_name(const std::string& input, PosixHeader& header, std::string& error) {
  if (input.size() <= sizeof(header.name)) {
    std::memcpy(header.name, input.data(), input.size());
    return true;
  }

  const std::size_t slash = input.rfind('/', sizeof(header.prefix));
  if (slash == std::string::npos) {
    error = "archive path too long for tar header: " + input;
    return false;
  }

  const std::string_view prefix = std::string_view(input).substr(0, slash);
  const std::string_view name = std::string_view(input).substr(slash + 1);

  if (prefix.size() > sizeof(header.prefix) || name.size() > sizeof(header.name)) {
    error = "archive path too long for tar header: " + input;
    return false;
  }

  std::memcpy(header.prefix, prefix.data(), prefix.size());
  std::memcpy(header.name, name.data(), name.size());
  return true;
}

bool make_header(const std::string& archive_name,
                 bool is_directory,
                 std::uintmax_t size,
                 std::time_t mtime,
                 std::array<std::byte, kTarBlockSize>& block,
                 std::string& error) {
  block.fill(std::byte{0});
  auto* header = reinterpret_cast<PosixHeader*>(block.data());

  if (!split_name(archive_name, *header, error)) {
    return false;
  }

  std::memcpy(header->magic, "ustar", 5);
  std::memcpy(header->version, "00", 2);
  write_octal(header->mode, sizeof(header->mode), is_directory ? 0755 : 0644);
  write_octal(header->uid, sizeof(header->uid), 0);
  write_octal(header->gid, sizeof(header->gid), 0);
  write_octal(header->size, sizeof(header->size), is_directory ? 0 : size);
  write_octal(header->mtime, sizeof(header->mtime), static_cast<std::uintmax_t>(mtime));
  std::memset(header->checksum, ' ', sizeof(header->checksum));
  header->typeflag = is_directory ? '5' : '0';

  unsigned int checksum = 0;
  for (const std::byte byte : block) {
    checksum += static_cast<unsigned char>(byte);
  }

  std::snprintf(header->checksum, sizeof(header->checksum), "%06o", checksum);
  header->checksum[6] = '\0';
  header->checksum[7] = ' ';
  return true;
}

std::string normalize_tar_path(const std::filesystem::path& path) {
  return path.generic_string();
}

std::time_t to_time_t(std::filesystem::file_time_type file_time) {
  const auto system_now = std::chrono::system_clock::now();
  const auto file_now = std::filesystem::file_time_type::clock::now();
  const auto adjusted = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
      file_time - file_now + system_now);
  return std::chrono::system_clock::to_time_t(adjusted);
}

}  // namespace

TarWriter::TarWriter(Sink sink) : sink_(std::move(sink)) {}

bool TarWriter::add_directory_tree(const std::filesystem::path& root, std::string& error) {
  std::error_code ec;
  const auto root_name = root.filename();
  if (root_name.empty()) {
    error = "directory must have a name";
    return false;
  }

  if (!add_directory_entry(normalize_tar_path(root_name) + "/", error)) {
    return false;
  }

  for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
    if (entry.is_symlink(ec)) {
      error = "symlinks are not supported in v0.1";
      return false;
    }

    const auto relative = std::filesystem::relative(entry.path(), root, ec);
    if (ec) {
      error = "failed to compute archive path";
      return false;
    }

    const auto archive_name = normalize_tar_path(root_name / relative);
    if (entry.is_directory(ec)) {
      if (!add_directory_entry(archive_name + "/", error)) {
        return false;
      }
      continue;
    }

    if (!entry.is_regular_file(ec)) {
      error = "unsupported entry in directory tree";
      return false;
    }

    if (!add_file_entry(entry.path(), archive_name, error)) {
      return false;
    }
  }

  return true;
}

bool TarWriter::finish(std::string& error) {
  std::array<std::byte, kTarBlockSize * 2> end_blocks{};
  return write_block(end_blocks.data(), end_blocks.size(), error);
}

bool TarWriter::add_directory_entry(const std::string& archive_name, std::string& error) {
  std::array<std::byte, kTarBlockSize> block{};
  if (!make_header(archive_name, true, 0, std::time(nullptr), block, error)) {
    return false;
  }

  return write_block(block.data(), block.size(), error);
}

bool TarWriter::add_file_entry(const std::filesystem::path& disk_path,
                               const std::string& archive_name,
                               std::string& error) {
  std::error_code ec;
  const auto file_size = std::filesystem::file_size(disk_path, ec);
  if (ec) {
    error = "failed to determine file size";
    return false;
  }
  const auto modified = std::filesystem::last_write_time(disk_path, ec);
  if (ec) {
    error = "failed to read file timestamp";
    return false;
  }

  std::array<std::byte, kTarBlockSize> block{};
  if (!make_header(archive_name, false, file_size, to_time_t(modified), block, error)) {
    return false;
  }

  if (!write_block(block.data(), block.size(), error)) {
    return false;
  }

  std::ifstream input(disk_path, std::ios::binary);
  if (!input) {
    error = "failed to read file contents";
    return false;
  }

  std::array<char, 64 * 1024> buffer{};
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize read_count = input.gcount();
    if (read_count <= 0) {
      break;
    }

    if (!write_block(buffer.data(), static_cast<std::size_t>(read_count), error)) {
      return false;
    }
  }

  if (!input.eof() && input.fail()) {
    error = "failed while streaming file into tar";
    return false;
  }

  const std::size_t remainder = static_cast<std::size_t>(file_size % kTarBlockSize);
  if (remainder != 0) {
    return write_padding(kTarBlockSize - remainder, error);
  }

  return true;
}

bool TarWriter::write_block(const void* data, std::size_t size, std::string& error) {
  return sink_(std::span(reinterpret_cast<const std::byte*>(data), size), error);
}

bool TarWriter::write_padding(std::size_t size, std::string& error) {
  std::array<std::byte, kTarBlockSize> padding{};
  while (size > 0) {
    const std::size_t chunk = std::min(size, padding.size());
    if (!write_block(padding.data(), chunk, error)) {
      return false;
    }
    size -= chunk;
  }
  return true;
}

}  // namespace mantis::archive
