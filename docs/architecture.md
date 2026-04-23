# Mantis Shrimp v0.3.0 Architecture

Mantis Shrimp v0.3.0 is a multi-codec compression utility that ships with all supported codecs enabled:

- `zstd`
- `gzip`
- `brotli`
- `lz4`
- `xz`
- `zip`

## Project Layout

- `src/cli/`: interactive terminal application (`mantis`)
- `include/mantis/api.hpp`: stable public API (`analyze`, `compress`, `extract`)
- `src/core/`: orchestration and smart codec selection (`Engine`, `SmartEngine`)
- `src/codecs/`: codec implementations and codec registry
- `src/archive/`: TAR writer used for directory packaging
- `tests/`: codec roundtrip, smart selection, and edge-case validation

## Runtime Design

1. `CodecRegistry` registers all codecs at startup.
2. `SmartEngine` benchmarks available codecs and recommends the best ratio.
3. `Engine` routes API calls for compression/extraction behavior.
4. CLI workflows call the same public API exposed to C++ consumers.

## Compression and Extraction Flow

- File compression: codec-specific output (`.zst`, `.gz`, `.br`, `.lz4`, `.xz`, `.zip`)
- Directory compression: archived as TAR before codec compression (where applicable)
- Extraction: format-aware decompression path with output handling for both file and archive workflows

## Build and Packaging

- CMake-based build (`C++20`, CMake >= 3.20)
- Installs executable, library, and public headers via `install` target
- Generates distributable archives via CPack (`.tar.gz`, `.zip`)
