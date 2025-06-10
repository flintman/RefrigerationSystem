CROSS_PREFIX = arm-linux-gnueabihf-
ARCH = arm-linux-gnueabihf

# Compiler and flags
CXX = $(CROSS_PREFIX)g++
CC = $(CROSS_PREFIX)gcc
CXXFLAGS = -std=c++17 -Wall -g -Iinclude -I$(ARCH)/include -Ivendor/ws2811 -mfpu=neon-vfpv4 -march=armv7-a -mtune=cortex-a7
CFLAGS = -Wall -g -Iinclude -Ivendor/ws2811 -mfpu=neon-vfpv4 -march=armv7-a -mtune=cortex-a7
LDFLAGS = -L$(ARCH)/lib -Wl,-rpath=$(ARCH)/lib -static-libstdc++ -static-libgcc
LDFLAGS += -static -lm

# Directories
SRC_DIR = src
INC_DIR = include
VENDOR_DIR = vendor/ws2811
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

# Vendor library files
VENDOR_SRCS := $(wildcard $(VENDOR_DIR)/*.c)
VENDOR_OBJS := $(patsubst $(VENDOR_DIR)/%.c, $(OBJ_DIR)/vendor/%.o, $(VENDOR_SRCS))

# Source and object files for tools
TOOL_SRCS := $(wildcard $(TOOLS_DIR)/*.cpp)
TOOL_OBJS := $(patsubst $(TOOLS_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(TOOL_SRCS))

ALL_OBJS = $(TOOL_OBJS) $(OBJ_DIR)/config_manager.o $(OBJ_DIR)/config_validator.o $(OBJ_DIR)/sensor_manager.o

# Default target
all: $(TARGET) $(TOOL_TARGET)

# Linking
$(TARGET): $(OBJS) $(VENDOR_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(LDFLAGS) -o $@ $^

$(TOOL_TARGET): $(ALL_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(LDFLAGS) -o $@ $^

# Compiling C++ files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Compiling tool C++ files
$(OBJ_DIR)/%.o: $(TOOLS_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Compiling vendor C files
$(OBJ_DIR)/vendor/%.o: $(VENDOR_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up
clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean