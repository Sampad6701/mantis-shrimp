#include "algorithms/zstd_codec.hpp"

#include <fstream>
#include <ostream>

#include <zstd.h>

namespace mantis::algorithms {

namespace {

std::string zstd_error(std::size_t code) {
  return ZSTD_getErrorName(code);
}

}  // namespace

ZstdCompressor::ZstdCompressor(std::ostream& output, int compression_level)
    : output_(output),
      stream_(ZSTD_createCStream()),
      out_buffer_(ZSTD_CStreamOutSize()) {
  ZSTD_initCStream(static_cast<ZSTD_CStream*>(stream_), compression_level);
}

ZstdCompressor::~ZstdCompressor() {
  ZSTD_freeCStream(static_cast<ZSTD_CStream*>(stream_));
}

bool ZstdCompressor::write(std::span<const std::byte> input, std::string& error) {
  return drain(input.data(), input.size(), false, error);
}

bool ZstdCompressor::finish(std::string& error) {
  if (finished_) {
    return true;
  }

  finished_ = drain(nullptr, 0, true, error);
  return finished_;
}

bool ZstdCompressor::drain(const void* input_data,
                           std::size_t input_size,
                           bool end_frame,
                           std::string& error) {
  ZSTD_inBuffer input{input_data, input_size, 0};

  do {
    ZSTD_outBuffer output_buffer{out_buffer_.data(), out_buffer_.size(), 0};
    const std::size_t result =
        end_frame ? ZSTD_compressStream2(static_cast<ZSTD_CStream*>(stream_),
                                         &output_buffer,
                                         &input,
                                         ZSTD_e_end)
                  : ZSTD_compressStream2(static_cast<ZSTD_CStream*>(stream_),
                                         &output_buffer,
                                         &input,
                                         ZSTD_e_continue);

    if (ZSTD_isError(result)) {
      error = zstd_error(result);
      return false;
    }

    output_.write(out_buffer_.data(), static_cast<std::streamsize>(output_buffer.pos));
    if (!output_) {
      error = "failed to write compressed output";
      return false;
    }

    if (!end_frame && input.pos == input.size) {
      return true;
    }

    if (end_frame && result == 0) {
      return true;
    }
  } while (true);
}

bool decompress_file(const std::filesystem::path& archive_path,
                     std::vector<std::byte>& output,
                     std::string& error) {
  std::ifstream input(archive_path, std::ios::binary);
  if (!input) {
    error = "failed to open compressed file";
    return false;
  }

  std::vector<char> in_buffer(ZSTD_DStreamInSize());
  std::vector<char> out_buffer(ZSTD_DStreamOutSize());

  ZSTD_DStream* stream = ZSTD_createDStream();
  if (stream == nullptr) {
    error = "failed to create zstd decompressor";
    return false;
  }

  const std::size_t init_result = ZSTD_initDStream(stream);
  if (ZSTD_isError(init_result)) {
    error = zstd_error(init_result);
    ZSTD_freeDStream(stream);
    return false;
  }

  while (input) {
    input.read(in_buffer.data(), static_cast<std::streamsize>(in_buffer.size()));
    const std::streamsize read_count = input.gcount();
    if (read_count <= 0) {
      break;
    }

    ZSTD_inBuffer input_buffer{in_buffer.data(), static_cast<std::size_t>(read_count), 0};
    while (input_buffer.pos < input_buffer.size) {
      ZSTD_outBuffer output_buffer{out_buffer.data(), out_buffer.size(), 0};
      const std::size_t result = ZSTD_decompressStream(stream, &output_buffer, &input_buffer);
      if (ZSTD_isError(result)) {
        error = zstd_error(result);
        ZSTD_freeDStream(stream);
        return false;
      }

      const auto* begin = reinterpret_cast<const std::byte*>(out_buffer.data());
      output.insert(output.end(), begin, begin + output_buffer.pos);
    }
  }

  if (!input.eof() && input.fail()) {
    error = "failed while reading compressed file";
    ZSTD_freeDStream(stream);
    return false;
  }

  ZSTD_freeDStream(stream);
  return true;
}

}  // namespace mantis::algorithms
