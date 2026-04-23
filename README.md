# Mantis Shrimp v0.3.0 (Early Development)

⚠️ **Development Status**: This project is in active development. v0.3.0 is a feature-complete **proof of concept** with working compression functionality, not yet production-ready. Breaking changes may occur.

Mantis Shrimp is a universal compression utility with a smart engine that automatically selects the best algorithm for your data.

Instead of juggling multiple tools (gzip, brotli, xz, zip, lz4), use one unified interface with intelligent codec auto-selection.

## Features

- 6 Compression Algorithms: Zstandard, Gzip, Brotli, LZ4, XZ, ZIP
- Smart Auto-Selection: Benchmarks all codecs and picks the best for your data
- Compression Analysis: See compression ratios across all algorithms
- TAR Packaging: Handle directories natively
- Public C++ API: Integrate compression into your projects
- Power-User Shortcuts: Force specific algorithms or compression levels

## The Problem It Solves

Users face unnecessary friction with compression:

- **Too many tools**: gzip, brotli, xz, 7z, zip—each with different syntax and learning curves
- **Guesswork**: Which codec is best for my data? How do I know without testing all of them?
- **Learning curves**: Different tools, different flags, different defaults
- **No unified interface**: Different CLIs, different APIs, no consistency
- **Codec selection paralysis**: Even experts spend time benchmarking different algorithms

**Mantis Shrimp solves this with one unified tool**: Smart defaults, automatic benchmarking, single interface for all codecs.

## Quick Start

```bash
# Clone the repository
git clone https://github.com/Sampad6701/mantis-shrimp.git
cd mantis-shrimp

# Install dependencies (Ubuntu/Debian)
sudo apt update
sudo apt install cmake g++ libzstd-dev zlib1g-dev libbrotli-dev liblz4-dev liblzma-dev libzip-dev pkg-config

# Build
cmake -S . -B build
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure

# Try it out
echo "Hello, world!" > example.txt
./build/mantis
```

## Prerequisites

- CMake 3.20 or higher
- C++20 compatible compiler (GCC 10+, Clang 10+, or MSVC 2019+)
- libzstd development libraries
- zlib development libraries
- libbrotli development libraries
- liblz4 development libraries
- liblzma development libraries (for XZ)
- libzip development libraries
- pkg-config

### Install Dependencies

**Ubuntu/Debian:**
```bash
sudo apt update
sudo apt install cmake g++ libzstd-dev zlib1g-dev libbrotli-dev liblz4-dev liblzma-dev libzip-dev pkg-config
```

**macOS:**
```bash
brew install cmake zstd zlib brotli lz4 xz libzip pkg-config
```

**Fedora/RHEL:**
```bash
sudo dnf install cmake gcc-c++ libzstd-devel zlib-devel brotli-devel lz4-devel xz-devel libzip-devel pkgconfig
```

## Usage

The CLI is **fully interactive**. Simply run:

```bash
./build/mantis
```

You'll be guided through:
1. **Main Menu** - Choose action (Compress, Decompress, Analyze, List Codecs)
2. **File Selection** - Enter file or directory path
3. **Analysis** - Optional: See compression ratios for all algorithms
4. **Algorithm Selection** - Pick specific algorithm or auto-select best
5. **Compression Level** - Choose speed vs. ratio tradeoff
6. **Results** - Clear confirmation with output file location

### Interactive Workflow Example

```
╔════════════════════════════════════════════════════════════════╗
║         Mantis Shrimp v0.3 - Universal Compression             ║
╚════════════════════════════════════════════════════════════════╝

What would you like to do?

  [1] Compress file or directory
  [2] Decompress file
  [3] Analyze compression options
  [4] List available algorithms
  [5] Exit

Select an option [1-5]: 1

Enter file or directory path to compress: myfile.txt

Analyze compression options first? [Y/n]: Y

Analyzing file...

==================================================
COMPRESSION ANALYSIS RESULTS
==================================================

Algorithm    Compressed       Ratio       Status
--------------------------------------------------
zstd         125437           34.22%      
gzip         142156           42.15%      
brotli       119834           31.45%      BEST
lz4          289456           78.34%      
xz           112234           29.11%      
zip          125000           34.10%      

--------------------------------------------------
RECOMMENDATION: brotli
Compression Ratio: 31.45%
==================================================

Compression level (1-11):
  1-3:   Fast     (less compression)
  4-6:   Balanced (recommended)
  7-11:  Maximum  (slower)

Enter compression level [6]: 6

Available compression algorithms:
  [1] brotli
  [2] gzip
  [3] lz4
  [4] xz
  [5] zip
  [6] zstd

Select algorithm [0 for auto-select]: 0

Auto-selected algorithm: brotli
Expected compression ratio: 31.45%

Starting compression with brotli (level 6)...

==================================================
SUCCESS!
==================================================
Output file: myfile.br
Algorithm: brotli
==================================================
```

### Menu Options

1. **Compress** - Compress file/directory with algorithm selection
2. **Decompress** - Extract compressed files automatically
3. **Analyze** - View all codec compression ratios with recommendations
4. **List Codecs** - Display available algorithms
5. **Exit** - Quit the application

## Algorithm Selection Guide

| Algorithm | Speed | Ratio | Best For |
|-----------|-------|-------|----------|
| **zstd** | Very Fast | Excellent | General use (default) |
| **gzip** | Fast | Good | Compatibility, legacy systems |
| **brotli** | Fast | Excellent | Web assets, text |
| **lz4** | Fastest | Poor | Real-time, streaming |
| **xz** | Slow | Excellent | Maximum compression, archival |
| **zip** | Fast | Very Good | Archive compatibility |

## Binary Footprint

Mantis Shrimp includes all 6 codec implementations in a single binary - leveraging system libraries for minimal overhead. Actual binary size depends on your build configuration and linked compression libraries.

## Codec Characteristics

Each algorithm has different speed/compression tradeoffs. The smart engine benchmarks your specific data:

| Algorithm | Characteristics | Best For |
|-----------|-----------------|----------|
| **zstd** | Balanced, fast compression | General use, modern systems |
| **gzip** | Legacy, widely supported | Compatibility, existing toolchains |
| **brotli** | Strong compression, slower | Web assets, highly compressible text |
| **lz4** | Ultra-fast, weak compression | Real-time, streaming scenarios |
| **xz** | Maximum compression, very slow | Archival, maximum density |
| **zip** | Archive format, good compatibility | Cross-platform archive needs |

**Tip**: Use `./build/mantis` and select "Analyze compression options" to benchmark all codecs with your actual data.

## C++ API

```cpp
#include "mantis/api.hpp"
#include "mantis/core/smart_engine.hpp"

// Compress with auto-selection
mantis::compress("myfile.txt", "output.zst", 6, "auto");

// Specific algorithm
mantis::compress("myfile.txt", "output.br", 6, "brotli");

// Analyze
auto stats = mantis::core::SmartEngine::instance().benchmarkAll("myfile.txt");
for (const auto& stat : stats) {
    std::cout << stat.algorithm << ": " << stat.compression_ratio << "%\n";
}

// Auto-select best
auto rec = mantis::core::SmartEngine::instance().autoSelect("myfile.txt");
std::cout << "Best: " << rec.algorithm << "\n";
```

## Build

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Repository Layout

```
mantis-shrimp/
├── include/mantis/
│   ├── api.hpp
│   ├── codecs/
│   │   ├── codec.hpp
│   │   └── registry.hpp
│   └── core/
│       └── smart_engine.hpp
├── src/
│   ├── cli/
│   │   └── main.cpp
│   ├── codecs/
│   │   ├── codec_registry.cpp
│   │   ├── zstd_codec_v2.cpp
│   │   ├── gzip_codec_v2.cpp
│   │   ├── brotli_codec.cpp
│   │   ├── lz4_codec.cpp
│   │   ├── xz_codec.cpp
│   │   └── zip_codec.cpp
│   ├── core/
│   │   └── smart_engine.cpp
│   ├── archive/
│   ├── api/
│   └── algorithms/ (legacy)
├── tests/
├── docs/
├── CMakeLists.txt
├── README.md
└── CHANGELOG.md
```

## Development Roadmap

**Phase 1 - Foundation (v0.3 ✅ CURRENT)**
- ✅ All major compression codecs (zstd, gzip, brotli, lz4, xz, zip)
- ✅ Interactive CLI with analysis and auto-selection
- ✅ Public C++ API
- ✅ Smart benchmarking engine

**Phase 2 - Polish & Stability (v0.4-0.5)**
- 🔄 Enhanced error handling and edge cases
- 🔄 Performance optimization
- 🔄 Comprehensive test coverage
- 🔄 GUI wrapper (planned)

**Phase 3 - Integration (v0.6+)**
- ⏳ Context menu integration
- ⏳ Custom codec plugins
- ⏳ Cloud streaming support

**Stability Target for Production: v1.0**

## License

See LICENSE file for details.
