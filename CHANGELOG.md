# Changelog
All notable changes to this project will be documented in this file.

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
