# ESP32 Secure OTA Firmware Assessment

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.4-blue)](https://docs.espressif.com/projects/esp-idf/)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)

Production-grade Over-The-Air (OTA) firmware update system for ESP32 with fail-safe mechanisms, automatic rollback, and recovery mode.

## Features

- ✅ **Dual-Partition OTA**: factory + ota_0 + ota_1 layout
- ✅ **10-Second Validation**: Automatic firmware stability check
- ✅ **Automatic Rollback**: Boot to previous partition on crash
- ✅ **Recovery Mode**: GPIO-triggered WiFi AP portal
- ✅ **WiFi Configuration**: Web-based credential management
- ✅ **LED Indicators**: Visual feedback for system state
- ✅ **SHA256 Verification**: Firmware integrity validation
- ✅ **Manual OTA Trigger**: HTTP-based firmware updates

## Hardware Requirements

| Component | Specification |
|-----------|---------------|
| MCU | ESP32 (ESP32, ESP32-S3, ESP32-C3) |
| Flash | Minimum 4MB |
| LED | Built-in or external (GPIO2) |
| Button | Recovery trigger (GPIO4) |

## Pin Configuration

| Function | GPIO | Description |
|----------|------|-------------|
| LED | 2 | Status indicator (built-in on most DevKits) |
| Recovery Button | 4 | Hold LOW during reset to enter recovery mode |

### LED Indicators

| Pattern | Interval | Meaning |
|---------|----------|---------|
| Slow blink | 1s ON / 1s OFF | Normal operation |
| Fast blink | 200ms ON / 200ms OFF | OTA update in progress |
| Double blink | 2x 100ms blink, 800ms pause | Recovery mode active |

## Quick Start

### Prerequisites
```bash
# Install ESP-IDF v5.4+
git clone -b v5.4 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh
source export.sh
```

### Build & Flash
```bash
# Clone repository
git clone <your-repo-url>
cd firmware-assessment-esp32

# Configure (optional)
idf.py menuconfig

# Build
idf.py build

# Flash & Monitor
idf.py -p /dev/ttyUSB0 flash monitor
```

### First Boot

After flashing, the device will:
1. Boot to `factory` partition
2. Initialize WiFi (default credentials in code)
3. Start OTA HTTP server on port 80
4. LED blinks slowly (normal mode)

## Usage Guide

### Normal Mode Operation

1. Device boots and connects to WiFi
2. Find device IP in serial monitor:
```
   I (xxx) WIFI_MGR: Got IP: 192.168.8.100
```
3. Access OTA portal: `http://192.168.8.100`
4. Enter firmware URL and click "Start Update"

### Recovery Mode

**Activation:**
1. Connect jumper wire: GPIO4 → GND
2. Press RESET button
3. Wait for double-blink LED pattern
4. Remove jumper wire

**Usage:**
1. Connect to WiFi AP: `ESP32-Recovery` / `recovery123`
2. Open browser: `http://192.168.4.1`
3. Configure WiFi credentials or trigger OTA update
4. Reboot device

### OTA Update Procedure

#### Step 1: Prepare Firmware
```bash
# Build new version
idf.py build

# (Optional) Add metadata for tracking
python tools/prepare-firmware.py   build/secure-ota-esp32.bin release/firmware_v2.0.0.bin 2.0.0
```

#### Step 2: Host Firmware
```bash
cd build/
python -m http.server 8000
```

#### Step 3: Trigger Update

Via web portal, enter URL:
```
http://<YOUR_PC_IP>:8000/secure-ota-esp32.bin
```

#### Step 4: Monitor Update

Serial output:
```
I (xxx) OTA_MGR: Starting OTA update
I (xxx) OTA_MGR: Progress: 10% ... 100%
I (xxx) OTA_MGR: OTA successful! Rebooting...
```

After reboot:
```
I (xxx) MAIN: New firmware detected, validating...
[10 second wait - LED fast blink]
I (xxx) MAIN: Firmware validated successfully!
I (xxx) MAIN: Running from partition: ota_0
```

## Architecture

### Partition Layout
```
┌─────────────────────┐ 0x0000
│   Bootloader        │ 32KB
├─────────────────────┤ 0x8000
│   Partition Table   │ 4KB
├─────────────────────┤ 0x9000
│   NVS               │ 24KB
├─────────────────────┤ 0xF000
│   PHY Init          │ 4KB
├─────────────────────┤ 0x10000
│   Factory (App)     │ 1MB
├─────────────────────┤ 0x110000
│   OTA_0             │ 1MB
├─────────────────────┤ 0x210000
│   OTA_1             │ 1MB
└─────────────────────┘
```

### State Machine
```
┌─────────┐
│  BOOT   │
└────┬────┘
     │
     v
┌─────────────────┐      ┌──────────────┐
│ GPIO4 == LOW?   ├─YES─→│ RECOVERY     │
└────┬────────────┘      │ MODE         │
     NO                   └──────────────┘
     │
     v
┌─────────────────┐
│ Check Partition │
│ State           │
└────┬────────────┘
     │
     ├─ PENDING_VERIFY
     │  ↓
     │  ┌──────────────┐
     │  │ 10s Wait     │
     │  └──┬───────────┘
     │     │
     │     ├─ Success → Mark Valid
     │     └─ Crash → Rollback
     │
     └─ VALID
        ↓
    ┌──────────────┐
    │ NORMAL MODE  │
    └──────────────┘
```

For detailed architecture, see [ARCHITECTURE.md](docs/ARCHITECTURE.md)


## Troubleshooting

### Issue: Device stuck in download mode
**Solution:** Ensure GPIO0 (BOOT button) is not pressed during normal boot

### Issue: WiFi connection fails
**Solution:** Use recovery mode to reconfigure credentials

### Issue: OTA fails with "invalid magic byte"
**Solution:** Ensure using raw binary (`secure-ota-esp32.bin`), not prepared version

### Issue: Recovery mode not triggered
**Solution:** Verify GPIO4 is LOW before pressing RESET, hold until LED double-blinks

## Project Structure
```
firmware-assessment-esp32/
├── main/
│   ├── main.c              # Main application logic
│   ├── led_indicator.c/h   # LED control
│   ├── wifi_manager.c/h    # WiFi & NVS
│   ├── ota_manager.c/h     # OTA implementation
│   ├── recovery_mode.c/h   # Recovery portal
│   └── CMakeLists.txt
├── tools/
│   └── prepare-firmware.py # Firmware metadata tool
├── docs/
│   ├── ARCHITECTURE.md     # Design decisions
│   └── prompt.md           # AI assistance log
├── partitions.csv          # Partition table
├── CMakeLists.txt
└── README.md
```

## Development

### Code Style

- Follow ESP-IDF coding conventions
- Use ESP_LOG macros for logging
- Add error handling for all operations
- Document non-obvious code

## License

MIT License - See LICENSE file for details

## Author

**Your Name** - Firmware Assessment Submission

## Acknowledgments

- ESP-IDF by Espressif Systems
- Assessment design by MJ Mokhtar