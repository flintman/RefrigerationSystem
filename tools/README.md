# Refrigeration System Tools

This directory contains utility scripts and tools for managing and monitoring the Refrigeration System project.

## `tech-tool`

`tech-tool` is a command-line utility designed to assist with configuration, diagnostics, and automation tasks for the Refrigeration System. It streamlines setup, testing, and maintenance operations.

### Features

- System configuration management
- Diagnostics and health checks
- Automation of routine tasks
- Logging and reporting

## Configuration Options

Below are the available configuration options for `tech_tool`:
| Config Key         | Description                                                                             |
|--------------------|-----------------------------------------------------------------------------------------|
| `logging.interval_mins`      | Interval in minutes between log entries. Default: `5`.                        |
| `logging.retention_period`   | Number of days to retain logs. Default: `30`.                                 |
| `unit.number`                | Unique identifier for the refrigeration unit. Default: `1234`.                |
| `unit.compressor_run_seconds`| Compressor runtime in seconds. Default: `0`.                                  |
| `unit.electric_heat`         | Enable (`1`) or disable (`0`) electric heat.                                  |
| `unit.fan_continuous`        | Enable (`1`) or disable (`0`) continuous fan operation.                       |
| `unit.relay_active_low`       | Low (`1`) or High (`0`) relay trigger.                     |
| `unit.setpoint_rotary`       | Enable (`1`) or disable (`0`) rotary setpoint adjustment.                     |
| `unit.setpoint`              | Temperature setpoint for the unit. Default: `55`.                             |
| `debug.code`                 | Enable (`1`) or disable (`0`) debug mode.                                     |
| `defrost.interval_hours`     | Interval in hours between defrost cycles. Default: `8`.                       |
| `defrost.timeout_mins`       | Maximum duration in minutes for a defrost cycle. Default: `45`.               |
| `defrost.coil_temperature`   | Coil temperature threshold for defrost (°F). Default: `45`.                   |
| `setpoint.offset`            | Offset to apply to the setpoint. Default: `2`.                                |
| `setpoint.high_limit`        | Maximum allowable setpoint. Default: `80`.                                    |
| `setpoint.low_limit`         | Minimum allowable setpoint. Default: `-20`.                                   |
| `compressor.off_timer`       | Minimum off time for compressor (minutes). Default: `5`.                      |
| `wifi.enable_hotspot`        | Enable (`1`) or disable (`0`) Wi-Fi hotspot.                                  |
| `wifi.hotspot_password`      | Password for the Wi-Fi hotspot. Default: `changeme`.                          |
| `sensor.return`              | Return air sensor value. Default: `0`.                                        |
| `sensor.supply`              | Supply air sensor value. Default: `0`.                                        |
| `sensor.coil`                | Coil sensor value. Default: `0`.                                              |
| `client.ip_address`          | IP address of the server. Default: `192.168.1.1`.                             |
| `client.cert`                | Path to client certificate file. Default: `/etc/refrigeration/cert.pem`.      |
| `client.enable_send_data`    | Enable (`1`) or disable (`0`) sending data to server.                         |
| `client.key`                 | Path to client key file. Default: `/etc/refrigeration/key.pem`.               |
| `client.ca`                  | Path to CA certificate file. Default: `/etc/refrigeration/ca.pem`.            |
| `client.sent_mins`           | Interval in minutes for sending data. Default: `15`.                          |


## Usage

```sh
sudo tech-tool
```


*For questions or contributions, please open an issue or pull request.*