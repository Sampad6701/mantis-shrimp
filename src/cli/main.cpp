#include <iostream>
#include <string>
#include <string_view>
#include <filesystem>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <thread>
#include <chrono>
#include <optional>
#include <vector>

#include "mantis/api.hpp"
#include "mantis/core/smart_engine.hpp"
#include "mantis/codecs/registry.hpp"

namespace fs = std::filesystem;

namespace {

constexpr int kFastLevel = 3;
constexpr int kBalancedLevel = 6;
constexpr int kMaxLevel = 11;

enum class CliMode {
  Compress,
  Extract,
  Analyze,
};

struct CliCommand {
  CliMode mode{CliMode::Analyze};
  fs::path input;
  fs::path output;
  std::string format;
  int compression_level{kBalancedLevel};
  int threads{1};
  bool threads_auto{false};
};

std::string trim_ascii(std::string value) {
  const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
    return std::isspace(ch) != 0;
  });
  const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
    return std::isspace(ch) != 0;
  }).base();

  if (first >= last) {
    return {};
  }

  return std::string(first, last);
}

std::string lower_ascii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

bool has_suffix(std::string_view value, std::string_view suffix) {
  return value.size() >= suffix.size() &&
         value.substr(value.size() - suffix.size()) == suffix;
}

void print_usage(std::string_view program_name) {
  std::cout
      << "Usage:\n"
      << "  " << program_name << "                         Start interactive mode\n"
      << "  " << program_name << " <input> -c <format> [output]\n"
      << "  " << program_name << " <archive> -e [output]\n"
      << "  " << program_name << " <input> -a\n\n"
      << "Formats:\n"
      << "  tar.zst  Directory archive compressed with zstd\n"
      << "  tar.gz   Directory archive compressed with gzip\n"
      << "  tar.br   Directory archive compressed with brotli\n"
      << "  tar.lz4  Directory archive compressed with lz4\n"
      << "  tar.xz   Directory archive compressed with xz\n"
      << "  tar      Directory archive without compression\n"
      << "  zst/gz/br/lz4/xz/zip  Single-file formats\n"
      << "  zip      Directory ZIP archive or single-file ZIP\n"
      << "  raw      Single file copied without compression\n\n"
      << "Options:\n"
      << "  -c, --compress <format>   Compress input\n"
      << "  -e, --extract             Extract archive\n"
      << "  -a, --analyze             Analyze input\n"
      << "  -o, --output <path>       Output file or extraction directory\n"
      << "  --fast                    Faster compression, larger output\n"
      << "  --balanced                Balanced default\n"
      << "  --max                     Smaller output, slower compression\n"
      << "  -l, --level <1-11>        Advanced manual compression level\n"
      << "  --threads auto|N          Worker threads for codecs that support them\n"
      << "  -h, --help                Show this help\n";
}

void clear_screen() {
  #ifdef _WIN32
    system("cls");
  #else
    system("clear");
  #endif
}

void display_banner() {
  std::cout << "\n";
  std::cout << "╔════════════════════════════════════════╗\n";
  std::cout << "║                                        ║\n";
  std::cout << "║  Mantis Shrimp v0.4.0-dev              ║\n";
  std::cout << "║  Smart Compression Utility             ║\n";
  std::cout << "║                                        ║\n";
  std::cout << "╚════════════════════════════════════════╝\n";
  std::cout << "\n";
}

void print_header() {
  std::cout << "\n";
  std::cout << "════════════════════════════════════════════════════\n";
}

void show_progress(const std::string& operation, int duration_ms = 2000) {
  std::cout << operation << "\n";
  
  const int bar_width = 20;
  const int total_iterations = 50;
  const int sleep_ms = duration_ms / total_iterations;
  
  for (int i = 0; i <= total_iterations; ++i) {
    int filled = (i * bar_width) / total_iterations;
    int empty = bar_width - filled;
    int percentage = (i * 100) / total_iterations;
    
    std::cout << "\r";
    std::cout << "  ";
    
    for (int j = 0; j < filled; ++j) {
      std::cout << "█";
    }
    for (int j = 0; j < empty; ++j) {
      std::cout << "░";
    }
    
    std::cout << "  " << std::setw(3) << percentage << "%";
    std::cout.flush();
    
    if (i < total_iterations) {
      std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }
  }
  
  std::cout << "\n";
}

std::string get_input(const std::string& prompt) {
  std::cout << prompt;
  std::string input;
  std::getline(std::cin, input);
  return trim_ascii(input);
}

bool path_exists(const std::string& path_str) {
  return fs::exists(path_str);
}

void display_codecs() {
  auto& registry = mantis::codecs::CodecRegistry::instance();
  auto codecs = registry.listAvailable();

  std::cout << "\nAvailable Algorithms:\n";
  for (size_t i = 0; i < codecs.size(); ++i) {
    std::cout << "  [" << (i + 1) << "] " << codecs[i] << "\n";
  }
  std::cout << "  [0] Auto-select best (default)\n";
}

std::vector<std::string> supported_api_algorithms() {
  return {"zstd", "gzip", "store"};
}

std::optional<std::string> normalize_format(std::string value) {
  value = lower_ascii(trim_ascii(std::move(value)));
  if (value == "zstd") {
    return "zst";
  }
  if (value == "gzip") {
    return "gz";
  }
  if (value == "store") {
    return "raw";
  }
  if (value == "tgz") {
    return "tar.gz";
  }

  if (value == "tar.zst" || value == "tar.gz" || value == "tar.br" ||
      value == "tar.lz4" || value == "tar.xz" || value == "tar" ||
      value == "zst" || value == "gz" || value == "br" ||
      value == "lz4" || value == "xz" || value == "zip" || value == "raw") {
    return value;
  }

  return std::nullopt;
}

bool format_is_archive(std::string_view format) {
  return format == "tar.zst" || format == "tar.gz" || format == "tar.br" ||
         format == "tar.lz4" || format == "tar.xz" || format == "tar" ||
         format == "zip";
}

bool format_is_tar_wrapped(std::string_view format) {
  return format == "tar.zst" || format == "tar.gz" || format == "tar.br" ||
         format == "tar.lz4" || format == "tar.xz";
}

std::string algorithm_for_format(std::string_view format) {
  if (format == "tar.zst" || format == "zst") {
    return "zstd";
  }
  if (format == "tar.gz" || format == "gz") {
    return "gzip";
  }
  if (format == "tar.br" || format == "br") {
    return "brotli";
  }
  if (format == "tar.lz4" || format == "lz4") {
    return "lz4";
  }
  if (format == "tar.xz" || format == "xz") {
    return "xz";
  }
  if (format == "zip") {
    return "zip";
  }
  return "store";
}

fs::path default_output_for_format(const fs::path& input, std::string_view format) {
  const std::string base_name = input.filename().string();
  if (format == "tar.zst") {
    return base_name + ".tar.zst";
  }
  if (format == "tar.gz") {
    return base_name + ".tar.gz";
  }
  if (format == "tar.br") {
    return base_name + ".tar.br";
  }
  if (format == "tar.lz4") {
    return base_name + ".tar.lz4";
  }
  if (format == "tar.xz") {
    return base_name + ".tar.xz";
  }
  if (format == "tar") {
    return base_name + ".tar";
  }
  if (format == "zst") {
    return base_name + ".zst";
  }
  if (format == "gz") {
    return base_name + ".gz";
  }
  if (format == "br") {
    return base_name + ".br";
  }
  if (format == "lz4") {
    return base_name + ".lz4";
  }
  if (format == "xz") {
    return base_name + ".xz";
  }
  if (format == "zip") {
    return base_name + ".zip";
  }
  return base_name + ".raw";
}

bool codec_supports_threads(std::string_view algorithm) {
  return algorithm == "zstd" || algorithm == "xz";
}

int resolve_threads(const CliCommand& command) {
  if (!command.threads_auto) {
    return std::max(1, command.threads);
  }

  const unsigned int detected = std::thread::hardware_concurrency();
  return std::max(1u, detected == 0 ? 1u : detected);
}

std::string threads_display(const CliCommand& command, std::string_view algorithm) {
  const int threads = resolve_threads(command);
  if (!codec_supports_threads(algorithm)) {
    return "1 (backend is currently single-stream)";
  }
  if (command.threads_auto) {
    return "auto (" + std::to_string(threads) + ")";
  }
  return std::to_string(threads);
}

fs::path temp_path_for(const fs::path& input, std::string_view extension) {
  const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
  return fs::temp_directory_path() /
         ("mantis_ms_" + input.filename().string() + "_" + std::to_string(ticks) + std::string(extension));
}

bool remove_known_suffix(std::string& value, std::string_view suffix) {
  if (!has_suffix(value, suffix)) {
    return false;
  }
  value.erase(value.size() - suffix.size());
  return true;
}

fs::path default_extract_output(const fs::path& input, std::string_view format) {
  if (format_is_archive(format)) {
    return {};
  }

  std::string output = input.filename().string();
  const std::string suffix = "." + std::string(format);
  if (!remove_known_suffix(output, suffix)) {
    output += ".out";
  }
  return output;
}

std::optional<std::string> infer_format_from_path(const fs::path& input) {
  const std::string name = lower_ascii(input.filename().string());
  for (std::string_view suffix : {".tar.zst", ".tar.gz", ".tar.br", ".tar.lz4", ".tar.xz"}) {
    if (has_suffix(name, suffix)) {
      return std::string(suffix.substr(1));
    }
  }
  for (std::string_view suffix : {".tar", ".zst", ".gz", ".br", ".lz4", ".xz", ".zip"}) {
    if (has_suffix(name, suffix)) {
      return std::string(suffix.substr(1));
    }
  }
  return std::nullopt;
}

bool compress_with_codec(const fs::path& input,
                         const fs::path& output,
                         std::string_view algorithm,
                         int level,
                         int threads,
                         std::string& error) {
  auto* codec = mantis::codecs::CodecRegistry::instance().getCodec(std::string(algorithm));
  if (codec == nullptr) {
    error = "codec not available: " + std::string(algorithm);
    return false;
  }
  return codec->compress(input, output, level, threads, error);
}

bool decompress_with_codec(const fs::path& input,
                           const fs::path& output,
                           std::string_view algorithm,
                           std::string& error) {
  auto* codec = mantis::codecs::CodecRegistry::instance().getCodec(std::string(algorithm));
  if (codec == nullptr) {
    error = "codec not available: " + std::string(algorithm);
    return false;
  }
  return codec->decompress(input, output, error);
}

mantis::OperationResult compress_command_input(const CliCommand& command,
                                               const fs::path& output,
                                               std::string_view algorithm,
                                               int threads) {
  if (command.format == "raw") {
    return mantis::compress(command.input, output, command.compression_level, "store");
  }

  if (command.format == "tar") {
    return mantis::compress(command.input, output, command.compression_level, "store");
  }

  if (format_is_tar_wrapped(command.format)) {
    std::string error;
    const fs::path temp_tar = temp_path_for(command.input, ".tar");
    const auto tar_result = mantis::compress(command.input, temp_tar, command.compression_level, "store");
    if (!tar_result.ok) {
      return tar_result;
    }

    const bool ok = compress_with_codec(temp_tar, output, algorithm, command.compression_level, threads, error);
    std::error_code ec;
    fs::remove(temp_tar, ec);
    return mantis::OperationResult{
        .ok = ok,
        .input_path = command.input,
        .output_path = output,
        .algorithm = std::string(algorithm),
        .message = ok ? "compression completed" : error,
    };
  }

  std::string error;
  const bool ok = compress_with_codec(command.input, output, algorithm, command.compression_level, threads, error);
  return mantis::OperationResult{
      .ok = ok,
      .input_path = command.input,
      .output_path = output,
      .algorithm = std::string(algorithm),
      .message = ok ? "compression completed" : error,
  };
}

mantis::OperationResult extract_command_input(const CliCommand& command, std::string_view format) {
  const fs::path output = command.output.empty() ? default_extract_output(command.input, format) : command.output;

  if (format == "tar" || format == "tar.zst" || format == "tar.gz") {
    return mantis::extract(command.input, output);
  }

  if (format_is_tar_wrapped(format)) {
    const std::string algorithm = algorithm_for_format(format);
    std::string error;
    const fs::path temp_tar = temp_path_for(command.input, ".tar");
    const bool decoded = decompress_with_codec(command.input, temp_tar, algorithm, error);
    if (!decoded) {
      return mantis::OperationResult{
          .ok = false,
          .input_path = command.input,
          .output_path = output,
          .algorithm = algorithm,
          .message = error,
      };
    }

    auto result = mantis::extract(temp_tar, output);
    result.algorithm = algorithm;
    std::error_code ec;
    fs::remove(temp_tar, ec);
    return result;
  }

  const std::string algorithm = algorithm_for_format(format);
  std::string error;
  const bool ok = decompress_with_codec(command.input, output, algorithm, error);
  return mantis::OperationResult{
      .ok = ok,
      .input_path = command.input,
      .output_path = output,
      .algorithm = algorithm,
      .message = ok ? "extraction completed" : error,
  };
}

std::string level_profile(int level) {
  if (level <= kFastLevel) {
    return "fast";
  }
  if (level <= kBalancedLevel) {
    return level == kBalancedLevel ? "balanced default" : "balanced";
  }
  if (level >= kMaxLevel) {
    return "max";
  }
  return "strong";
}

std::string level_display(int level) {
  return std::to_string(level) + " (" + level_profile(level) + ")";
}

void print_level_transparency(int level) {
  if (level < kMaxLevel) {
    std::cout << "Note: this is not maximum compression. Higher levels may create smaller output "
              << "but take longer. Use --max or -l 11 for maximum compression.\n";
  } else {
    std::cout << "Note: maximum compression may take significantly longer and use more CPU.\n";
  }
}

void display_codecs(const std::vector<std::string>& codecs) {
  std::cout << "\nAvailable Algorithms:\n";
  for (size_t i = 0; i < codecs.size(); ++i) {
    std::cout << "  [" << (i + 1) << "] " << codecs[i] << "\n";
  }
  std::cout << "  [0] Auto-select best (default)\n";
}

void display_compression_levels() {
  std::cout << "\nCompression level (1-11):\n";
  std::cout << "  1-3:   Fast     (less compression, less time)\n";
  std::cout << "  4-6:   Balanced (default/recommended)\n";
  std::cout << "  7-11:  Stronger (smaller output, more time/CPU)\n";
  std::cout << "Profiles: --fast = 3, --balanced = 6, --max = 11\n";
}

int get_compression_level() {
  while (true) {
    std::string input = get_input("Enter compression level [6]: ");
    
    if (input.empty()) {
      return kBalancedLevel;
    }
    
    try {
      int level = std::stoi(input);
      if (level >= 1 && level <= 11) {
        return level;
      }
      std::cerr << "Invalid level. Please enter a number between 1-11.\n";
    } catch (...) {
      std::cerr << "Invalid input. Please enter a number.\n";
    }
  }
}

std::string get_algorithm_choice() {
  auto codecs = supported_api_algorithms();

  display_codecs(codecs);

  while (true) {
    std::string input = get_input("\nChoice (0 or press ENTER for auto): ");
    
    if (input.empty() || input == "0") {
      return "auto";
    }
    
    try {
      int choice = std::stoi(input);
      if (choice >= 1 && choice <= static_cast<int>(codecs.size())) {
        return codecs[choice - 1];
      }
      std::cerr << "Invalid choice. Please select 0-" << codecs.size() << ".\n";
    } catch (const std::invalid_argument&) {
      std::cerr << "Invalid input. Please enter a number or press ENTER for auto-select.\n";
    } catch (...) {
      std::cerr << "Invalid input. Please enter a number or press ENTER for auto-select.\n";
    }
  }
}

void show_analysis(const fs::path& input, int compression_level = 6) {
  std::cout << "\nAnalyzing " << (fs::is_directory(input) ? "directory" : "file");
  show_progress("", 1500);

  if (fs::is_directory(input)) {
    const auto analysis = mantis::analyze(input, compression_level);

    std::cout << "\n" << std::string(50, '=') << "\n";
    std::cout << "COMPRESSION ANALYSIS RESULTS\n";
    std::cout << std::string(50, '=') << "\n\n";
    std::cout << "Input type: directory\n";
    std::cout << "Compression level: " << level_display(compression_level) << "\n";
    print_level_transparency(compression_level);
    std::cout << "Files/directories scanned: " << analysis.entries.size() << "\n";
    std::cout << "Total file bytes: " << analysis.size << "\n";
    std::cout << "\n" << std::string(50, '-') << "\n";
    std::cout << "RECOMMENDATION: " << analysis.recommended_algorithm << "\n";
    std::cout << analysis.recommendation_reason << "\n";
    std::cout << std::string(50, '=') << "\n";
    return;
  }

  auto stats = mantis::core::SmartEngine::instance().benchmarkAll(input, compression_level);
  
  if (stats.empty()) {
    std::cerr << "No codecs available for benchmarking.\n";
    return;
  }
  
  auto rec = mantis::core::SmartEngine::instance().autoSelect(input, compression_level);
  
  std::cout << "\n" << std::string(50, '=') << "\n";
  std::cout << "COMPRESSION ANALYSIS RESULTS\n";
  std::cout << std::string(50, '=') << "\n\n";
  std::cout << "Compression level: " << level_display(compression_level) << "\n";
  print_level_transparency(compression_level);
  std::cout << "\n";
  
  std::cout << std::left 
            << std::setw(12) << "Algorithm"
            << std::setw(16) << "Compressed"
            << std::setw(12) << "Ratio"
            << "Status\n";
  std::cout << std::string(50, '-') << "\n";
  
  for (const auto& stat : stats) {
    std::string marker = (stat.algorithm == rec.algorithm) ? "BEST" : "";
    
    std::cout << std::left 
              << std::setw(12) << stat.algorithm
              << std::setw(16) << stat.compressed_size
              << std::fixed << std::setprecision(2)
              << std::setw(12) << (std::string(std::to_string(stat.compression_ratio).substr(0, 5)) + "%")
              << marker << "\n";
  }
  
  std::cout << "\n" << std::string(50, '-') << "\n";
  std::cout << "RECOMMENDATION: " << rec.algorithm << "\n";
  std::cout << "Compression Ratio: " << std::fixed << std::setprecision(2) 
            << rec.compression_ratio << "%\n";
  std::cout << std::string(50, '=') << "\n";
}

std::optional<CliCommand> parse_command_line(int argc, char* argv[], std::string& error) {
  if (argc < 2) {
    error = "missing input path";
    return std::nullopt;
  }

  CliCommand command;
  command.input = trim_ascii(argv[1]);
  bool mode_set = false;
  bool positional_output_set = false;
  bool level_set = false;

  for (int index = 2; index < argc; ++index) {
    const std::string arg = trim_ascii(argv[index]);

    if (arg == "-c" || arg == "--compress") {
      if (mode_set) {
        error = "choose only one action";
        return std::nullopt;
      }
      if (index + 1 >= argc) {
        error = "-c requires a format";
        return std::nullopt;
      }
      const auto format = normalize_format(argv[++index]);
      if (!format.has_value()) {
        error = "unsupported format; use tar.zst, tar.gz, tar.br, tar.lz4, tar.xz, tar, zst, gz, br, lz4, xz, zip, or raw";
        return std::nullopt;
      }
      command.mode = CliMode::Compress;
      command.format = format.value();
      mode_set = true;
      continue;
    }

    if (arg == "-e" || arg == "--extract") {
      if (mode_set) {
        error = "choose only one action";
        return std::nullopt;
      }
      command.mode = CliMode::Extract;
      mode_set = true;
      continue;
    }

    if (arg == "-a" || arg == "--analyze") {
      if (mode_set) {
        error = "choose only one action";
        return std::nullopt;
      }
      command.mode = CliMode::Analyze;
      mode_set = true;
      continue;
    }

    if (arg == "-o" || arg == "--output") {
      if (index + 1 >= argc) {
        error = "-o requires a path";
        return std::nullopt;
      }
      command.output = trim_ascii(argv[++index]);
      continue;
    }

    if (arg == "--fast" || arg == "--balanced" || arg == "--max" || arg == "--best") {
      if (level_set) {
        error = "choose only one compression profile or level";
        return std::nullopt;
      }
      if (arg == "--fast") {
        command.compression_level = kFastLevel;
      } else if (arg == "--balanced") {
        command.compression_level = kBalancedLevel;
      } else {
        command.compression_level = kMaxLevel;
      }
      level_set = true;
      continue;
    }

    if (arg == "-l" || arg == "--level") {
      if (level_set) {
        error = "choose only one compression profile or level";
        return std::nullopt;
      }
      if (index + 1 >= argc) {
        error = "-l requires a compression level";
        return std::nullopt;
      }
      try {
        command.compression_level = std::stoi(argv[++index]);
      } catch (...) {
        error = "compression level must be a number";
        return std::nullopt;
      }
      if (command.compression_level < 1 || command.compression_level > 11) {
        error = "compression level must be between 1 and 11";
        return std::nullopt;
      }
      level_set = true;
      continue;
    }

    if (arg == "--threads") {
      if (index + 1 >= argc) {
        error = "--threads requires auto or a positive number";
        return std::nullopt;
      }

      const std::string value = lower_ascii(trim_ascii(argv[++index]));
      if (value == "auto") {
        command.threads_auto = true;
        command.threads = 1;
        continue;
      }

      try {
        command.threads = std::stoi(value);
      } catch (...) {
        error = "--threads requires auto or a positive number";
        return std::nullopt;
      }

      if (command.threads < 1) {
        error = "--threads must be at least 1";
        return std::nullopt;
      }
      command.threads_auto = false;
      continue;
    }

    if (arg == "-h" || arg == "--help") {
      error = "help";
      return std::nullopt;
    }

    if (arg.starts_with("-")) {
      error = "unknown option: " + arg;
      return std::nullopt;
    }

    if (positional_output_set || !command.output.empty()) {
      error = "unexpected extra argument: " + arg;
      return std::nullopt;
    }
    command.output = arg;
    positional_output_set = true;
  }

  if (!mode_set) {
    error = "missing action; use -c, -e, or -a";
    return std::nullopt;
  }

  if (command.input.empty()) {
    error = "input path cannot be empty";
    return std::nullopt;
  }

  return command;
}

int run_command(const CliCommand& command) {
  if (!fs::exists(command.input)) {
    std::cerr << "Error: input path does not exist: " << command.input << "\n";
    return 1;
  }

  if (command.mode == CliMode::Analyze) {
    show_analysis(command.input, command.compression_level);
    return 0;
  }

  if (command.mode == CliMode::Compress) {
    const bool input_is_directory = fs::is_directory(command.input);

    if (!input_is_directory && (format_is_tar_wrapped(command.format) || command.format == "tar")) {
      std::cerr << "Error: " << command.format
                << " is a directory archive format. Use zst, gz, br, lz4, xz, zip, or raw for single files.\n";
      return 1;
    }
    if (input_is_directory && !format_is_archive(command.format)) {
      std::cerr << "Error: " << command.format
                << " is a single-file format. Use tar.zst, tar.gz, tar.br, tar.lz4, tar.xz, tar, or zip for directories.\n";
      return 1;
    }

    const fs::path output =
        command.output.empty() ? default_output_for_format(command.input, command.format) : command.output;
    const std::string algorithm = algorithm_for_format(command.format);
    const int threads = resolve_threads(command);
    const auto result = compress_command_input(command, output, algorithm, threads);

    if (!result.ok) {
      std::cerr << "Error: " << result.message << "\n";
      return 1;
    }

    std::cout << "Compressed: " << fs::absolute(result.output_path) << "\n";
    std::cout << "Algorithm: " << result.algorithm << "\n";
    std::cout << "Level: " << level_display(command.compression_level) << "\n";
    std::cout << "Threads: " << threads_display(command, algorithm) << "\n";
    print_level_transparency(command.compression_level);
    return 0;
  }

  const auto format = infer_format_from_path(command.input);
  if (!format.has_value()) {
    std::cerr << "Error: unsupported archive extension\n";
    return 1;
  }

  const auto result = extract_command_input(command, format.value());
  if (!result.ok) {
    std::cerr << "Error: " << result.message << "\n";
    return 1;
  }

  std::cout << "Extracted to: " << fs::absolute(result.output_path) << "\n";
  return 0;
}

void compress_workflow() {
  print_header();
  
  std::string input_path;
  while (true) {
    input_path = get_input("Enter file or directory path to compress: ");
    if (path_exists(input_path)) {
      break;
    }
    std::cerr << "Path does not exist. Please try again.\n";
  }
  
  fs::path input(input_path);

  std::cout << "\n";
  display_compression_levels();
  int level = get_compression_level();
  
  bool show_analysis_first = true;
  std::string show_analysis_input = get_input("\nAnalyze compression options first? [Y/n]: ");
  if (show_analysis_input == "n" || show_analysis_input == "N") {
    show_analysis_first = false;
  }
  
  if (show_analysis_first) {
    show_analysis(input, level);
  }
  
  std::cout << "\n";
  std::string algorithm = get_algorithm_choice();
  
  if (algorithm == "auto") {
    if (fs::is_directory(input)) {
      const auto analysis = mantis::analyze(input, level);
      algorithm = analysis.recommended_algorithm;
      std::cout << "\nAuto-selected algorithm: " << algorithm << "\n";
      std::cout << analysis.recommendation_reason << "\n";
    } else {
      auto rec = mantis::core::SmartEngine::instance().autoSelect(input, level);
      algorithm = rec.algorithm;
      std::cout << "\nAuto-selected algorithm: " << algorithm << "\n";
      std::cout << "Expected compression ratio: " << std::fixed << std::setprecision(2)
                << rec.compression_ratio << "%\n";
    }
  }
  
  show_progress("\nCompressing with " + algorithm + " (level " + std::to_string(level) + ")", 2500);
  
  const auto result = mantis::compress(input, fs::path{}, level, algorithm);
  
  if (result.ok) {
    std::cout << "\n" << std::string(50, '=') << "\n";
    std::cout << "SUCCESS!\n";
    std::cout << std::string(50, '=') << "\n";
    std::cout << "Output file: " << result.output_path << "\n";
    std::cout << "Algorithm: " << result.algorithm << "\n";
    std::cout << "Level: " << level_display(level) << "\n";
    print_level_transparency(level);
    std::cout << std::string(50, '=') << "\n";
  } else {
    std::cerr << "\nFAILED!\n";
    std::cerr << "Error: " << result.message << "\n";
  }
}

void decompress_workflow() {
  print_header();
  
  std::string input_path;
  while (true) {
    input_path = get_input("Enter compressed file path to decompress: ");
    if (path_exists(input_path)) {
      break;
    }
    std::cerr << "Path does not exist. Please try again.\n";
  }
  
  fs::path input(input_path);
  
  show_progress("Decompressing " + input.filename().string(), 2500);
  
  const auto result = mantis::extract(input, fs::path{});
  
  if (result.ok) {
    std::cout << "\n" << std::string(50, '=') << "\n";
    std::cout << "SUCCESS!\n";
    std::cout << std::string(50, '=') << "\n";
    std::cout << "Output: " << result.output_path << "\n";
    std::cout << std::string(50, '=') << "\n";
  } else {
    std::cerr << "\nFAILED!\n";
    std::cerr << "Error: " << result.message << "\n";
  }
}

void analyze_workflow() {
  print_header();
  
  std::string input_path;
  while (true) {
    input_path = get_input("Enter file or directory path to analyze: ");
    if (path_exists(input_path)) {
      break;
    }
    std::cerr << "Path does not exist. Please try again.\n";
  }
  
  show_analysis(fs::path(input_path), 6);
}

void main_menu() {
  while (true) {
    clear_screen();
    display_banner();
    
    std::cout << "What would you like to do?\n\n";
    std::cout << "  [1] Compress file or directory\n";
    std::cout << "  [2] Decompress file\n";
    std::cout << "  [3] Analyze compression options\n";
    std::cout << "  [4] List available algorithms\n";
    std::cout << "  [5] Exit\n";
    
    std::string choice = get_input("\nSelect an option [1-5]: ");
    
    if (choice == "1") {
      compress_workflow();
    } else if (choice == "2") {
      decompress_workflow();
    } else if (choice == "3") {
      analyze_workflow();
    } else if (choice == "4") {
      clear_screen();
      display_banner();
      display_codecs();
      std::cout << "\n";
    } else if (choice == "5") {
      std::cout << "\nThank you for using Mantis Shrimp!\n\n";
      return;
    } else {
      std::cerr << "\nInvalid choice. Please select 1-5.\n";
      continue;
    }
    
    std::cout << "\nPress Enter to continue...";
    std::cin.ignore();
    std::cin.get();
  }
}

}

int main(int argc, char* argv[]) {
  if (argc > 1) {
    const std::string first_arg = trim_ascii(argv[1]);
    if (first_arg == "-h" || first_arg == "--help") {
      print_usage(fs::path(argv[0]).filename().string());
      return 0;
    }

    std::string error;
    const auto command = parse_command_line(argc, argv, error);
    if (!command.has_value()) {
      if (error == "help") {
        print_usage(fs::path(argv[0]).filename().string());
        return 0;
      }
      std::cerr << "Error: " << error << "\n\n";
      print_usage(fs::path(argv[0]).filename().string());
      return 2;
    }

    return run_command(command.value());
  }

  main_menu();
  return 0;
}
