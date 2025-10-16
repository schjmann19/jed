# Changelog
All notable changes to this project will be documented in this file.

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

## [0.1.1]
### Added
- Implicit file making (can just open the editor and :wq with a new filename and it will create it.)

### Fixed
- Style and proxility in jed.c
