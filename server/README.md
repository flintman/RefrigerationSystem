# C++ Secure Server

This directory contains the C++ implementation of the refrigeration system server, which replaces the Python version with identical functionality but no external dependencies (except OpenSSL for SSL/TLS).

## Features

- **SSL/TLS Socket Server** (port 5001) - Secure communication with refrigeration units
- **HTTP Web Server** (port 5008) - Web interface for monitoring and control
- **Data Persistence** - JSON file storage in `received_data/` directory
- **Email Notifications** - Sends SMTP for alarm notifications (logs messages) --If setup
- **Command Queuing** - Send commands (defrost, alarm_reset) to units
- **CSV Export** - Download unit data as CSV files
- **IP Blocking** - Security against failed authentication attempts
- **No External Dependencies** - Self-contained except for OpenSSL

## Building

From the repository root:
```bash
make server
```

Or from this directory:
```bash
make
```

## Running

From the repository root:
```bash
./server/build/bin/secure_server
```

## Configuration

The server uses environment variables for configuration:

- `CERT_FILE` - SSL certificate file path
- `KEY_FILE` - SSL private key file path  
- `CA_CERT_FILE` - CA certificate file path
- `EMAIL_SERVER` - SMTP server hostname
- `EMAIL_ADDRESS` - Email address for notifications
- `EMAIL_PASSWORD` - Email password

If SSL certificates are not configured, the server runs in non-SSL mode for testing.

## Web Interface

Access the web interface at: http://localhost:5008

### API Endpoints

- `GET /` - Main web interface
- `GET /api/units` - Get all units and their data
- `GET /unit/<unit>` - Get data for specific unit
- `GET /download/<unit>` - Download unit data as CSV
- `POST /command/<unit>` - Send command to unit
- `GET /static/<file>` - Static files (CSS, JS, images)

### Commands

Send commands via POST to `/command/<unit>` with JSON body:
```json
{"command": "defrost"}
{"command": "alarm_reset"}
```

## Data Format

Unit data is stored in JSON files in the `received_data/` directory with format:
`<unit>_<YYYY-MM-DD>.json`

Example data structure:
```json
{
  "unit": "Unit001",
  "timestamp": "14:30:15  01:15:2024",
  "setpoint": 35.0,
  "return_temp": 36.2,
  "supply_temp": 32.8,
  "coil_temp": 28.5,
  "fan": true,
  "compressor": true,
  "electric_heater": false,
  "valve": true,
  "status": "Cooling",
  "alarm_codes": [101, 205]
}
```

## Socket Communication

Units can connect to port 5001 and send JSON data. The server will:

1. Validate client certificates (if SSL enabled)
2. Process received data
3. Store data to files
4. Send email notifications for alarms
5. Return queued commands to units
6. Block IPs after failed authentication attempts

## Build System

- `make` - Build the server
- `make clean` - Clean build files
- `make run` - Build and run the server