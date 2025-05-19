CROSS_PREFIX = arm-linux-gnueabihf-
ARCH = arm-linux-gnueabihf

# Compiler and flags
CXX = $(CROSS_PREFIX)g++
CXXFLAGS = -std=c++17 -Wall -g -Iinclude -I$(ARCH)/include -mfpu=neon-vfpv4 -march=armv7-a -mtune=cortex-a7
LDFLAGS = -L$(ARCH)/lib -Wl,-rpath=$(ARCH)/lib -static-libstdc++ -static-libgcc
LDFLAGS += -static

# Directories
SRC_DIR = src
INC_DIR = include
BUILD_DIR = build
TOOLS_DIR = tools
OBJ_DIR = $(BUILD_DIR)/obj
BIN_DIR = $(BUILD_DIR)/bin

# Executable name
TARGET = $(BIN_DIR)/refrigeration
TOOL_TARGET = $(BIN_DIR)/config_editor

# Source and object files
SRCS := $(wildcard $(SRC_DIR)/*.cpp)
OBJS := $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(SRCS))

# Source and object files for tools
TOOL_SRCS := $(wildcard $(TOOLS_DIR)/*.cpp)  # Find all .cpp files in tools directory
TOOL_OBJS := $(patsubst $(TOOLS_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(TOOL_SRCS))

ALL_OBJS = $(TOOL_OBJS) $(OBJ_DIR)/config_manager.o $(OBJ_DIR)/config_validator.o

# Default target
all: $(TARGET) $(TOOL_TARGET)

# Linking
$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(LDFLAGS) -o $@ $^

$(TOOL_TARGET): $(ALL_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(LDFLAGS) -o $@ $^

# Compiling
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(TOOLS_DIR)/%.cpp
	@mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean up
clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
