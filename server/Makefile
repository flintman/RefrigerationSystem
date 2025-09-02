# Server Makefile
# Compile the C++ server without external dependencies

# Compiler and flags
CXX = g++
CXXFLAGS = -std=c++17 -Wall -g -Iinclude -I../include
LDFLAGS = -pthread -lssl -lcrypto -lstdc++fs -lcurl

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

DEB_DIR = $(BUILD_DIR)/deb
DEB_NAME = refrigeration-server
DEB_VERSION = 1.0.1
DEB_ARCH = $(shell dpkg --print-architecture)

SYSTEMD_DIR = $(DEB_DIR)/lib/systemd/system
SERVICE_FILE = $(SYSTEMD_DIR)/refrigeration-server.service

all: deb

deb: $(TARGET)
	@echo "Building .deb package..."
	@rm -rf $(DEB_DIR)
	@mkdir -p $(DEB_DIR)/DEBIAN
	@mkdir -p $(DEB_DIR)/usr/bin
	@mkdir -p $(DEB_DIR)/$(DEB_NAME)
	@mkdir -p $(DEB_DIR)/$(DEB_NAME)/templates
	@mkdir -p $(DEB_DIR)/$(DEB_NAME)/static
	@mkdir -p $(SYSTEMD_DIR)

    # Control file
	@echo "Package: $(DEB_NAME)" > $(DEB_DIR)/DEBIAN/control
	@echo "Version: $(DEB_VERSION)" >> $(DEB_DIR)/DEBIAN/control
	@echo "Section: base" >> $(DEB_DIR)/DEBIAN/control
	@echo "Priority: optional" >> $(DEB_DIR)/DEBIAN/control
	@echo "Architecture: $(DEB_ARCH)" >> $(DEB_DIR)/DEBIAN/control
	@echo "Maintainer: William Bellvance Jr <william@bellavance.co>" >> $(DEB_DIR)/DEBIAN/control
	@echo "Description: Refrigeration system controller and web server" >> $(DEB_DIR)/DEBIAN/control

    # Post install script (move files to /home/username/refrigeration-server)
	@echo "#!/bin/bash" > $(DEB_DIR)/DEBIAN/postinst
	@echo "USERNAME=\$$(logname)" >> $(DEB_DIR)/DEBIAN/postinst
	@echo "INSTALL_DIR=/home/\$${USERNAME}/refrigeration-server" >> $(DEB_DIR)/DEBIAN/postinst
	@echo "mkdir -p \$${INSTALL_DIR}" >> $(DEB_DIR)/DEBIAN/postinst
	@echo "cp -r /$(DEB_NAME)/* \$${INSTALL_DIR}/" >> $(DEB_DIR)/DEBIAN/postinst
	@echo "chown -R \$${USERNAME}: \$${INSTALL_DIR}" >> $(DEB_DIR)/DEBIAN/postinst
	@echo "systemctl enable refrigeration-server.service" >> $(DEB_DIR)/DEBIAN/postinst
	@chmod +x $(DEB_DIR)/DEBIAN/postinst
	@echo "rm -R /$(DEB_NAME)" >> $(DEB_DIR)/DEBIAN/postinst

    # Systemd service file
	@echo "[Unit]" > $(SERVICE_FILE)
	@echo "Description=Refrigeration server C++ Program" >> $(SERVICE_FILE)
	@echo "After=network.target multi-user.target" >> $(SERVICE_FILE)
	@echo "" >> $(SERVICE_FILE)
	@echo "[Service]" >> $(SERVICE_FILE)
	@echo "Type=simple" >> $(SERVICE_FILE)
	@echo "WorkingDirectory=/usr/bin/" >> $(SERVICE_FILE)
	@echo "User=flintman" >> $(SERVICE_FILE)
	@echo "ExecStart=secure_server" >> $(SERVICE_FILE)
	@echo "KillMode=control-group" >> $(SERVICE_FILE)
	@echo "KillSignal=SIGINT" >> $(SERVICE_FILE)
	@echo "TimeoutStopSec=30s" >> $(SERVICE_FILE)
	@echo "Restart=always" >> $(SERVICE_FILE)
	@echo "RestartSec=10s" >> $(SERVICE_FILE)
	@echo "" >> $(SERVICE_FILE)
	@echo "[Install]" >> $(SERVICE_FILE)
	@echo "WantedBy=multi-user.target" >> $(SERVICE_FILE)

    # Copy compiled binary
	@mkdir -p $(DEB_DIR)/usr/bin/
	@cp $(TARGET) $(DEB_DIR)/usr/bin/

    # Copy config files, templates, static files, etc.
	@cp -r templates/* $(DEB_DIR)/$(DEB_NAME)/templates/
	@cp -r static/* $(DEB_DIR)/$(DEB_NAME)/static/

    # Build the .deb
	dpkg-deb --build $(DEB_DIR) $(BUILD_DIR)/$(DEB_NAME)_$(DEB_VERSION)_$(DEB_ARCH).deb
	@echo "--------------------------------------------------------------------------------"
	@echo "--------------------------------------------------------------------------------"
	@echo ".deb package built: $(abspath $(BUILD_DIR))/$(DEB_NAME)_$(DEB_VERSION)_$(DEB_ARCH).deb"
	@echo "--------------------------------------------------------------------------------"
	@echo "--------------------------------------------------------------------------------"


clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean deb