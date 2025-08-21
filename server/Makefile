# Server Makefile
# Compile the C++ server without external dependencies

# Compiler and flags
CXX = g++
CXXFLAGS = -std=c++17 -Wall -g -Iinclude -I../include
LDFLAGS = -pthread -lssl -lcrypto -lstdc++fs

# Directories
SRC_DIR = src
INC_DIR = include
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
BIN_DIR = $(BUILD_DIR)/bin

# Executable name
TARGET = $(BIN_DIR)/secure_server

# Source and object files
SRCS := $(wildcard $(SRC_DIR)/*.cpp)
OBJS := $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(SRCS))

# Default target
all: $(TARGET)

# Linking
$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) -o $@ $^ $(LDFLAGS)

# Compiling C++ files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean up
clean:
	rm -rf $(BUILD_DIR)

# Run the server
run: $(TARGET)
	cd .. && ./server/$(TARGET)

.PHONY: all clean run