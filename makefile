# Allow override from environment or command line
CROSS_PREFIX ?=
ARCH ?= native

# Auto-detect architecture if not set
ifeq ($(ARCH),native)
UNAME_M := $(shell uname -m)
ifeq ($(UNAME_M),x86_64)
ARCH := aarch64-linux-gnu
CROSS_PREFIX := aarch64-linux-gnu-
endif
ifeq ($(UNAME_M),aarch64)
ARCH :=
CROSS_PREFIX :=
endif
endif

# Compiler and flags
CXX = $(CROSS_PREFIX)g++
CC = $(CROSS_PREFIX)gcc
CXXFLAGS = -std=c++17 -Wall -g -Iinclude -I$(ARCH)/include -Ivendor/ws2811 -Ivendor/openssl/compiled/include -Ivendor/nlohmann_json/single_include
CFLAGS = -Wall -g -Iinclude -Ivendor/ws2811 -Ivendor/openssl/compiled/include -Ivendor/nlohmann_json/single_include
LDFLAGS = -L$(ARCH)/lib -Lvendor/openssl/compiled/lib \
		   -Wl,-rpath=$(ARCH)/lib -Wl,-rpath=vendor/openssl/compiled/lib \
		   -static-libstdc++ -static-libgcc -static -lm
# Link in OpenSSL (libssl + libcrypto) and other system libs after object files
LDLIBS = -lssl -lcrypto -ldl -pthread

# Directories
SRC_DIR = src
INC_DIR = include
VENDOR_DIR = vendor/ws2811
OPENSSL_DIR = vendor/openssl
OPENSSL_PREFIX = $(OPENSSL_DIR)/compiled
BUILD_DIR = build
TOOLS_DIR = tools
OBJ_DIR = $(BUILD_DIR)/obj
BIN_DIR = $(BUILD_DIR)/bin
DEB_DIR = $(BUILD_DIR)/deb
DEB_NAME = refrigeration
 # Make sure to update the version # in refrigeration.h
DEB_VERSION = 2.1.0
DEB_ARCH = arm64

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
TOOL_OBJS := $(patsubst $(TOOLS_DIR)/%.cpp, $(OBJ_DIR)/tools_%.o, $(TOOL_SRCS))

ALL_OBJS = $(TOOL_OBJS) $(OBJ_DIR)/config_manager.o $(OBJ_DIR)/config_validator.o $(OBJ_DIR)/sensor_manager.o

# =============================
# FTXUI (Raspberry Pi / aarch64)
# =============================

FTXUI_DIR        := vendor/FTXUI
FTXUI_LIB    := $(FTXUI_DIR)/build
FTXUI_INC    := -I$(FTXUI_DIR)/include
FTXUI_LIBS   := -L$(FTXUI_LIB) -lftxui-component -lftxui-dom -lftxui-screen
TOOLCHAIN_FILE   := $(abspath aarch64-toolchain.cmake)

# Default target
all: deb
	@echo "--------------------------------------------------------------------------------"
	@echo ""
	@echo "--------------------------------------------------------------------------------"
	@echo "Build completed successfully!"

# Linking
$(TARGET): $(OBJS) $(VENDOR_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) -o $@ $^ $(LDFLAGS) $(LDLIBS)


$(TOOL_TARGET): $(ALL_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) -o $@ $^ $(LDFLAGS) $(FTXUI_LIBS) $(LDLIBS)


# Compiling C++ files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp openssl
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@


# Compiling tool C++ files
$(OBJ_DIR)/tools_%.o: $(TOOLS_DIR)/%.cpp  ftxui_build
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ivendor/FTXUI/include -c $< -o $@

# Compiling vendor C files
$(OBJ_DIR)/vendor/%.o: $(VENDOR_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

deb: $(TARGET) $(TOOL_TARGET)
	@echo "Building .deb package..."
	rm -rf $(DEB_DIR)
	mkdir -p $(DEB_DIR)/DEBIAN
	mkdir -p $(DEB_DIR)/usr/bin
	mkdir -p $(DEB_DIR)/usr/share/doc/$(DEB_NAME)
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
	chmod +x $(DEB_DIR)/DEBIAN/postinst

	# Pre-removal script
	echo "#!/bin/bash" > $(DEB_DIR)/DEBIAN/prerm
	echo "systemctl stop $(DEB_NAME).service" >> $(DEB_DIR)/DEBIAN/prerm
	echo "systemctl disable $(DEB_NAME).service" >> $(DEB_DIR)/DEBIAN/prerm
	chmod +x $(DEB_DIR)/DEBIAN/prerm

	# Copy service file
	cp services/$(DEB_NAME).service $(DEB_DIR)/etc/systemd/system/$(DEB_NAME).service

	# Add this to your packaging target (e.g. deb) in makefile
	# Copy main LICENSE and third-party licenses to package
	cp LICENSE $(DEB_DIR)/usr/share/doc/$(DEB_NAME)/LICENSE
	cp THIRD_PARTY_LICENSES.md $(DEB_DIR)/usr/share/doc/$(DEB_NAME)/THIRD_PARTY_LICENSES
	cp vendor/openssl/LICENSE.txt $(DEB_DIR)/usr/share/doc/$(DEB_NAME)/openssl_LICENSE
	cp vendor/ws2811/LICENSE $(DEB_DIR)/usr/share/doc/$(DEB_NAME)/ws2811_LICENSE
	cp vendor/nlohmann_json/LICENSE.MIT $(DEB_DIR)/usr/share/doc/$(DEB_NAME)/nlohmann_json_LICENSE

	# Copy compiled binaries
	cp $(TARGET) $(DEB_DIR)/usr/bin/$(DEB_NAME)
	cp $(TOOL_TARGET) $(DEB_DIR)/usr/bin/tech-tool

	# Build the .deb
	dpkg-deb --build $(DEB_DIR) $(BUILD_DIR)/$(DEB_NAME)_$(DEB_VERSION)_$(DEB_ARCH).deb
	@echo "--------------------------------------------------------------------------------"
	@echo ".deb package built: $(BUILD_DIR)/$(DEB_NAME)_$(DEB_VERSION)_$(DEB_ARCH).deb"

	@cp $(BUILD_DIR)/$(DEB_NAME)_$(DEB_VERSION)_$(DEB_ARCH).deb ./

openssl:
	@echo "Building OpenSSL into $(OPENSSL_PREFIX) for target $(ARCH)"
	@mkdir -p $(OPENSSL_DIR)
	@if [ -d "$(OPENSSL_PREFIX)" ]; then \
		echo "OpenSSL already built at $(OPENSSL_PREFIX) - remove it with 'make clean' to rebuild"; \
	else \
		cd $(OPENSSL_DIR) && rm -rf compiled && \
		CC=$(CROSS_PREFIX)gcc perl Configure linux-aarch64 no-shared --prefix=$$(pwd)/compiled && \
		make -j$$(nproc) && \
		make install_sw; \
	fi

ftxui_build:
	@echo "[FTXUI] Building for Raspberry Pi (aarch64)..."
	cmake -S $(FTXUI_DIR) -B $(FTXUI_LIB) -DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN_FILE)
	cmake --build $(FTXUI_LIB)

# Clean up
clean:
	rm -rf $(BUILD_DIR)
	rm -f ./$(DEB_NAME)_$(DEB_VERSION)_$(DEB_ARCH).deb


clean-all: clean
	rm -rf $(FTXUI_LIB)
	rm -rf $(OPENSSL_PREFIX)
	@if [ -d "$(OPENSSL_DIR)" ]; then \
		$(MAKE) -C $(OPENSSL_DIR) clean || true; \
	fi

.PHONY: all clean server openssl clean-all deb ftxui_build

