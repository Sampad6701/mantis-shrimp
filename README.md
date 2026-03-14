# Mantis Shrimp

Mantis Shrimp v0.1 is a modular compression engine written in C++. The baseline release
provides a CLI and public API for:

- Streaming compression of individual files with Zstandard
- TAR packaging plus Zstandard compression for directories
- Basic path analysis through the shared engine layer

## Quick Start

```bash
# Clone the repository
git clone https://github.com/Sampad6701/mantis-shrimp.git
cd mantis-shrimp

# Install dependencies (Ubuntu/Debian example)
sudo apt install cmake g++ libzstd-dev pkg-config

# Build
cmake -S . -B build
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure

# Try it out
echo "Hello, world!" > example.txt
./build/mantis compress example.txt
```

## Prerequisites

- CMake 3.20 or higher
- C++20 compatible compiler (GCC 10+, Clang 10+, or MSVC 2019+)
- libzstd development libraries
- pkg-config

### Install dependencies

**Ubuntu/Debian:**
```bash
sudo apt update
sudo apt install cmake g++ libzstd-dev pkg-config
```

**macOS:**
```bash
brew install cmake zstd pkg-config
```

**Fedora/RHEL:**
```bash
sudo dnf install cmake gcc-c++ libzstd-devel pkgconfig
```

## Build

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## CLI

```bash
./build/mantis compress path/to/file.txt
./build/mantis compress path/to/folder
./build/mantis analyze path/to/file-or-folder
```

Files are compressed to `name.zst`. Directories are packaged to `name.tar.zst`.

## Repository layout

- `include/mantis/`: public API
- `src/cli/`: CLI frontend
- `src/core/`: orchestration and validation
- `src/algorithms/`: compression modules
- `src/archive/`: TAR packaging
- `tests/`: integrity tests
- `docs/`: project notes
