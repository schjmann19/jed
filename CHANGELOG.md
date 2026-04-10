# Changelog
All notable changes to this project will be documented in this file.

## [0.2.0]
### Added
- Vi-like normal-mode commands: `h`, `j`, `k`, `l` for movement, `0` and `$` for line navigation, `x` for delete character, `dd` for delete line, `o` for open line below, `a` for append after cursor

### Fixed
- Replaced GNU `getopt_long` with POSIX `getopt` for better portability
- Added `_POSIX_C_SOURCE` macro for POSIX compliance
- Fixed Makefile dependency on undefined `BIN_DIR`
- Ensured safe null-termination of filename buffers
- Cleared stale pointers after line deletion
- Prevented repeated `atexit` registration for raw mode
- Resolved signedness warnings in cursor handling
- Increased status buffer size to avoid truncation warnings
- Removed unused variables in command handling

### Changed
- Improved cursor positioning logic for better vi compatibility


## [0.1.2]
### Compliance:
- C99 + POSIX.1-2008
- Use a Makefile and do away with my #include practices
- Flipped the order of this here file (I like it better with newest on top)


## [0.1.1]
### Added
- Implicit file making (can just open the editor and :wq with a new filename and it will create it.)

### Fixed
- Style and proxility in jed.c


## [0.1] 
### Added
- Basic text editor functionality
- Modal editing (Normal/Insert/Command modes)
- File operations (open, save)
- Terminal UI with status line
- Cursor movement with arrow keys
- Basic text insertion and deletion

### Fixed
- Memory leak in open_file()
- Buffer overflow risk in insert_char()
- Null pointer dereference risks
