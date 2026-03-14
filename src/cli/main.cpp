#include <iostream>
#include <string_view>

#include "mantis/api.hpp"

namespace {

void print_usage() {
  std::cout
      << "Usage:\n"
      << "  mantis compress <path> [-o <output>] [--level <n>]\n"
      << "  mantis analyze <path>\n"
      << "  mantis extract <archive> [destination]\n";
}

const char* kind_to_string(mantis::InputKind kind) {
  switch (kind) {
    case mantis::InputKind::Missing:
      return "missing";
    case mantis::InputKind::File:
      return "file";
    case mantis::InputKind::Directory:
      return "directory";
    case mantis::InputKind::Other:
      return "other";
  }

  return "unknown";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    print_usage();
    return 1;
  }

  const std::string_view command = argv[1];
  if (command == "compress") {
    if (argc < 3) {
      print_usage();
      return 1;
    }

    std::filesystem::path input = argv[2];
    std::filesystem::path output;
    int level = 3;

    for (int i = 3; i < argc; ++i) {
      const std::string_view flag = argv[i];
      if (flag == "-o" && i + 1 < argc) {
        output = argv[++i];
        continue;
      }

      if (flag == "--level" && i + 1 < argc) {
        level = std::stoi(argv[++i]);
        continue;
      }

      std::cerr << "unknown argument: " << flag << '\n';
      return 1;
    }

    const auto result = mantis::compress(input, output, level);
    if (!result.ok) {
      std::cerr << "compression failed: " << result.message << '\n';
      return 1;
    }

    std::cout << result.output_path << '\n';
    return 0;
  }

  if (command == "analyze") {
    if (argc != 3) {
      print_usage();
      return 1;
    }

    const auto analysis = mantis::analyze(argv[2]);
    std::cout << "kind: " << kind_to_string(analysis.kind) << '\n';
    std::cout << "size: " << analysis.size << '\n';
    std::cout << "entries: " << analysis.entries.size() << '\n';
    return analysis.kind == mantis::InputKind::Missing ? 1 : 0;
  }

  if (command == "extract") {
    if (argc < 3 || argc > 4) {
      print_usage();
      return 1;
    }

    const std::filesystem::path destination = argc == 4 ? argv[3] : std::filesystem::path{};
    const auto result = mantis::extract(argv[2], destination);
    if (!result.ok) {
      std::cerr << result.message << '\n';
      return 1;
    }

    return 0;
  }

  print_usage();
  return 1;
}
