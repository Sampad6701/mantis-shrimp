#include <iostream>
#include <string>
#include <filesystem>
#include <iomanip>
#include <algorithm>
#include <thread>
#include <chrono>

#include "mantis/api.hpp"
#include "mantis/core/smart_engine.hpp"
#include "mantis/codecs/registry.hpp"

namespace fs = std::filesystem;

namespace {

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
  std::cout << "║  Mantis Shrimp v0.3.0                 ║\n";
  std::cout << "║  Smart Compression Utility            ║\n";
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
  return input;
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

void display_compression_levels() {
  std::cout << "\nCompression level (1-11):\n";
  std::cout << "  1-3:   Fast     (less compression)\n";
  std::cout << "  4-6:   Balanced (recommended)\n";
  std::cout << "  7-11:  Maximum  (slower)\n";
}

int get_compression_level() {
  while (true) {
    std::string input = get_input("Enter compression level [6]: ");
    
    if (input.empty()) {
      return 6;
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
  auto& registry = mantis::codecs::CodecRegistry::instance();
  auto codecs = registry.listAvailable();
  
  display_codecs();
  
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

void show_analysis(const fs::path& input) {
  std::cout << "\nAnalyzing file";
  show_progress("", 1500);
  
  auto stats = mantis::core::SmartEngine::instance().benchmarkAll(input);
  
  if (stats.empty()) {
    std::cerr << "No codecs available for benchmarking.\n";
    return;
  }
  
  auto rec = mantis::core::SmartEngine::instance().autoSelect(input);
  
  std::cout << "\n" << std::string(50, '=') << "\n";
  std::cout << "COMPRESSION ANALYSIS RESULTS\n";
  std::cout << std::string(50, '=') << "\n\n";
  
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
  
  bool show_analysis_first = true;
  std::string show_analysis_input = get_input("\nAnalyze compression options first? [Y/n]: ");
  if (show_analysis_input == "n" || show_analysis_input == "N") {
    show_analysis_first = false;
  }
  
  if (show_analysis_first) {
    show_analysis(input);
  }
  
  std::cout << "\n";
  display_compression_levels();
  int level = get_compression_level();
  
  std::cout << "\n";
  std::string algorithm = get_algorithm_choice();
  
  if (algorithm == "auto") {
    auto rec = mantis::core::SmartEngine::instance().autoSelect(input);
    algorithm = rec.algorithm;
    std::cout << "\nAuto-selected algorithm: " << algorithm << "\n";
    std::cout << "Expected compression ratio: " << std::fixed << std::setprecision(2) 
              << rec.compression_ratio << "%\n";
  }
  
  show_progress("\nCompressing with " + algorithm + " (level " + std::to_string(level) + ")", 2500);
  
  const auto result = mantis::compress(input, fs::path{}, level, algorithm);
  
  if (result.ok) {
    std::cout << "\n" << std::string(50, '=') << "\n";
    std::cout << "SUCCESS!\n";
    std::cout << std::string(50, '=') << "\n";
    std::cout << "Output file: " << result.output_path << "\n";
    std::cout << "Algorithm: " << result.algorithm << "\n";
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
  
  show_analysis(fs::path(input_path));
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

int main() {
  main_menu();
  return 0;
}

