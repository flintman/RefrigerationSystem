# Refrigeration API Web Interface Makefile
# Builds a standalone web interface that aggregates data from multiple refrigeration units via REST API
# Supports cross-compilation for aarch64 and x86_64

# Versioning for .deb package
VERSION := 1.0.0

# Architecture options: aarch64, x86_64, or native (auto-detect)
ARCH ?= native

# Auto-detect architecture if not explicitly set
ifeq ($(ARCH),native)
UNAME_M := $(shell uname -m)
ifeq ($(UNAME_M),aarch64)
ARCH := aarch64
DEB_ARCH := arm64
else ifeq ($(UNAME_M),x86_64)
ARCH := x86_64
DEB_ARCH := amd64
else
$(error Unsupported architecture: $(UNAME_M). Please specify ARCH=aarch64 or ARCH=x86_64)
endif
endif

# Set cross-compiler prefix based on target architecture
ifeq ($(ARCH),aarch64)
CROSS_PREFIX := aarch64-linux-gnu-
ARCH_LIB_DIR := aarch64-linux-gnu
else ifeq ($(ARCH),x86_64)
CROSS_PREFIX :=
ARCH_LIB_DIR := x86_64-linux-gnu
else
$(error Unsupported ARCH: $(ARCH). Use ARCH=aarch64 or ARCH=x86_64)
endif

# Compiler and flags
CXX = $(CROSS_PREFIX)g++
CC = $(CROSS_PREFIX)gcc
CXXFLAGS = -std=c++17 -Wall -g -I../../include -Ivendor -I../../vendor/nlohmann_json/single_include -fPIC
LDFLAGS = -L../../$(ARCH_LIB_DIR)/lib -L../../vendor/openssl/compiled/lib -Wl,-rpath=../../vendor/openssl/compiled/lib -static-libstdc++ -static-libgcc -lm
LDLIBS = -lcurl -lssl -lcrypto -ldl -pthread

# Directories
SRC_DIR = src
INC_DIR = ../../include
BUILD_DIR = ../../build
OBJ_DIR = $(BUILD_DIR)/web-api/$(ARCH)/obj
BIN_DIR = $(BUILD_DIR)/web-api/$(ARCH)/bin
STATIC_DIR = $(BIN_DIR)/static
TEMPLATES_DIR = $(BIN_DIR)/templates

# Executable name
TARGET = $(BIN_DIR)/api-web-interface
DEB_BUILD_DIR := $(BUILD_DIR)/deb-build
DEB_PACKAGE := ../../web-api_$(VERSION)_$(DEB_ARCH)

# Default target
.PHONY: all clean deb
SRCS := $(wildcard $(SRC_DIR)/*.cpp)
OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))

# Default target
.PHONY: all clean

all: deb

$(TARGET): $(OBJS) | $(BIN_DIR)
	@echo "Linking API Web Interface for $(ARCH): $@"
	@$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)
	@echo "API Web Interface built successfully for $(ARCH): $@"
	@echo "Copying static files and templates..."
	@mkdir -p $(STATIC_DIR)
	@mkdir -p $(TEMPLATES_DIR)
	@if [ -d "static" ]; then cp -r static/* $(STATIC_DIR)/ 2>/dev/null || true; fi
	@if [ -d "templates" ]; then cp -r templates/* $(TEMPLATES_DIR)/ 2>/dev/null || true; fi
	@echo "Static files and templates copied"

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	@mkdir -p $(@D)
	@echo "Compiling: $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN_DIR):
	@mkdir -p $@

$(OBJ_DIR):
	@mkdir -p $@

# Build .deb package
deb: $(TARGET)
	@echo "Building $(DEB_PACKAGE).deb..."
	@rm -rf $(DEB_BUILD_DIR)
	@mkdir -p $(DEB_BUILD_DIR)/DEBIAN
	@mkdir -p $(DEB_BUILD_DIR)/usr/bin
	@mkdir -p $(DEB_BUILD_DIR)/usr/share/web-api
	@mkdir -p $(DEB_BUILD_DIR)/etc/web-api
	@mkdir -p $(DEB_BUILD_DIR)/etc/systemd/system

	# Create control file
	@echo "Package: web-api" > $(DEB_BUILD_DIR)/DEBIAN/control
	@echo "Version: $(VERSION)" >> $(DEB_BUILD_DIR)/DEBIAN/control
	@echo "Architecture: $(DEB_ARCH)" >> $(DEB_BUILD_DIR)/DEBIAN/control
	@echo "Maintainer: William Bellvance Jr <william@example.com>" >> $(DEB_BUILD_DIR)/DEBIAN/control
	@echo "Depends: libc6, libssl3, libcurl4" >> $(DEB_BUILD_DIR)/DEBIAN/control
	@echo "Homepage: https://github.com/yourusername/RefrigerationSystem" >> $(DEB_BUILD_DIR)/DEBIAN/control
	@echo "Description: Web API for Refrigeration System" >> $(DEB_BUILD_DIR)/DEBIAN/control
	@echo " REST API interface for managing refrigeration units" >> $(DEB_BUILD_DIR)/DEBIAN/control
	@echo " with email notifications, demo mode, and log management." >> $(DEB_BUILD_DIR)/DEBIAN/control

	# Create systemd service file
	@echo "[Unit]" > $(DEB_BUILD_DIR)/etc/systemd/system/web-api.service
	@echo "Description=Web API for Refrigeration System" >> $(DEB_BUILD_DIR)/etc/systemd/system/web-api.service
	@echo "After=network.target" >> $(DEB_BUILD_DIR)/etc/systemd/system/web-api.service
	@echo "" >> $(DEB_BUILD_DIR)/etc/systemd/system/web-api.service
	@echo "[Service]" >> $(DEB_BUILD_DIR)/etc/systemd/system/web-api.service
	@echo "Type=simple" >> $(DEB_BUILD_DIR)/etc/systemd/system/web-api.service
	@echo "User=root" >> $(DEB_BUILD_DIR)/etc/systemd/system/web-api.service
	@echo "WorkingDirectory=/etc/web-api" >> $(DEB_BUILD_DIR)/etc/systemd/system/web-api.service
	@echo "ExecStart=/usr/bin/web-api" >> $(DEB_BUILD_DIR)/etc/systemd/system/web-api.service
	@echo "Restart=on-failure" >> $(DEB_BUILD_DIR)/etc/systemd/system/web-api.service
	@echo "RestartSec=10" >> $(DEB_BUILD_DIR)/etc/systemd/system/web-api.service
	@echo "StandardOutput=journal" >> $(DEB_BUILD_DIR)/etc/systemd/system/web-api.service
	@echo "StandardError=journal" >> $(DEB_BUILD_DIR)/etc/systemd/system/web-api.service
	@echo "SyslogIdentifier=web-api" >> $(DEB_BUILD_DIR)/etc/systemd/system/web-api.service
	@echo "" >> $(DEB_BUILD_DIR)/etc/systemd/system/web-api.service
	@echo "[Install]" >> $(DEB_BUILD_DIR)/etc/systemd/system/web-api.service
	@echo "WantedBy=multi-user.target" >> $(DEB_BUILD_DIR)/etc/systemd/system/web-api.service

	# Create postinst script
	@echo '#!/bin/bash' > $(DEB_BUILD_DIR)/DEBIAN/postinst
	@echo 'set -e' >> $(DEB_BUILD_DIR)/DEBIAN/postinst
	@echo 'systemctl daemon-reload' >> $(DEB_BUILD_DIR)/DEBIAN/postinst
	@echo 'systemctl enable web-api.service' >> $(DEB_BUILD_DIR)/DEBIAN/postinst
	@echo 'echo ""' >> $(DEB_BUILD_DIR)/DEBIAN/postinst
	@echo 'echo "✓ Web API installed successfully"' >> $(DEB_BUILD_DIR)/DEBIAN/postinst
	@echo 'echo ""' >> $(DEB_BUILD_DIR)/DEBIAN/postinst
	@echo 'echo "Next steps:"' >> $(DEB_BUILD_DIR)/DEBIAN/postinst
	@echo 'echo "1. Edit configuration file:"' >> $(DEB_BUILD_DIR)/DEBIAN/postinst
	@echo 'echo "   sudo nano /etc/web-api/web_interface_config.env"' >> $(DEB_BUILD_DIR)/DEBIAN/postinst
	@echo 'echo ""' >> $(DEB_BUILD_DIR)/DEBIAN/postinst
	@echo 'echo "2. Start the service:"' >> $(DEB_BUILD_DIR)/DEBIAN/postinst
	@echo 'echo "   sudo systemctl start web-api"' >> $(DEB_BUILD_DIR)/DEBIAN/postinst
	@echo 'echo ""' >> $(DEB_BUILD_DIR)/DEBIAN/postinst
	@echo 'echo "3. Check service status:"' >> $(DEB_BUILD_DIR)/DEBIAN/postinst
	@echo 'echo "   sudo systemctl status web-api"' >> $(DEB_BUILD_DIR)/DEBIAN/postinst
	@echo 'echo ""' >> $(DEB_BUILD_DIR)/DEBIAN/postinst
	@echo 'echo "4. View logs:"' >> $(DEB_BUILD_DIR)/DEBIAN/postinst
	@echo 'echo "   sudo journalctl -u web-api -f"' >> $(DEB_BUILD_DIR)/DEBIAN/postinst
	@echo 'echo ""' >> $(DEB_BUILD_DIR)/DEBIAN/postinst
	@echo 'exit 0' >> $(DEB_BUILD_DIR)/DEBIAN/postinst
	@chmod 755 $(DEB_BUILD_DIR)/DEBIAN/postinst

	# Create postrm script
	@echo '#!/bin/bash' > $(DEB_BUILD_DIR)/DEBIAN/postrm
	@echo 'set -e' >> $(DEB_BUILD_DIR)/DEBIAN/postrm
	@echo 'systemctl disable web-api.service 2>/dev/null || true' >> $(DEB_BUILD_DIR)/DEBIAN/postrm
	@echo 'systemctl daemon-reload' >> $(DEB_BUILD_DIR)/DEBIAN/postrm
	@echo 'exit 0' >> $(DEB_BUILD_DIR)/DEBIAN/postrm
	@chmod 755 $(DEB_BUILD_DIR)/DEBIAN/postrm

	# Copy binary and files
	@cp $(TARGET) $(DEB_BUILD_DIR)/usr/bin/web-api
	@chmod 755 $(DEB_BUILD_DIR)/usr/bin/web-api
	@if [ -d "static" ]; then cp -r static $(DEB_BUILD_DIR)/usr/share/web-api/ 2>/dev/null || true; fi
	@if [ -d "templates" ]; then cp -r templates $(DEB_BUILD_DIR)/usr/share/web-api/ 2>/dev/null || true; fi
	@if [ -f "web_interface_config.env" ]; then cp web_interface_config.env $(DEB_BUILD_DIR)/etc/web-api/web_interface_config.env.sample; fi

	# Build package
	@dpkg-deb --build $(DEB_BUILD_DIR) $(DEB_PACKAGE).deb
	@echo "✓ Package created: $(DEB_PACKAGE).deb"

clean:
	@rm -rf $(BUILD_DIR)
	@rm -rf $(DEB_PACKAGE).deb
	@echo "API Web Interface cleaned for $(ARCH)"

