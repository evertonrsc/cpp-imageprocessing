# Complete Makefile separating the different application elements into directories:
# - bin: executables (programs)
# - build: binaries
# - doc: documentation (ideally generated in an automatic way)
# - src: source code files

# Special variables:
# - $@: target name
# - $^: list with all the pre-requisites without duplicates
# - $<: name of the first pre-requisite

# Operating system commands
RM = rm -rf
MKDIR = mkdir -p

# Compiler
CXX = g++

# Directory variables
BIN_DIR = bin
BUILD_DIR = build
DOC_DIR = doc
SRC_DIR = src
INCLUDE_DIR = include

# Program name
PROG = imageprocessing

# Base compilation options
CXXFLAGS = -W -Wall -std=c++17 -I$(INCLUDE_DIR)

# Detect OS
UNAME_S := $(shell uname -s)

# Default: assume pkg-config available
PKGCONFIG := $(shell which pkg-config 2>/dev/null)

ifeq ($(UNAME_S),Darwin)   # macOS
    ifeq ($(PKGCONFIG),)
        # No pkg-config -> fallback to Homebrew
        CXXFLAGS += -I/opt/homebrew/include/opencv4
        LIBS = -L/opt/homebrew/lib -lopencv_core -lopencv_imgcodecs -lopencv_imgproc -lcurl
    else
        # pkg-config works
        CXXFLAGS += $(shell pkg-config --cflags opencv4)
        LIBS = $(shell pkg-config --libs opencv4) -lcurl
    endif
else ifeq ($(UNAME_S),Linux)
    ifeq ($(PKGCONFIG),)
        # No pkg-config -> assume system paths (may need adjustment per distribution)
        CXXFLAGS += -I/usr/include/opencv4
        LIBS = -lopencv_core -lopencv_imgcodecs -lopencv_imgproc -lcurl
    else
        CXXFLAGS += $(shell pkg-config --cflags opencv4)
        LIBS = $(shell pkg-config --libs opencv4) -lcurl
    endif
endif

# Sources and objects
SRCS = $(wildcard $(SRC_DIR)/*.cpp)
OBJS = $(SRCS:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)

# Default target
all: $(BIN_DIR)/$(PROG)

# Link program
$(BIN_DIR)/$(PROG): $(OBJS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

# Compile source into objects
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Ensure directories exist
$(BIN_DIR) $(BUILD_DIR):
	$(MKDIR) $@

# Clean target
clean:
	$(RM) -r $(BUILD_DIR) $(BIN_DIR)

# Automatically generate source code documentation with Doxygen
# Always remove the previous documentation (if it exists) and generate a new version
doxy:
	$(RM) $(DOC_DIR)/*
	doxygen -g
doc:
    $(RM) $(DOC_DIR)/*
	doxygen

.PHONY: all clean