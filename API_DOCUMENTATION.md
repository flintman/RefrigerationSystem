# Refrigeration System REST API Documentation

## Base URL
```
http://<host>:<port>/api/v1
```
Default port: `8095` (configurable via `api.port` setting)

## Authentication
All endpoints require API key authentication via the `X-API-Key` header.

**Header:**
```
X-API-Key: <your-api-key>
```

Default API key: `refrigeration-api-default-key-change-me`
Configure via `api.key` setting in `/etc/refrigeration/config.env`

---

## Endpoints

### 1. Health Check

#### GET `/health` or `/api/v1/health`
Health check endpoint to verify API is running.

**Response (200 OK):**
```json
{
  "status": "ok",
  "timestamp": 1764953832
}
```

---

### 2. System Status

#### GET `/api/v1/status`
Get complete system status including relays, sensors, and setpoint.

**Response (200 OK):**
```json
{
  "timestamp": 1764953832,
  "system": "Refrigeration Control System",
  "version": "<version>",
  "relays": {
    "compressor": true,
    "fan": false,
    "valve": true,
    "electric_heater": false
  },
  "system_status": "Running",
  "sensors": {
    "return_temp": 38.5,
    "supply_temp": 42.1,
    "coil_temp": 35.2
  },
  "setpoint": 40.0
}
```

**Fields:**
- `relays`: Boolean status of each relay (True = ON, False = OFF)
  - `compressor`: Compressor state
  - `fan`: Fan state
  - `valve`: Expansion valve state
  - `electric_heater`: Electric heater state
- `system_status`: Overall system status
- `sensors`: Current temperature readings (in °F)
  - `return_temp`: Return line temperature
  - `supply_temp`: Supply line temperature
  - `coil_temp`: Evaporator coil temperature
- `setpoint`: Target temperature setpoint

---

### 3. Relay Status

#### GET `/api/v1/relays`
Get current status of all relays independently.

**Response (200 OK):**
```json
{
  "compressor": true,
  "fan": true,
  "valve": false,
  "electric_heater": false,
  "timestamp": 1764953832
}
```

**Fields:**
- `compressor`: Compressor relay (True = ON, False = OFF)
- `fan`: Fan relay (True = ON, False = OFF)
- `valve`: Expansion valve relay (True = ON, False = OFF)
- `electric_heater`: Electric heater relay (True = ON, False = OFF)

---

### 4. Sensor Status

#### GET `/api/v1/sensors`
Get current sensor readings.

**Response (200 OK):**
```json
{
  "return_temp": 38.5,
  "supply_temp": 42.1,
  "coil_temp": 35.2,
  "setpoint": 40.0,
  "timestamp": 1764953832
}
```

**Fields:**
- `return_temp`: Return line temperature (°F)
- `supply_temp`: Supply line temperature (°F)
- `coil_temp`: Evaporator coil temperature (°F)
- `setpoint`: Current target setpoint (°F)

---

### 5. Get Setpoint

#### GET `/api/v1/setpoint`
Retrieve the current temperature setpoint.

**Response (200 OK):**
```json
{
  "setpoint": 40.0,
  "timestamp": 1764953832
}
```

**Fields:**
- `setpoint`: Current setpoint temperature (°F)

---

### 6. Set Setpoint

#### POST `/api/v1/setpoint`
Update the temperature setpoint.

**Request Body:**
```json
{
  "setpoint": 38.5
}
```

**Response (200 OK) - Success:**
```json
{
  "success": true,
  "setpoint": 38.5,
  "timestamp": 1764953832
}
```

**Response (200 OK) - Out of Range:**
```json
{
  "error": true,
  "message": "Setpoint out of range",
  "low_limit": -20.0,
  "high_limit": 80.0,
  "timestamp": 1764953832
}
```

**Response (400 Bad Request) - Invalid JSON:**
```json
{
  "error": "Invalid JSON body",
  "timestamp": 1764953832
}
```

**Parameters:**
- `setpoint` (number, required): New setpoint temperature (°F)
  - Must be between `setpoint.low_limit` and `setpoint.high_limit` from config
  - Default range: -20°F to 80°F

---

### 7. Reset Alarms

#### POST `/api/v1/alarms/reset`
Reset all active alarms in the system.

**Request Body:** (none)

**Response (200 OK):**
```json
{
  "success": true,
  "message": "Alarms reset successfully",
  "timestamp": 1764953832
}
```

---

### 8. Trigger Defrost

#### POST `/api/v1/defrost/trigger`
Manually trigger a defrost cycle, if coil temperature is below defrost setpoint.

**Request Body:** (none)

**Response (200 OK):**
```json
{
  "success": true,
  "message": "Defrost triggered",
  "timestamp": 1764953832
}
```

---

### 9. System Information

#### GET `/api/v1/system-info`
Get comprehensive system configuration and status information.

**Response (200 OK):**
```json
{
  "api.key": "refrigeration-api-default-key-change-me",
  "api.port": "8095",
  "compressor.off_timer": "5",
  "debug.code": "1",
  "defrost.coil_temperature": "45",
  "defrost.interval_hours": "8",
  "defrost.timeout_mins": "45",
  "logging.interval_mins": "5",
  "logging.retention_period": "30",
  "sensor.coil": "0",
  "sensor.return": "0",
  "sensor.supply": "0",
  "setpoint.high_limit": "80",
  "setpoint.low_limit": "-20",
  "setpoint.offset": "2",
  "unit.compressor_run_seconds": "0",
  "unit.electric_heat": "1",
  "unit.fan_continuous": "0",
  "unit.number": "1234",
  "unit.relay_active_low": "1",
  "unit.setpoint": "55",
  "wifi.enable_hotspot": "1",
  "wifi.hotspot_password": "changeme",
  "active_alarms": [],
  "alarm_warning": false,
  "alarm_shutdown": false,
  "timestamp": 1764953832
}
```

**Fields:**
Configuration parameters:
- `api.key`: API authentication key
- `api.port`: API server port
- `compressor.off_timer`: Compressor off timer duration (seconds)
- `debug.code`: Debug mode flag
- `defrost.coil_temperature`: Target coil temperature for defrost (°F)
- `defrost.interval_hours`: Hours between defrost cycles
- `defrost.timeout_mins`: Maximum defrost cycle duration (minutes)
- `logging.interval_mins`: Log data interval (minutes)
- `logging.retention_period`: Days to retain logs
- `sensor.coil`: Coil sensor I2C address
- `sensor.return`: Return line sensor I2C address
- `sensor.supply`: Supply line sensor I2C address
- `setpoint.high_limit`: Maximum allowed setpoint (°F)
- `setpoint.low_limit`: Minimum allowed setpoint (°F)
- `setpoint.offset`: Temperature offset (°F)
- `unit.compressor_run_seconds`: Accumulated compressor runtime (seconds)
- `unit.electric_heat`: Electric heating enabled (1 = yes, 0 = no)
- `unit.fan_continuous`: Continuous fan mode (1 = yes, 0 = no)
- `unit.number`: Unit identification number
- `unit.relay_active_low`: Relay active state (1 = active low, 0 = active high)
- `unit.setpoint`: Current setpoint (°F)
- `wifi.enable_hotspot`: WiFi hotspot enabled (1 = yes, 0 = no)
- `wifi.hotspot_password`: WiFi hotspot password

Alarm status:
- `active_alarms`: Array of currently active alarm codes
- `alarm_warning`: Warning-level alarm active
- `alarm_shutdown`: Shutdown-level alarm active

---

### 10. Update Configuration

#### POST `/api/v1/config`
Update one or more configuration items.

**Request Body:**
```json
{
  "defrost.interval_hours": "10",
  "setpoint.high_limit": "85",
  "wifi.hotspot_password": "newpassword",
  "unit.electric_heat": "0"
}
```

**Response (200 OK) - Success:**
```json
{
  "success": true,
  "updated": {
    "defrost.interval_hours": "10",
    "setpoint.high_limit": "85",
    "wifi.hotspot_password": "newpassword",
    "unit.electric_heat": "0"
  },
  "timestamp": 1764971799
}
```

**Response (200 OK) - Partial Success:**
```json
{
  "success": true,
  "updated": {
    "defrost.interval_hours": "10"
  },
  "skipped": {
    "timestamp": "Read-only field",
    "unit.compressor_run_seconds": "Read-only field"
  },
  "errors": {
    "invalid.key": "Configuration key not found"
  },
  "timestamp": 1764971799
}
```

**Response (400 Bad Request):**
```json
{
  "error": true,
  "message": "Invalid JSON body",
  "timestamp": 1764971799
}
```

**Updateable Fields:**
All configuration items except:
- `timestamp` - Read-only (server timestamp)
- `active_alarms` - Read-only (current alarm status)
- `alarm_warning` - Read-only (alarm status)
- `alarm_shutdown` - Read-only (alarm status)
- `unit.compressor_run_seconds` - Read-only (accumulated runtime)
- `api.key` - Cannot be updated via API for security reasons
- `api.port` - Cannot be updated via API for security reasons

**Valid Fields for Update:**
- `compressor.off_timer` - Compressor off timer in seconds (integer)
- `debug.code` - Debug mode (0 or 1)
- `defrost.coil_temperature` - Target coil temperature for defrost (integer °F)
- `defrost.interval_hours` - Hours between defrost cycles (integer)
- `defrost.timeout_mins` - Maximum defrost duration (integer minutes)
- `logging.interval_mins` - Log data interval (integer minutes)
- `logging.retention_period` - Days to retain logs (integer)
- `sensor.coil` - Coil sensor I2C address (string)
- `sensor.return` - Return line sensor I2C address (string)
- `sensor.supply` - Supply line sensor I2C address (string)
- `setpoint.high_limit` - Maximum allowed setpoint (integer °F)
- `setpoint.low_limit` - Minimum allowed setpoint (integer °F)
- `setpoint.offset` - Temperature offset (integer °F)
- `unit.electric_heat` - Electric heating enabled (0 or 1)
- `unit.fan_continuous` - Continuous fan mode (0 or 1)
- `unit.number` - Unit identification number (integer)
- `unit.relay_active_low` - Relay active state (0 or 1)
- `unit.setpoint` - Current setpoint (integer °F)
- `wifi.enable_hotspot` - WiFi hotspot enabled (0 or 1)
- `wifi.hotspot_password` - WiFi hotspot password (string)

---

### 11. Download Events Log

#### GET `/api/v1/logs/events?date=YYYY-MM-DD`
Download the events log file for a specific date.

**Query Parameters:**
- `date` (required): Date in format YYYY-MM-DD (e.g., 2025-12-05)

**Response (200 OK):**
Returns the log file as a text/plain attachment with proper headers:
```
HTTP/1.1 200 OK
Content-Type: text/plain
Content-Disposition: attachment; filename="events-2025-12-05.log"
Content-Length: <size>
Connection: close

[2025-12-05 11:57:12] Debug] API: Status request received
[2025-12-05 11:57:13] Error] API: Exception reading system info
...
```

**Response (400 Bad Request):**
```json
{
  "error": "Invalid date format. Use YYYY-MM-DD",
  "timestamp": 1764953832
}
```

**Response (404 Not Found):**
```json
{
  "error": "Log file not found for date: 2025-12-05",
  "timestamp": 1764953832
}
```

---

### 11. Download Conditions Log

#### GET `/api/v1/logs/conditions?date=YYYY-MM-DD`
Download the conditions log file for a specific date.

**Query Parameters:**
- `date` (required): Date in format YYYY-MM-DD (e.g., 2025-12-05)

**Response (200 OK):**
Returns the log file as a text/plain attachment with proper headers:
```
HTTP/1.1 200 OK
Content-Type: text/plain
Content-Disposition: attachment; filename="conditions-2025-12-05.log"
Content-Length: <size>
Connection: close

2025-12-05 10:15:45 - Setpoint: 40.0, Return Sensor: 38.5, Coil Sensor: 35.2, Supply: 42.1, Status: Running, Compressor: True, Fan: False, Valve: True, Electric_heater: False
2025-12-05 10:20:45 - Setpoint: 40.0, Return Sensor: 38.6, Coil Sensor: 35.1, Supply: 42.2, Status: Running, Compressor: True, Fan: True, Valve: True, Electric_heater: False
...
```

**Response (400 Bad Request):**
```json
{
  "error": "Invalid date format. Use YYYY-MM-DD",
  "timestamp": 1764953832
}
```

**Response (404 Not Found):**
```json
{
  "error": "Log file not found for date: 2025-12-05",
  "timestamp": 1764953832
}
```

---

## Error Responses

### 401 Unauthorized
```json
{
  "error": "Invalid or missing API key",
  "timestamp": 1764953832
}
```

### 404 Not Found
```json
{
  "error": "Endpoint not found",
  "timestamp": 1764953832
}
```

### 400 Bad Request
```json
{
  "error": "Invalid JSON body",
  "timestamp": 1764953832
}
```

### 500 Internal Server Error
```json
{
  "error": "<error message>",
  "timestamp": 1764953832
}
```

---

## Example Usage

### Get System Status
```bash
curl -H "X-API-Key:refrigeration-api-default-key-change-me" \
  http://xxx.xxx.xxx.xxx:8095/api/v1/status
```

### Set Setpoint to 38°F
```bash
curl -X POST \
  -H "X-API-Key:refrigeration-api-default-key-change-me" \
  -H "Content-Type: application/json" \
  -d '{"setpoint": 38}' \
  http://xxx.xxx.xxx.xxx:8095/api/v1/setpoint
```

### Get Sensor Data
```bash
curl -H "X-API-Key:refrigeration-api-default-key-change-me" \
  http://xxx.xxx.xxx.xxx:8095/api/v1/sensors
```

### Get All Relay Status
```bash
curl -H "X-API-Key:refrigeration-api-default-key-change-me" \
  http://xxx.xxx.xxx.xxx:8095/api/v1/relays
```

### Reset Alarms
```bash
curl -X POST \
  -H "X-API-Key:refrigeration-api-default-key-change-me" \
  http://xxx.xxx.xxx.xxx:8095/api/v1/alarms/reset
```

### Trigger Defrost
```bash
curl -X POST \
  -H "X-API-Key:refrigeration-api-default-key-change-me" \
  http://xxx.xxx.xxx.xxx:8095/api/v1/defrost/trigger
```

### Get System Information
```bash
curl -H "X-API-Key:refrigeration-api-default-key-change-me" \
  http://xxx.xxx.xxx.xxx:8095/api/v1/system-info
```

### Update Configuration
```bash
curl -X POST \
  -H "X-API-Key:refrigeration-api-default-key-change-me" \
  -H "Content-Type: application/json" \
  -d '{"defrost.interval_hours": "10", "setpoint.high_limit": "85", "wifi.hotspot_password": "newpassword"}' \
  http://xxx.xxx.xxx.xxx:8095/api/v1/config
```

### Download Events Log
```bash
curl -H "X-API-Key:refrigeration-api-default-key-change-me" \
  "http://xxx.xxx.xxx.xxx:8095/api/v1/logs/events?date=2025-12-05" \
  -o events-2025-12-05.log
```

### Download Conditions Log
```bash
curl -H "X-API-Key:refrigeration-api-default-key-change-me" \
  "http://xxx.xxx.xxx.xxx:8095/api/v1/logs/conditions?date=2025-12-05" \
  -o conditions-2025-12-05.log
```

---

## Response Format

All successful responses include:
- JSON object with endpoint-specific fields
- `timestamp`: Unix timestamp of response

All error responses include:
- `error` field describing the error
- `timestamp`: Unix timestamp of response
