#include "zip_codec.hpp"
#include <fstream>
#include <vector>
#include <zip.h>

namespace mantis::codecs {

namespace {

bool add_zip_file(zip_t* archive,
                  const fs::path& disk_path,
                  const std::string& entry_name,
                  int level,
                  std::string& error) {
    zip_source_t* source = zip_source_file(
        archive,
        disk_path.string().c_str(),
        0,
        0
    );

    if (!source) {
        error = "Failed to create ZIP source";
        return false;
    }

    const zip_int64_t index = zip_file_add(archive, entry_name.c_str(), source, ZIP_FL_OVERWRITE);
    if (index < 0) {
        error = "Failed to add file to ZIP archive";
        zip_source_free(source);
        return false;
    }

    zip_set_file_compression(archive, static_cast<zip_uint64_t>(index), ZIP_CM_DEFLATE, level);
    return true;
}

bool add_zip_directory(zip_t* archive,
                       const std::string& entry_name,
                       std::string& error) {
    if (zip_dir_add(archive, entry_name.c_str(), ZIP_FL_ENC_UTF_8) < 0) {
        error = "Failed to add directory to ZIP archive";
        return false;
    }
    return true;
}

fs::path strip_first_component(const fs::path& path) {
    fs::path stripped;
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

}

bool ZipCodec::compress(
    const fs::path& input,
    const fs::path& output,
    int level,
    std::string& error
) {
    try {
        int zip_error = ZIP_ER_OK;
        zip_t* archive = zip_open(output.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &zip_error);

        if (!archive) {
            error = "Failed to create ZIP archive";
            return false;
        }

        if (fs::is_regular_file(input)) {
            std::string entry_name = input.filename().string();
            if (!add_zip_file(archive, input, entry_name, level, error)) {
                zip_close(archive);
                return false;
            }
        } else if (fs::is_directory(input)) {
            const auto root_name = input.filename();
            if (root_name.empty()) {
                error = "Directory must have a name";
                zip_close(archive);
                return false;
            }

            if (!add_zip_directory(archive, root_name.generic_string() + "/", error)) {
                zip_close(archive);
                return false;
            }

            std::error_code ec;
            for (const auto& entry : fs::recursive_directory_iterator(input)) {
                const auto relative = entry.path().lexically_relative(input);
                if (relative.empty()) {
                    error = "Failed to compute ZIP entry path";
                    zip_close(archive);
                    return false;
                }

                const auto archive_path = (root_name / relative).generic_string();
                if (entry.is_symlink(ec)) {
                    error = "ZIP symlink entries are not supported yet";
                    zip_close(archive);
                    return false;
                }

                if (entry.is_directory(ec)) {
                    if (!add_zip_directory(archive, archive_path + "/", error)) {
                        zip_close(archive);
                        return false;
                    }
                    continue;
                }

                if (!entry.is_regular_file(ec)) {
                    error = "Unsupported entry in directory tree";
                    zip_close(archive);
                    return false;
                }

                if (!add_zip_file(archive, entry.path(), archive_path, level, error)) {
                    zip_close(archive);
                    return false;
                }
            }
        } else {
            error = "Unsupported input type for ZIP archive";
            zip_close(archive);
            return false;
        }

        if (zip_close(archive) != 0) {
            error = "Failed to close ZIP archive";
            return false;
        }

        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

bool ZipCodec::decompress(
    const fs::path& input,
    const fs::path& output,
    std::string& error
) {
    try {
        int zip_error = ZIP_ER_OK;
        zip_t* archive = zip_open(input.string().c_str(), ZIP_RDONLY, &zip_error);

        if (!archive) {
            error = "Failed to open ZIP archive";
            return false;
        }

        const zip_int64_t num_entries = zip_get_num_entries(archive, 0);
        if (num_entries <= 0) {
            error = "ZIP archive is empty";
            zip_close(archive);
            return false;
        }

        const bool output_is_existing_dir = !output.empty() && fs::exists(output) && fs::is_directory(output);
        const bool single_entry = (num_entries == 1);
        fs::path base_output_dir;

        if (!single_entry) {
            base_output_dir = output.empty() ? input.stem() : output;
            std::error_code ec;
            fs::create_directories(base_output_dir, ec);
            if (ec) {
                error = "Failed to create output directory: " + ec.message();
                zip_close(archive);
                return false;
            }
        }

        for (zip_int64_t i = 0; i < num_entries; ++i) {
            zip_stat_t stat;
            if (zip_stat_index(archive, i, 0, &stat) != 0) {
                error = "Failed to read ZIP entry metadata";
                zip_close(archive);
                return false;
            }

            const std::string entry_name = stat.name ? stat.name : "";
            const bool is_directory_entry = !entry_name.empty() && entry_name.back() == '/';
            fs::path entry_path;

            if (single_entry) {
                const fs::path entry_filename = fs::path(entry_name).filename();
                if (output.empty()) {
                    entry_path = entry_filename;
                } else if (output_is_existing_dir) {
                    entry_path = output / entry_filename;
                } else {
                    entry_path = output;
                }
            } else {
                fs::path relative_entry = entry_name;
                if (!output.empty()) {
                    relative_entry = strip_first_component(relative_entry);
                    if (relative_entry.empty()) {
                        continue;
                    }
                }
                entry_path = base_output_dir / relative_entry;
            }

            std::error_code ec;
            if (is_directory_entry) {
                fs::create_directories(entry_path, ec);
                if (ec) {
                    error = "Failed to create output directory: " + ec.message();
                    zip_close(archive);
                    return false;
                }
                continue;
            }

            fs::create_directories(entry_path.parent_path(), ec);
            if (ec) {
                error = "Failed to create output directory: " + ec.message();
                zip_close(archive);
                return false;
            }

            zip_file_t* file = zip_fopen_index(archive, i, 0);
            if (!file) {
                error = "Failed to open entry in ZIP archive";
                zip_close(archive);
                return false;
            }

            std::vector<uint8_t> file_data(stat.size);
            const zip_int64_t bytes_read = zip_fread(file, file_data.data(), stat.size);
            if (bytes_read != static_cast<zip_int64_t>(stat.size)) {
                error = "Failed to read entry from ZIP archive";
                zip_fclose(file);
                zip_close(archive);
                return false;
            }

            zip_fclose(file);

            std::ofstream out(entry_path, std::ios::binary);
            if (!out) {
                error = "Failed to create output file";
                zip_close(archive);
                return false;
            }

            out.write(reinterpret_cast<const char*>(file_data.data()), file_data.size());
            out.close();
        }

        zip_close(archive);
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

CompressionStats ZipCodec::stats(const fs::path& input, int level) {
    CompressionStats stats;
    stats.algorithm = name();
    stats.original_size = fs::file_size(input);
    stats.compression_level = level;

    fs::path temp_output = input.string() + ".zip.tmp";
    std::string error;

    if (compress(input, temp_output, level, error)) {
        stats.compressed_size = fs::file_size(temp_output);
        stats.compression_ratio = 100.0 * (1.0 - (double)stats.compressed_size / stats.original_size);
        fs::remove(temp_output);
    }

    return stats;
}

}
