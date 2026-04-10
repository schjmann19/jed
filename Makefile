# Makefile for jed - a bare-bones text editor
# POSIX.1-2008 compliant (no GNU Make extensions)

# Compiler and flags
CC      ?= cc
CSTD    = -std=c99
CFLAGS  = $(CSTD) -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2
LDFLAGS =
LDLIBS  =

# Directories
SRC_DIR = src
OBJ_DIR = obj

# Output binary
BINARY  = jed

# Source and object files
SOURCES = $(SRC_DIR)/main.c $(SRC_DIR)/jed.c $(SRC_DIR)/aux.c
HEADERS = $(SRC_DIR)/jed.h $(SRC_DIR)/aux.h
OBJECTS = $(OBJ_DIR)/main.o $(OBJ_DIR)/jed.o $(OBJ_DIR)/aux.o

# Default target
all: $(BINARY)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Linking
$(BINARY): $(OBJECTS) $(BIN_DIR)
	$(CC) $(LDFLAGS) -o $(BINARY) $(OBJECTS) $(LDLIBS)

# Compilation rules
$(OBJ_DIR)/main.o: $(SRC_DIR)/main.c $(HEADERS) $(OBJ_DIR)
	$(CC) $(CFLAGS) -c -o $(OBJ_DIR)/main.o $(SRC_DIR)/main.c

$(OBJ_DIR)/jed.o: $(SRC_DIR)/jed.c $(HEADERS) $(OBJ_DIR)
	$(CC) $(CFLAGS) -c -o $(OBJ_DIR)/jed.o $(SRC_DIR)/jed.c

$(OBJ_DIR)/aux.o: $(SRC_DIR)/aux.c $(SRC_DIR)/aux.h $(OBJ_DIR)
	$(CC) $(CFLAGS) -c -o $(OBJ_DIR)/aux.o $(SRC_DIR)/aux.c

# Cleaning targets
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)


# Declare phony targets
.PHONY: all clean distclean install uninstall help check
