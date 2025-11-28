# Refrigeration C++

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.

## Third-Party Licenses

This project includes third-party software:

- **OpenSSL** (Apache License 2.0) — see [`vendor/openssl/LICENSE.txt`](vendor/openssl/LICENSE.txt)
- **ws2811** (MIT License) — see [`vendor/ws2811/LICENSE`](vendor/ws2811/LICENSE)
- **nlohmann/json** (MIT License) — see [`include/nlohmann/json.hpp`](include/nlohmann/json.hpp)

All third-party licenses are included in the source distribution.
See [THIRD_PARTY_LICENSES](THIRD_PARTY_LICENSES.md) for details.
When redistributing or packaging this software, be sure to include all relevant license files.

---

This is my software to control a refrigeration system with a Raspberry Pi.

---

## Compilation

### Base System

```sh
sudo apt-get install libssl-dev build-essential gcc make g++-arm-linux-gnueabihf
make            # Build .deb
make clean      # Clean build folder
```

### Server

```sh
make server         # Build server .deb
make server-clean   # Clean the directory
```

## Server Dependencies

```sh
sudo apt-get install libcurl4-openssl-dev
```

---

## Installation

1. `sudo raspi-config`
    - Enable 1-Wire, I2C, and SPI, then reboot.
2. Install the `.deb` file.
3. Run `sudo tech-tool` to set up the device.

---

## Startup Behavior

On startup, the system will broadcast a hotspot for 2 minutes (or as long as you remain connected).

### Force Start Hotspot

Set the setpoint to **65°F** and press and hold the alarm button for 10+ seconds.
The IP address will display on the inside screen. The hotspot remains active for 2 minutes unless you stay connected.

---

## Alarm Codes

<details>
<summary><strong>Shutdown Alarms</strong></summary>

- **1001** - Unit not cooling (≥5°F across coil in 30 min & Return Temp > 30°F)
- **1002** - Unit not heating (≥5°F across coil in 30 min & Return Temp < 60°F)
- **1004** - Unit defrost timed out (defrost > 45 min)
- **2000** - Return sensor failure (out of range)
- **2001** - Coil sensor failure (out of range)
- **9001** - Pretrip cooling issue
- **9002** - Pretrip heating issue
- **9003** - Pretrip cooling issue #2

</details>

<details>
<summary><strong>Warning Alarms</strong></summary>

- **2002** - Supply sensor failure (out of range)
- **9000** - Pretrip passed

</details>

---

## Setpoint Options

- **Buttons:**
  - Press/hold either button for 2s: display flashes setpoint.
  - Use up/down to adjust. Hold >5s to skip by 5.
  - Press both buttons to save and exit.
  - Inactivity exits without saving.

---

## Demo Mode

Set setpoint to **80°F** and hold the defrost button for 10s.
Toggles Demo Mode (simulates cooling, heating, defrost, etc.).

---

## Pretrip Mode

Set setpoint to **65°F** and hold the defrost button for 10s.
Starts Pretrip Mode (simulates cooling, heating, and returns to cooling before pass/fail).

---

## Generating Keys

### Common Key

```sh
openssl genrsa -out ca.key 4096

openssl req -x509 -new -key ca.key -sha256 -days 3650 -out ca.pem \
  -subj "/C=US/O=Refrigeration/OU=Root CA/CN=Refrigeration Root CA" \
  -addext "basicConstraints=critical,CA:true" \
  -addext "keyUsage=critical, keyCertSign, cRLSign"
```

### Server Key

```sh
openssl genrsa -out server.key 2048

openssl req -new -key server.key -out server.csr \
  -subj "/C=US/O=Refrigeration/OU=Server/CN=Refrigeration Server"

openssl x509 -req -in server.csr -CA ca.pem -CAkey ca.key -CAcreateserial \
  -out server.crt -days 825 -sha256
```

### Client Key

```sh
openssl genrsa -out client.key 2048

openssl req -new -key client.key -out client.csr \
  -subj "/C=US/O=Refrigeration/OU=Client/CN=Refrigeration Client"

openssl x509 -req -in client.csr -CA ca.pem -CAkey ca.key -CAcreateserial \
  -out client.crt -days 825 -sha256
```

This will generate:
`ca.key`, `ca.pem`, `ca.srl`, `client.crt`, `client.csr`, `client.key`, `server.crt`, `server.csr`, `server.key`

#### Server Files Needed

```sh
CERT_FILE=/path/to/server.crt
KEY_FILE=/path/to/server.key
CA_CERT_FILE=/path/to/ca.pem
```

#### Client Files Needed

```sh
client.ca=/path/to/ca.pem
client.cert=/path/to/client.crt
client.key=/path/to/client.key
```

---

## GPIO Pins Used

| Function                | GPIO Pin |
|-------------------------|----------|
| Compressor Output       | 17       |
| Fan Output              | 27       |
| 3-way Valve Output      | 22       |
| Electric Heat Output    | 23       |
| Alarm Button            | 5        |
| Defrost Button          | 25       |
| LED Lights              | 18       |
| SDA                     | 2        |
| SCL                     | 3        |
| One-Wire                | 4        |

---

## Required Equipment *(WIP until fully built and tested)*

- (1) [Raspberry Pi](https://www.amazon.com/Raspberry-Quad-core-Bluetooth-onboard-Antenna/dp/B0CCRP85TR/)
- (1) [Headers](https://www.amazon.com/Frienda-Break-Away-Connector-Compatible-Raspberry/dp/B083DYVWDN)
- (1) [DS18B20 sensors](https://www.amazon.com/AILEWEI-DS18B20-Temperature-Stainless-Waterproof/dp/B0DK3HP3TV/)
- (1) [Enclosure](https://www.amazon.com/Gratury-Stainless-Waterproof-Electrical-290%C3%97190%C3%97140mm/dp/B08282SQPT/)
- (1) [17 pin connector](https://www.amazon.com/HangTon-Aviation-Circular-Connector-Automotive/dp/B0BN3ZD6DB/)
- (1) [20 conductor cable](https://www.amazon.com/KWANGIL-20AWG-Conductor-Cable-UL2464/dp/B0CSCYYS2T/)
- (1) Weather connector
- (1) [ADS1115](https://www.amazon.com/Teyleten-Robot-Converter-Amplifier-Raspberry/dp/B0CNV9G4K1/)
- (1) [Weather Box Back](https://www.amazon.com/dp/B0CXN42RP3/)

---