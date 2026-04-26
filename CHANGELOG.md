# Changelog

All notable changes to Mantis Shrimp are documented in this file.

## Project Status

**Current Phase**: Polish & Stability (v0.4.x) - Under Development
- Full feature implementation for core compression functionality
- Not yet production-ready - breaking changes possible
- API and CLI may evolve
- Stability target: v1.0

---

## [0.4.0-dev] - Under Development

### Added
- `ms` command-mode UX for all six codecs: zstd, gzip, brotli, lz4, xz, and zip
- Directory archive formats: `tar.zst`, `tar.gz`, `tar.br`, `tar.lz4`, `tar.xz`, `tar`, and `zip`
- Compression profiles: `--fast`, `--balanced`, `--max`, plus advanced `--level`
- Thread option plumbing with `--threads auto|N`
- Native threaded compression path for zstd and xz codec backends
- ZIP directory archive creation
- Symlink preservation for tar-based directory archives

### Changed
- Analysis now uses the same compression level/profile as compression by default
- CLI output now clearly states level/profile and threading behavior

## [0.3.0] - 2026-04-21

### Added
- Multi-codec compression support: Zstandard, Gzip, Brotli, LZ4, XZ, ZIP
- Codec registry system for extensible codec management
- Smart Engine for automatic algorithm benchmarking and selection
- Compression analysis feature to show ratios across all algorithms
- List codecs command to display available compression algorithms
- **Fully interactive CLI mode (default and only mode)**
  - Menu-driven interface for all operations
  - Step-by-step guided workflow
  - Real-time algorithm recommendations
  - Compression level selection with descriptions
  - Optional analysis before compression
  - Clear success/failure feedback
- Comprehensive codec interfaces with standard compression/decompression API
- Support for custom compression levels per algorithm
- Enhanced test suite with codec-specific roundtrip tests
- Empty file and large file handling tests
- BrotliCodec implementation with full compress/decompress support
- Lz4Codec implementation with level-based compression
- XzCodec implementation for maximum compression ratios
- ZipCodec implementation for archive compatibility

### Changed
- Updated CMakeLists.txt to include all compression library dependencies
- Replaced argument-based CLI with interactive menu system
- Enhanced compression API with algorithm parameter
- Improved test harness with codec registry tests
- Updated README with comprehensive v0.3 features and interactive usage examples
- Version bumped from 0.1.0 to 0.3.0

### Technical Details
- Codec implementations use consistent compression/decompression patterns
- Registry pattern allows runtime codec registration
- SmartEngine benchmarks all available codecs for auto-selection
- All codecs return CompressionStats with ratio calculations
- Error handling propagated through std::string references
- Interactive CLI uses input validation for robustness
- Menu system supports cross-platform (Windows/Unix) interaction

### Dependencies Added
- libbrotlienc (Brotli encoding)
- libbrotlidec (Brotli decoding)
- liblz4 (LZ4 compression)
- liblzma (XZ compression)
- libzip (ZIP archive format)

## [0.1.0] - Initial Release

### Added
- Streaming compression with Zstandard and Gzip
- TAR packaging for directory compression
- Auto algorithm selection based on empirical benchmarking
- Basic CLI interface
- Public C++ API
- Core compression engine
