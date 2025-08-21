# Allow override from environment or command line
CROSS_PREFIX ?=
ARCH ?= native

# Auto-detect architecture if not set
ifeq ($(ARCH),native)
UNAME_M := $(shell uname -m)
ifeq ($(UNAME_M),x86_64)
ARCH := arm-linux-gnueabihf
CROSS_PREFIX := arm-linux-gnueabihf-
endif
ifeq ($(UNAME_M),aarch64)
ARCH :=
CROSS_PREFIX :=
endif
endif

# Compiler and flags
CXX = $(CROSS_PREFIX)g++
CC = $(CROSS_PREFIX)gcc
CXXFLAGS = -std=c++17 -Wall -g -Iinclude -I$(ARCH)/include -Ivendor/ws2811 -Ivendor/openssl/include
CFLAGS = -Wall -g -Iinclude -Ivendor/ws2811 -Ivendor/openssl/include
LDFLAGS = -L$(ARCH)/lib -Lvendor/openssl/lib -Wl,-rpath=$(ARCH)/lib -Wl,-rpath=vendor/openssl/lib -static-libstdc++ -static-libgcc
LDFLAGS += -static -lm

# Directories
SRC_DIR = src
INC_DIR = include
VENDOR_DIR = vendor/ws2811
BUILD_DIR = build
TOOLS_DIR = tools
OBJ_DIR = $(BUILD_DIR)/obj
BIN_DIR = $(BUILD_DIR)/bin
DEB_DIR = $(BUILD_DIR)/deb
DEB_NAME = refrigeration
 # Make sure to update the version # in refrigeration.h
DEB_VERSION = 1.1.1
DEB_ARCH = armhf

# Executable name
TARGET = $(BIN_DIR)/refrigeration
TOOL_TARGET = $(BIN_DIR)/tech-tool

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
all: deb
	@echo "--------------------------------------------------------------------------------"
	@echo ""
	@echo "--------------------------------------------------------------------------------"
	@echo "Build completed successfully!"

# Server target
server:
	@echo "Building C++ server..."
	$(MAKE) -C server
	@echo "Server build completed!"

# Clean server
clean-server:
	$(MAKE) -C server clean

# Linking
$(TARGET): $(OBJS) $(VENDOR_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(LDFLAGS) -o $@ $^ -lssl -lcrypto

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

deb: $(TARGET) $(TOOL_TARGET)
	@echo "Building .deb package..."
	rm -rf $(DEB_DIR)
	mkdir -p $(DEB_DIR)/DEBIAN
	mkdir -p $(DEB_DIR)/usr/bin
	mkdir -p $(DEB_DIR)/etc/systemd/system/
	mkdir -p $(DEB_DIR)/etc/refrigeration

	# Control file
	echo "Package: $(DEB_NAME)" > $(DEB_DIR)/DEBIAN/control
	echo "Version: $(DEB_VERSION)" >> $(DEB_DIR)/DEBIAN/control
	echo "Section: base" >> $(DEB_DIR)/DEBIAN/control
	echo "Priority: optional" >> $(DEB_DIR)/DEBIAN/control
	echo "Architecture: $(DEB_ARCH)" >> $(DEB_DIR)/DEBIAN/control
	echo "Maintainer: William Bellvance Jr <william@bellavance.co>" >> $(DEB_DIR)/DEBIAN/control
	echo "Description: Refrigeration system controller and config tool" >> $(DEB_DIR)/DEBIAN/control

	# Post install script
	echo "#!/bin/bash" > $(DEB_DIR)/DEBIAN/postinst
	echo "systemctl daemon-reexec" >> $(DEB_DIR)/DEBIAN/postinst
	echo "systemctl daemon-reload" >> $(DEB_DIR)/DEBIAN/postinst
	echo "systemctl enable $(DEB_NAME).service" >> $(DEB_DIR)/DEBIAN/postinst
	echo "systemctl start $(DEB_NAME).service" >> $(DEB_DIR)/DEBIAN/postinst
	chmod +x $(DEB_DIR)/DEBIAN/postinst

	# Pre-removal script
	echo "#!/bin/bash" > $(DEB_DIR)/DEBIAN/prerm
	echo "systemctl stop $(DEB_NAME).service" >> $(DEB_DIR)/DEBIAN/prerm
	echo "systemctl disable $(DEB_NAME).service" >> $(DEB_DIR)/DEBIAN/prerm
	chmod +x $(DEB_DIR)/DEBIAN/prerm

	# Copy service file
	cp services/$(DEB_NAME).service $(DEB_DIR)/etc/systemd/system/$(DEB_NAME).service

	# Copy compiled binaries
	cp $(TARGET) $(DEB_DIR)/usr/bin/$(DEB_NAME)
	cp $(TOOL_TARGET) $(DEB_DIR)/usr/bin/tech-tool

	# Build the .deb
	dpkg-deb --build $(DEB_DIR) $(BUILD_DIR)/$(DEB_NAME)_$(DEB_VERSION)_$(DEB_ARCH).deb
	@echo "--------------------------------------------------------------------------------"
	@echo ".deb package built: $(BUILD_DIR)/$(DEB_NAME)_$(DEB_VERSION)_$(DEB_ARCH).deb"

# Clean up
clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean server clean-server