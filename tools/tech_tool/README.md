

# Refrigeration System Tools

This directory contains utility scripts and tools for managing and monitoring the Refrigeration System project.

## `tech-tool`

`tech-tool` is a full-screen terminal utility for configuration, diagnostics, and automation tasks related to the Refrigeration System. It features a modern TUI, live sensor polling, tabbed navigation, and direct integration with the refrigeration API and service controls.

### Key Features

- Tabbed interface: Config Editor and Dashboard
- Live sensor polling and display
- View, edit, and reset config values with validation and instant save
- API health check and service controls (start/stop/restart/kill)
- Demo mode toggle, defrost trigger, alarm reset
- Relay and sensor status, temperature history, and event logs
- Strict config validation and feedback
- Confirmation prompts and improved error handling

## Configuration Options

Below are the available configuration options for `tech_tool`, as validated by the tool. Only these keys are accepted:

| Config Key                      | Type     | Default Value                                 | Description                                                      |
|----------------------------------|----------|-----------------------------------------------|------------------------------------------------------------------|
| `api.key`                       | String   | refrigeration-api-default-key-change-me       | API key for authentication                                       |
| `api.port`                      | Integer  | 8095                                          | API port for local/remote server                                 |
| `compressor.off_timer`          | Integer  | 5                                             | Minimum off time for compressor (minutes)                        |
| `debug.code`                    | Boolean  | 1                                             | Enable (`1`) or disable (`0`) debug mode                         |
| `defrost.coil_temperature`      | Integer  | 45                                            | Coil temperature threshold for defrost (°F)                      |
| `defrost.interval_hours`        | Integer  | 8                                             | Interval in hours between defrost cycles                         |
| `defrost.timeout_mins`          | Integer  | 45                                            | Maximum duration in minutes for a defrost cycle                  |
| `logging.interval_mins`         | Integer  | 5                                             | Interval in minutes between log entries                          |
| `logging.retention_period`      | Integer  | 30                                            | Number of days to retain logs                                    |
| `sensor.coil`                   | Integer  | 0                                             | Coil sensor value                                                |
| `sensor.return`                 | Integer  | 0                                             | Return air sensor value                                          |
| `sensor.supply`                 | Integer  | 0                                             | Supply air sensor value                                          |
| `setpoint.high_limit`           | Integer  | 80                                            | Maximum allowable setpoint                                       |
| `setpoint.low_limit`            | Integer  | -20                                           | Minimum allowable setpoint                                       |
| `setpoint.offset`               | Integer  | 2                                             | Offset to apply to the setpoint                                  |
| `unit.compressor_run_seconds`   | Integer  | 0                                             | Compressor runtime in seconds                                    |
| `unit.electric_heat`            | Boolean  | 1                                             | Enable (`1`) or disable (`0`) electric heat                      |
| `unit.fan_continuous`           | Boolean  | 0                                             | Enable (`1`) or disable (`0`) continuous fan operation           |
| `unit.number`                   | Integer  | 1234                                          | Unique identifier for the refrigeration unit                     |
| `unit.relay_active_low`         | Boolean  | 1                                             | Low (`1`) or High (`0`) relay trigger                            |
| `unit.setpoint`                 | Integer  | 55                                            | Temperature setpoint for the unit                                |
| `wifi.enable_hotspot`           | Boolean  | 1                                             | Enable (`1`) or disable (`0`) Wi-Fi hotspot                      |
| `wifi.hotspot_password`         | String   | changeme                                      | Password for the Wi-Fi hotspot                                   |

## Usage

```sh
sudo tech-tool
```

*For questions or contributions, please open an issue or pull request.*