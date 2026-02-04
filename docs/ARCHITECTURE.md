# Architecture Documentation

## System Overview

This document details the technical design decisions, fail-safe mechanisms, and implementation strategies for the ESP32 OTA firmware update system.

## Design Principles

1. **Fail-Safe First**: Every operation considers power-loss scenarios
2. **Atomic State Changes**: Use hardware mechanisms for reliability
3. **Progressive Validation**: Multi-stage verification before trust
4. **Clear State Indicators**: Visual and logging feedback at every stage

---

## Visual Architecture

### OTA State Machine
```mermaid
stateDiagram-v2
    [*] --> ESP_OTA_IMG_NEW: esp_ota_set_boot_partition()
    ESP_OTA_IMG_NEW --> ESP_OTA_IMG_PENDING_VERIFY: Device Reboot
    
    ESP_OTA_IMG_PENDING_VERIFY --> Validating: First Boot
    
    Validating --> WaitingStability: Start 10s Timer
    
    WaitingStability --> ESP_OTA_IMG_VALID: 10s passed, no crash
    WaitingStability --> BootloaderRollback: Crash/WDT Timeout
    
    ESP_OTA_IMG_VALID --> [*]: Normal Operation
    
    BootloaderRollback --> PreviousPartition: Boot from last valid
    PreviousPartition --> [*]: Safe Fallback
    
    note right of ESP_OTA_IMG_PENDING_VERIFY
        Critical State:
        Firmware not yet trusted
    end note
    
    note right of WaitingStability
        10 second validation period
        LED: Fast Blink
    end note
    
    note right of BootloaderRollback
        Automatic recovery
        No user intervention needed
    end note
```

---

### Boot Flow & Recovery Detection
```mermaid
flowchart TD
    Start([Power On / Reset]) --> CheckGPIO{GPIO4 == LOW?}
    
    CheckGPIO -->|Yes| RecoveryMode[Recovery Mode]
    CheckGPIO -->|No| CheckOTAState[Check OTA Partition State]
    
    RecoveryMode --> StartAP[Start WiFi AP<br/>SSID: ESP32-Recovery]
    StartAP --> HTTPServer[Launch HTTP Server<br/>Port: 192.168.4.1]
    HTTPServer --> WaitConfig[Wait for User<br/>WiFi Config or OTA Trigger]
    WaitConfig --> Reboot{User Action}
    Reboot -->|Config Saved| Start
    Reboot -->|OTA Triggered| OTAProcess
    
    CheckOTAState --> StateCheck{Partition State?}
    
    StateCheck -->|PENDING_VERIFY| Validation[10s Validation Period<br/>LED: Fast Blink]
    StateCheck -->|VALID/UNDEFINED| NormalBoot[Normal Boot<br/>LED: Slow Blink]
    
    Validation --> ValidationWait{Crash?}
    ValidationWait -->|No Crash| MarkValid[esp_ota_mark_app_valid_cancel_rollback]
    ValidationWait -->|Crash/WDT| AutoRollback[Bootloader Auto Rollback]
    
    MarkValid --> NormalBoot
    AutoRollback --> Start
    
    NormalBoot --> InitWiFi[Initialize WiFi]
    InitWiFi --> StartOTAServer[Start OTA HTTP Server<br/>Port: 80]
    StartOTAServer --> Running([Running])
    
    Running --> OTATriggered{OTA Triggered?}
    OTATriggered -->|Yes| OTAProcess[OTA Download & Flash]
    OTATriggered -->|No| Running
    
    OTAProcess --> SetBootPartition[esp_ota_set_boot_partition]
    SetBootPartition --> Restart[esp_restart]
    Restart --> Start
    
    style RecoveryMode fill:#ff9999
    style NormalBoot fill:#99ff99
    style Validation fill:#ffff99
    style AutoRollback fill:#ff6666
```

---

### Component Architecture
```mermaid
graph TB
    subgraph Main["main.c"]
        AppMain[app_main]
    end
    
    subgraph Components["Components"]
        LED[led_indicator.c]
        WiFi[wifi_manager.c]
        OTA[ota_manager.c]
        Recovery[recovery_mode.c]
    end
    
    subgraph Storage["Storage"]
        NVS[(NVS<br/>WiFi Creds)]
        Flash[(Flash<br/>Partitions)]
    end
    
    subgraph ESPIDF["ESP-IDF"]
        FreeRTOS[FreeRTOS]
        WiFiDriver[WiFi Driver]
        HTTPServer[HTTP Server]
        OTALib[OTA Library]
    end
    
    AppMain --> LED
    AppMain --> WiFi
    AppMain --> OTA
    AppMain --> Recovery
    
    LED --> FreeRTOS
    
    WiFi --> WiFiDriver
    WiFi --> NVS
    
    OTA --> HTTPServer
    OTA --> OTALib
    OTA --> Flash
    
    Recovery --> WiFiDriver
    Recovery --> HTTPServer
    Recovery --> WiFi
    Recovery --> OTA
    
    style Main fill:#e3f2fd
    style Components fill:#f3e5f5
    style Storage fill:#fff3e0
    style ESPIDF fill:#e8f5e9
```

---

### Partition Layout
```mermaid
graph TB
    subgraph Flash["Flash Memory (4MB)"]
        A[0x0000 - Bootloader<br/>32KB]
        B[0x8000 - Partition Table<br/>4KB]
        C[0x9000 - NVS<br/>24KB]
        D[0xF000 - PHY Init<br/>4KB]
        E[0x10000 - Factory<br/>1MB]
        F[0x110000 - OTA_0<br/>1MB]
        G[0x210000 - OTA_1<br/>1MB]
    end
    
    A --> B
    B --> C
    C --> D
    D --> E
    E --> F
    F --> G
    
    style A fill:#e1f5ff
    style E fill:#c8e6c9
    style F fill:#fff9c4
    style G fill:#ffccbc
    
    E -.First Boot.-> E
    E -.OTA Update 1.-> F
    F -.OTA Update 2.-> G
    G -.OTA Update 3.-> F
```

---

## Partition Strategy

### Layout Rationale
```
Factory (1MB):  Initial firmware, fallback for corrupted OTA
OTA_0 (1MB):    Primary update target
OTA_1 (1MB):    Secondary update target (alternating)
NVS (24KB):     WiFi credentials, system state
```

**Why this layout:**
- **Factory partition**: Immutable safety net - always has working firmware
- **Dual OTA**: Alternating updates allow rollback without re-download
- **Oversized partitions**: 1MB each provides headroom for future growth

### Partition Selection Algorithm
```c
const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
```

ESP-IDF automatically selects:
- If running from `factory` → write to `ota_0`
- If running from `ota_0` → write to `ota_1`
- If running from `ota_1` → write to `ota_0`

---

## OTA Implementation Details

### State Transition Implementation
```c
// In main.c
if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
    ESP_LOGI(TAG, "Validating new firmware...");
    led_set_mode(LED_MODE_OTA);
    
    vTaskDelay(pdMS_TO_TICKS(10000));  // 10 second stability test
    
    esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI(TAG, "Firmware validated!");
}
```

**Why 10 seconds:**
- Enough time for critical initialization (WiFi, sensors, etc.)
- Short enough to minimize user wait
- Industry standard for embedded validation windows

### OTA Download Sequence
```mermaid
sequenceDiagram
    participant User
    participant WebUI
    participant OTAManager
    participant HTTPClient
    participant Server
    participant Flash
    participant Bootloader
    
    User->>WebUI: Enter firmware URL
    WebUI->>OTAManager: POST /update
    OTAManager->>OTAManager: Validate URL
    
    OTAManager->>HTTPClient: esp_http_client_init()
    OTAManager->>HTTPClient: esp_http_client_open()
    HTTPClient->>Server: HTTP GET /firmware.bin
    Server-->>HTTPClient: 200 OK + Content-Length
    
    OTAManager->>Flash: esp_ota_begin(ota_partition)
    Flash-->>OTAManager: update_handle
    
    loop Download Loop
        HTTPClient->>Server: Read 1024 bytes
        Server-->>HTTPClient: Firmware chunk
        OTAManager->>OTAManager: Check magic byte (0xE9)
        OTAManager->>Flash: esp_ota_write(chunk)
        OTAManager->>User: Log: Progress X%
    end
    
    OTAManager->>Flash: esp_ota_end()
    Flash-->>OTAManager: Validation OK
    
    OTAManager->>Bootloader: esp_ota_set_boot_partition()
    Bootloader-->>OTAManager: Boot partition set
    
    OTAManager->>User: OTA Success! Rebooting...
    OTAManager->>OTAManager: esp_restart()
    
    Note over Bootloader: On next boot:<br/>State = PENDING_VERIFY
```

---

### Atomic Operations

**OTA Data Partition (0x9000):**
```c
typedef struct {
    uint32_t ota_seq;      // Monotonic counter
    uint8_t  seq_label[20]; // Partition label
    uint32_t crc;          // Self-integrity check
} esp_ota_select_entry_t;
```

ESP-IDF bootloader uses **double-buffered** OTA data:
1. Writes new entry with incremented `ota_seq`
2. CRC protects against partial writes
3. Bootloader picks highest valid `ota_seq`

**Result:** Power loss during partition switch is safe - bootloader falls back to previous valid entry.

### Flash Write Reliability
```c
err = esp_ota_write(update_handle, buffer, data_read);
```

Internally uses page-level writes:
- Flash controller buffers partial pages
- Only commits full 4KB pages
- Incomplete writes show as 0xFF (erased state)
- ESP-IDF validates with CRC before boot

---

## NVS Usage

### Storage Layout
```
Namespace: "wifi_config"
├─ ssid (string, max 32 bytes)
└─ password (string, max 64 bytes)

Namespace: "ota_data" (ESP-IDF managed)
└─ OTA state machine data
```

### Write Strategy
```c
nvs_handle_t handle;
nvs_open("wifi_config", NVS_READWRITE, &handle);
nvs_set_str(handle, "ssid", ssid);
nvs_set_str(handle, "password", password);
nvs_commit(handle);  // ← Atomic commit
nvs_close(handle);
```

**`nvs_commit()` guarantees:**
- All writes in transaction are atomic
- CRC-protected
- Wear-leveling across flash sectors
- Power-loss safe (commits entire page or nothing)

---


### Boot Detection
```c
gpio_set_pull_mode(BOOT_GPIO, GPIO_PULLUP_ONLY);
vTaskDelay(pdMS_TO_TICKS(100));  // Debounce

if (gpio_get_level(BOOT_GPIO) == 0) {
    // GPIO LOW → Recovery mode
}
```

**Timing:**
- GPIO read happens ~100ms after reset
- User must hold button through reset
- Internal pull-up ensures defined state

### WiFi AP Configuration
```c
wifi_config_t ap_config = {
    .ap = {
        .ssid = "ESP32-Recovery",
        .password = "recovery123",
        .max_connection = 4,
        .authmode = WIFI_AUTH_WPA2_PSK
    }
};
```

**Security considerations:**
- WPA2-PSK for production (changeable in code)
- Limited connections to prevent DoS
- No internet access (isolated AP)

### HTTP Server

Lightweight server using ESP-IDF httpd:
```c
httpd_config_t config = HTTPD_DEFAULT_CONFIG();
config.max_uri_handlers = 8;
config.stack_size = 4096;
```

**Memory footprint:** ~30KB RAM

---

## OTA Download Implementation

### Why Not `esp_https_ota()`?

Assessment requires custom implementation to demonstrate understanding. Using low-level API:
```c
esp_http_client_handle_t client = esp_http_client_init(&config);
esp_http_client_open(client, 0);

esp_ota_begin(partition, OTA_SIZE_UNKNOWN, &handle);

while (data_read > 0) {
    data_read = esp_http_client_read(client, buffer, 1024);
    esp_ota_write(handle, buffer, data_read);
}

esp_ota_end(handle);
esp_ota_set_boot_partition(partition);
```

**Advantages:**
- Custom header parsing (for `prepare-firmware.py` output)
- Progress callbacks
- Retry logic
- Timeout handling

### Header Detection
```c
uint32_t magic = *((uint32_t *)buffer);
if (magic == 0xDEADBEEF) {
    // Custom header - skip 44 bytes
    firmware_offset = 44;
} else if (buffer[0] == 0xE9) {
    // Raw ESP32 binary - write as-is
    firmware_offset = 0;
}
```

Supports both:
- Prepared firmware (with metadata)
- Raw binaries (direct from build)

---

## LED Indicator Design

### LED State Machine
```mermaid
stateDiagram-v2
    [*] --> Normal: Normal Boot
    [*] --> Recovery: GPIO4 LOW at boot
    Normal --> OTA: OTA Triggered
    OTA --> Validation: Reboot after OTA
    Validation --> Normal: 10s passed
    Recovery --> Normal: Reboot after config
    
    state Normal {
        [*] --> SlowBlink
        SlowBlink --> On: 1000ms
        On --> Off: 1000ms
        Off --> SlowBlink
    }
    
    state OTA {
        [*] --> FastBlink
        FastBlink --> On2: 200ms
        On2 --> Off2: 200ms
        Off2 --> FastBlink
    }
    
    state Recovery {
        [*] --> DoubleBlink
        DoubleBlink --> Blink1On: 100ms
        Blink1On --> Blink1Off: 100ms
        Blink1Off --> Blink2On: 100ms
        Blink2On --> Blink2Off: 100ms
        Blink2Off --> Pause: 800ms
        Pause --> DoubleBlink
    }
    
    note right of Normal
        Slow Blink
        Normal Operation
    end note
    
    note right of OTA
        Fast Blink
        Firmware Update
    end note
    
    note right of Recovery
        Double Blink
        Recovery Mode
    end note
```

### Implementation
```c
typedef enum {
    LED_MODE_NORMAL,    // 1000ms on, 1000ms off
    LED_MODE_OTA,       // 200ms on, 200ms off
    LED_MODE_RECOVERY   // 100ms-100ms-100ms-100ms-800ms
} led_mode_t;

static void led_task(void *pvParameters) {
    while (1) {
        switch (current_mode) {
            case LED_MODE_RECOVERY:
                for (int i = 0; i < 2; i++) {
                    gpio_set_level(LED_GPIO, 1);
                    vTaskDelay(pdMS_TO_TICKS(100));
                    gpio_set_level(LED_GPIO, 0);
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                vTaskDelay(pdMS_TO_TICKS(800));
                break;
            // ... other modes
        }
    }
}
```

Dedicated FreeRTOS task ensures non-blocking operation.

---

## Memory Management

### Flash Usage

| Component | Size | Notes |
|-----------|------|-------|
| Bootloader | 32KB | ESP-IDF default |
| Partition Table | 4KB | |
| NVS | 24KB | WiFi + system state |
| PHY Init | 4KB | RF calibration |
| Factory App | ~900KB | |
| OTA Partitions | 1MB each | Growth headroom |
| **Total** | ~3MB | 4MB flash recommended |

### RAM Usage (Estimated)

| Component | Heap | Stack | Total |
|-----------|------|-------|-------|
| FreeRTOS Kernel | 10KB | - | 10KB |
| WiFi Stack | 40KB | 8KB | 48KB |
| HTTP Server | 20KB | 4KB | 24KB |
| Application | 15KB | 8KB | 23KB |
| **Available** | ~130KB | - | ~130KB |
| **Total (ESP32)** | ~280KB DRAM | ~320KB SRAM |

Plenty of headroom for additional features.

---

## Security Considerations

### Current Implementation

- SHA256 integrity check (via `prepare-firmware.py`)
- WPA2-PSK for recovery AP
- HTTP (unencrypted) for OTA

### Integration Tests

1. **Happy Path**: Normal OTA update
2. **Network Failure**: Mid-download disconnect
3. **Power Loss Simulation**: Reset during flash write
4. **Crash Test**: Firmware with intentional crash
5. **Recovery Mode**: Full workflow test

---

## Performance Metrics

| Operation | Duration | Notes |
|-----------|----------|-------|
| Boot Time | ~2s | Factory partition |
| WiFi Connect | 3-5s | Depends on AP |
| OTA Download | ~30s | 900KB @ 256kbps |
| Flash Write | ~15s | Including verification |
| Validation Period | 10s | Mandatory |
| **Total OTA** | **~60s** | From trigger to validated boot |

---

## Future Improvements

1. **Delta Updates**: Binary diff to reduce download size
2. **Compressed Firmware**: gzip compression (ESP32 hardware decompression)
3. **A/B Testing**: Deploy to subset of devices
4. **Remote Monitoring**: MQTT telemetry for OTA status
5. **Batch Updates**: Scheduled deployment windows

---

## References

- [ESP-IDF OTA Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/ota.html)
- [ESP32 Flash Encryption](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/security/flash-encryption.html)
- [Secure Boot V2](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/security/secure-boot-v2.html)
- [Mermaid Diagram Syntax](https://mermaid.js.org/)
