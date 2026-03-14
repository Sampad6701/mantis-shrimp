# Mantis Shrimp v0.1

The v0.1 baseline follows the architecture described in `mantis_shrimp_v0_1.pdf`:

- `cli/`: command-line frontend exposed through the `mantis` binary
- Public API: `include/mantis/api.hpp`
- `core/`: orchestration for analysis and compression routing
- `algorithms/`: Zstandard streaming compression
- `archive/`: TAR packaging for directories
- `tests/`: integrity and packaging validation

Implemented v0.1 commands:

- `mantis compress <path>` compresses a file to `.zst` or a directory to `.tar.zst`
- `mantis analyze <path>` inspects whether the target is a file or directory

`extract` exists in the public API and CLI surface as a forward-compatible placeholder, but
archive extraction is intentionally deferred to a later version.
