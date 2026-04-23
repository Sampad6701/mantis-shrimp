#include "algorithms/gzip_codec.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <ostream>

#include <zlib.h>

namespace mantis::algorithms {

namespace {

constexpr std::size_t kBufferSize = 64 * 1024;

std::string zlib_error(int code) {
  switch (code) {
    case Z_MEM_ERROR:
      return "zlib memory allocation failed";
    case Z_BUF_ERROR:
      return "zlib buffer error";
    case Z_DATA_ERROR:
      return "zlib data error";
    case Z_STREAM_ERROR:
      return "zlib stream error";
    case Z_VERSION_ERROR:
      return "zlib version mismatch";
    default:
      return "zlib error";
  }
}

}  // namespace

GzipCompressor::GzipCompressor(std::ostream& output, int compression_level)
    : output_(output), stream_(new z_stream{}), out_buffer_(kBufferSize) {
  auto* stream = static_cast<z_stream*>(stream_);
  const int level = compression_level < 0 ? Z_DEFAULT_COMPRESSION
                                           : std::min(std::max(compression_level, 0), 9);
  const int init_result = deflateInit2(
      stream, level, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
  initialized_ = init_result == Z_OK;
}

GzipCompressor::~GzipCompressor() {
  end_stream();
  delete static_cast<z_stream*>(stream_);
}

bool GzipCompressor::set_error(int code, std::string& error) {
  error = zlib_error(code);
  return false;
}

bool GzipCompressor::write_output_chunk(std::size_t produced, std::string& error) {
  output_.write(reinterpret_cast<const char*>(out_buffer_.data()),
                static_cast<std::streamsize>(produced));
  if (!output_) {
    error = "failed to write compressed output";
    return false;
  }

  return true;
}

bool GzipCompressor::deflate_bytes(const std::byte* input,
                                   std::size_t input_size,
                                   int flush_mode,
                                   std::string& error) {
  if (!initialized_) {
    error = "failed to initialize gzip compressor";
    return false;
  }

  auto* stream = static_cast<z_stream*>(stream_);
  stream->next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(input));
  stream->avail_in = static_cast<uInt>(input_size);

  do {
    stream->next_out = out_buffer_.data();
    stream->avail_out = static_cast<uInt>(out_buffer_.size());

    const int result = deflate(stream, flush_mode);
    if (result != Z_OK && result != Z_STREAM_END) {
      return set_error(result, error);
    }

    const std::size_t produced = out_buffer_.size() - stream->avail_out;
    if (produced > 0 && !write_output_chunk(produced, error)) {
      return false;
    }

    if (flush_mode == Z_FINISH && result == Z_STREAM_END) {
      return true;
    }
  } while (stream->avail_in > 0 || stream->avail_out == 0);

  return flush_mode != Z_FINISH;
}

bool GzipCompressor::write(std::span<const std::byte> input, std::string& error) {
  if (finished_) {
    error = "gzip stream already finished";
    return false;
  }

  return deflate_bytes(input.data(), input.size(), Z_NO_FLUSH, error);
}

bool GzipCompressor::end_stream() {
  if (!initialized_) {
    return false;
  }

  initialized_ = false;
  return deflateEnd(static_cast<z_stream*>(stream_)) == Z_OK;
}

bool GzipCompressor::finish(std::string& error) {
  if (finished_) {
    return true;
  }

  if (!deflate_bytes(nullptr, 0, Z_FINISH, error)) {
    return false;
  }

  finished_ = true;
  end_stream();
  return true;
}

bool decompress_gzip_file(const std::filesystem::path& archive_path,
                          std::vector<std::byte>& output,
                          std::string& error) {
  std::ifstream input(archive_path, std::ios::binary);
  if (!input) {
    error = "failed to open compressed file";
    return false;
  }

  z_stream stream{};
  if (inflateInit2(&stream, 15 + 16) != Z_OK) {
    error = "failed to initialize gzip decompressor";
    return false;
  }

  std::array<unsigned char, kBufferSize> in_buffer{};
  std::array<unsigned char, kBufferSize> out_buffer{};
  bool reached_stream_end = false;

  while (input) {
    input.read(reinterpret_cast<char*>(in_buffer.data()),
               static_cast<std::streamsize>(in_buffer.size()));
    const std::streamsize read_count = input.gcount();
    if (read_count <= 0) {
      break;
    }

    stream.next_in = in_buffer.data();
    stream.avail_in = static_cast<uInt>(read_count);

    do {
      stream.next_out = out_buffer.data();
      stream.avail_out = static_cast<uInt>(out_buffer.size());

      const int result = inflate(&stream, Z_NO_FLUSH);
      if (result != Z_OK && result != Z_STREAM_END) {
        error = zlib_error(result);
        inflateEnd(&stream);
        return false;
      }

      const std::size_t produced = out_buffer.size() - stream.avail_out;
      const auto* begin = reinterpret_cast<const std::byte*>(out_buffer.data());
      output.insert(output.end(), begin, begin + produced);

      if (result == Z_STREAM_END) {
        reached_stream_end = true;
        break;
      }
    } while (stream.avail_in > 0);

    if (reached_stream_end) {
      break;
    }
  }

  if (!input.eof() && input.fail()) {
    error = "failed while reading compressed file";
    inflateEnd(&stream);
    return false;
  }

  inflateEnd(&stream);
  if (!reached_stream_end) {
    error = "gzip stream ended unexpectedly";
    return false;
  }

  return true;
}

}  // namespace mantis::algorithms
