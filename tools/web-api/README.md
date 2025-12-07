# Refrigeration API Web Interface

A modular, production-ready web API that aggregates refrigeration unit data from multiple units via REST API, monitors for alarms, sends email notifications, and provides a web dashboard.

**Version:** 1.0.0
**Architecture:** arm64 (aarch64), x86_64

## Features

- **Multi-Unit Polling**: Polls multiple refrigeration units every 30 seconds via HTTPS API
- **Alarm Detection**: Real-time monitoring with automatic email alerts on alarm conditions
- **Email Notifications**:
  - Startup notifications with timestamp (MM-DD-YYYY HH:MM:SS format)
  - Detailed alarm emails with sensor readings and system status
- **Web Dashboard**: Full HTML/CSS/JS dashboard for unit management and monitoring
- **Secure API**: RESTful API with configurable authentication
- **Log Downloads**: Download event and condition logs from refrigeration units
- **Demo Mode**: Test mode for development (controllable via debug.code setting)
- **Systemd Integration**: Install as system service for automatic startup
- **Cross-Platform**: Builds for aarch64 and x86_64 architectures

## Architecture

The web-api consists of 5 modular C++ components:

### Components

1. **ConfigManager** (`src/config_manager.cpp`)
   - Loads and monitors configuration files
   - Provides access to email settings, web port, and unit definitions
   - Hot-reloading on config file changes

2. **WebServer** (`src/web_server.cpp`)
   - Non-blocking socket server on port 9000
   - HTTP/1.1 request handling
   - Static file serving (CSS, JS, images)

3. **UnitPoller** (`src/unit_poller.cpp`)
   - 30-second polling interval for each configured unit
   - Detects alarm state changes
   - Triggers email notifications on alarm transitions

4. **EmailNotifier** (`src/email_notifier.cpp`)
   - SMTP email sending via libcurl
   - Formats startup and alarm notification emails
   - Human-readable timestamp formatting

5. **APIProxy** (`src/api_proxy.cpp`)
   - HTTPS communication with refrigeration units
   - Proxies GET/POST requests to configured units
   - Handles API key authentication

## Building

### Building .deb Package

```bash
cd tools/web-api
make          # Create .deb package (web-api_1.0.0_arm64.deb)
```

### Installing from .deb

```bash
sudo dpkg -i web-api_1.0.0_arm64.deb
```

The installer will:
- Copy binary to `/usr/bin/web-api`
- Copy static/template files to `/usr/share/web-api/`
- Create config directory at `/etc/web-api/`
- Install and enable systemd service
- Display next steps for configuration

## Installation & Configuration

### Step 1: Edit Configuration

Edit the config file (created during install or at `web_interface_config.env`):

```bash
sudo nano /etc/web-api/web_interface_config.env
```

**Configuration Format:**

```ini
# Email Configuration (for alarm notifications)
email.server=mail.example.com
email.address=alerts@example.com
email.password=your_app_password
email.port=587

# Web Server
web.port=9000
web.password=your_dashboard_password

# Debug Mode (set to "1" to enable debug, blocks demo mode)
debug.code=0

# Unit Configuration (repeat for each unit)
unit.1.id=unit-001
unit.1.address=192.168.1.100
unit.1.port=8095
unit.1.key=your-api-key-here

unit.2.id=unit-002
unit.2.address=192.168.1.101
unit.2.port=8095
unit.2.key=your-api-key-here
```

### Step 2: Start Service

```bash
sudo systemctl start web-api
```

### Verify Installation

```bash
sudo systemctl status web-api
sudo journalctl -u web-api -f    # View logs in real-time
```

## File Locations

After .deb installation:

```
/usr/bin/web-api                           # Binary executable
/etc/web-api/web_interface_config.env      # Configuration file
/usr/share/web-api/static/                 # CSS, JS, images
/usr/share/web-api/templates/              # HTML templates
/var/log/web-api/                          # Service logs
```

## API Endpoints

All endpoints respond with JSON (unless noted otherwise).

### Status & Information

- `GET /health` - Service health check (no auth required)
- `GET /api/v1/status` - Current system status
- `GET /api/v1/system-info` - System information and unit list

### Unit Data

- `GET /api/v1/sensors` - Sensor readings from all units
- `GET /api/v1/relays` - Relay states from all units
- `GET /api/v1/alarms` - Current alarm status

### Control

- `POST /api/v1/alarms/reset` - Reset active alarms
- `POST /api/v1/demo-mode` - Toggle demo mode (blocked if debug.code=1)
- `GET /api/v1/demo-mode` - Get demo mode status

### Logs

- `GET /api/v1/logs/events?date=YYYY-MM-DD` - Download event logs
- `GET /api/v1/logs/conditions?date=YYYY-MM-DD` - Download condition logs

### Proxy

- `POST /api/v1/defrost/trigger` - Trigger defrost cycle
- `POST /api/v1/setpoint` - Set temperature setpoint
- `POST /api/v1/config` - Update unit configuration

## Running

### From .deb Installation

```bash
sudo systemctl start web-api      # Start service
sudo systemctl stop web-api       # Stop service
sudo systemctl restart web-api    # Restart service
sudo systemctl status web-api     # Check status
```

Enable on boot:
```bash
sudo systemctl enable web-api
```

### Manual Execution

```bash
# Development mode (uses local config)
./build/web-api/aarch64/bin/api-web-interface

# Production mode (uses /etc/web-api/web_interface_config.env)
./build/web-api/aarch64/bin/api-web-interface /etc/web-api/web_interface_config.env
```

## Web Dashboard

Access the web interface at:
```
http://localhost:9000   **Unless port was changed
```

Features:
- View status of all configured units
- Monitor sensor readings and alarm states
- Control relays and set points
- Reset alarms
- Download logs
- Toggle demo mode (when enabled)

## Troubleshooting

### Service won't start
```bash
sudo journalctl -u web-api -n 50       # View last 50 log lines
sudo journalctl -u web-api -f           # Follow logs in real-time
```

### Config file not found
The service looks for config in this order:
1. Argument passed on command line
2. `./web_interface_config.env`
3. `/etc/web-api/web_interface_config.env`
4. `/etc/web-api/web_interface_config.env.sample`

### Email notifications not working
- Verify `email.server`, `email.address`, and `email.password` in config
- Check firewall allows outbound SMTP (port 587)
- View logs: `sudo journalctl -u web-api -f`

### Static files or templates not loading
Ensure they exist in one of these locations:
- `./static/` and `./templates/` (development)
- `/usr/share/web-api/static/` and `/usr/share/web-api/templates/` (production)

## Development

### Building for aarch64 Cross-Compilation
```bash
make ARCH=aarch64
```

### Building for x86_64 (default)
```bash
make
```

## Performance

- **Polling**: 30-second interval per unit
- **API Timeout**: 10 seconds per request
- **Memory**: ~50MB typical runtime
- **Connections**: Non-blocking, handles multiple concurrent requests

## License

Copyright © 2025 William Bellvance Jr
Licensed under the MIT License

See [LICENSE](../../LICENSE) for details.
