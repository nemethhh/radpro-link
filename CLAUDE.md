# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**RadPro-Link** is firmware for the **Seeed Studio XIAO nRF54L15** board, built using **PlatformIO** with the **Zephyr RTOS** framework. It provides a BLE interface for RadPro radiation detector devices, exposing command and control functionality via BLE Nordic UART Service (NUS) and supporting additional protocols available on the nRF54L15.

**Key Security Feature**: Time-limited pairing window (1 minute by default) that allows device pairing only during initial boot, after which only previously bonded devices can connect.

## Build System

- **Framework**: Zephyr RTOS 3.4+
- **Build Tool**: PlatformIO
- **Target Board**: Seeed Studio XIAO nRF54L15 (nRF54L15 SoC)
- **Working Directory**: Repository root

### Common Commands

All commands must be run from the repository root directory:

```bash
# Build the firmware
pio run

# Upload to device
pio run -t upload

# Clean build
pio run -t clean

# Monitor console output via RTT (requires J-Link debugger)
# Use JLinkRTTClient, JLinkRTTLogger, or OpenOCD with RTT support
JLinkRTTClient
```

### Build Configuration Files

- `platformio.ini` - PlatformIO build configuration
- `zephyr/prj.conf` - Zephyr kernel and subsystem configuration
- `zephyr/CMakeLists.txt` - CMake build script with source files
- `zephyr/boards/xiao_nrf54l15_nrf54l15_cpuapp.overlay` - Device tree overlay for pin configuration

## Architecture

The firmware follows a **modular, layered architecture** with clear separation of concerns:

### Module Structure

```
src/
├── main.c                    # Application entry point, initialization sequence
├── ble/
│   ├── ble_service.c/.h      # BLE stack, advertising, NUS service, MTU management
├── uart/
│   ├── uart_bridge.c/.h      # UART async API, data buffering
├── security/
│   ├── security_manager.c/.h # Pairing window timer, bonding policy
├── led/
│   ├── led_status.c/.h       # LED status indication
└── board/
    └── board_config.c/.h     # Hardware initialization, pin configuration
```

### Data Flow

**UART → BLE Direction** (main.c:121-130):
1. UART module receives data via async API → calls `uart_data_handler()`
2. Handler checks if BLE is authenticated via `ble_service_is_authenticated()`
3. If authenticated, forwards data via `ble_service_send()`

**BLE → UART Direction** (main.c:132-139):
1. BLE module receives data via NUS characteristic → calls `ble_data_handler()`
2. Handler forwards directly to UART via `uart_bridge_send()`
3. Security check happens inside BLE module before callback (ble_service.c:158)

### Initialization Sequence (main.c:30-108)

The initialization order is critical:

1. **Board hardware** (`board_init()`) - GPIO, clocks
2. **LED status** (`led_status_init()`) - LED initialization
3. **Security manager** (`security_manager_init()`) - Start pairing window timer
4. **UART bridge** (`uart_bridge_init()`) - Configure UART21 with async API
5. **Bluetooth stack** (`bt_enable()`) - Initialize BLE controller
6. **Settings** (`settings_load()`) - Load bonding information from NVS
7. **BLE service** (`ble_service_init()`) - Register NUS callbacks
8. **Advertising** (`ble_service_start_advertising()`) - Start connectable advertising

### Threading Model

- **Main thread**: Initialization, then sleeps forever
- **Status monitor thread** (main.c:132-149): Updates LED status every second based on pairing window and connection state
- **System work queue**: Handles BLE callbacks, UART async callbacks
- **BLE RX thread**: Processes incoming BLE data (Zephyr internal, 2048 bytes stack)

### Security Architecture

**Pairing Window Concept** (security_manager.c):
- On boot, device accepts pairing for configured time (default 1 minute)
- After window closes, only bonded devices can connect
- Security level enforced: BLE Security Level 2 (authenticated encryption)
- Up to 4 devices can be bonded (CONFIG_BT_MAX_PAIRED=4)
- Bonding data persists in NVS (Non-Volatile Storage)

**Bond Management** (Automatic):
- When storage is full (4 bonds), new pairing automatically replaces oldest unused bond
- Enabled via CONFIG_BT_KEYS_OVERWRITE_OLDEST=y (zephyr/prj.conf:57)
- "Oldest unused" = least recently connected device
- Ensures seamless operation without manual bond clearing
- Each bond consumes ~200-300 bytes of NVS storage

**Security Checks**:
- Incoming BLE data rejected if security level < L2 (ble_service.c:158)
- Outgoing data only sent if authenticated (main.c:124)

### BLE Configuration

- **Service**: Nordic UART Service (NUS) - UUID 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
- **MTU**: Negotiated up to 247 bytes (244 bytes usable payload)
- **Device Name**: `RadPro-Link` (configurable in prj.conf)
- **Advertising**: Fast connectable advertising with NUS UUID
- **Connection**: Single connection (CONFIG_BT_MAX_CONN=1)
- **Address**: Static device address (privacy disabled for consistent MAC)

### Hardware Configuration (Device Tree Overlay)

- **UART21**: External device communication (D6/D7 pins, 115200 baud)
  - TX: P2.8 (D6), RX: P2.7 (D7)
  - Async API with DMA for efficient data transfer
  - Uses board default pinctrl configuration
- **LED0**: Status indication (P2.0, single LED)
  - Uses blink patterns to indicate different states
- **Console**: RTT (Real-Time Transfer via SWD debug interface)
  - nRF54L15 has no USB hardware on chip
  - XIAO board has USB-to-UART bridge but it's not connected to nRF54L15 console

### LED Status Patterns

The single onboard LED uses different blink patterns:
- **Rapid Flash (100ms)**: Error state
- **Fast Blink (250ms)**: Pairing window active, not connected
- **Medium Blink (500ms)**: Pairing window active AND connected
- **Off**: Pairing window closed (regardless of connection state)

## Important Notes

- **Working directory**: All commands run from repository root directory
- **Device tree**: Pin configurations in `.overlay` file take precedence over board defaults
- **C Standard**: Must use C11 (`-std=gnu11`) due to Zephyr BLE stack requirements
- **Memory constraints**: nRF54L15 has limited RAM; stack sizes tuned in prj.conf
- **No USB**: nRF54L15 lacks USB hardware; console uses RTT (Real-Time Transfer)
- **Async UART**: Uses Zephyr async API for efficient DMA-based transfers
- **Bonding storage**: Requires NVS partition in flash (configured automatically)

## Modifying the Code

**Adding a new module**:
1. Create subdirectory in `src/` with `.c` and `.h` files
2. Add source file to `zephyr/CMakeLists.txt` in `target_sources()`
3. Add include directory to `target_include_directories()`
4. Include header in `main.c` and call init function in `app_init()`

**Changing UART pins or baud rate**:
- Edit `zephyr/boards/xiao_nrf54l15_nrf54l15_cpuapp.overlay`
- To use custom pins: add custom `pinctrl` configuration sections
- To change baud rate: update `current-speed` property (default: 115200)

**Adjusting security parameters**:
- Pairing window: Change `PAIRING_WINDOW_MS` in `main.c:23`
- Max bonded devices: Change `CONFIG_BT_MAX_PAIRED` in `zephyr/prj.conf:36`
- Bond eviction: Controlled by `CONFIG_BT_KEYS_OVERWRITE_OLDEST` in `zephyr/prj.conf:57`
- Security level enforcement: See `ble_service.c:158` and `ble_service.c:222`

**Changing BLE device name**:
- Edit `CONFIG_BT_DEVICE_NAME` in `zephyr/prj.conf:33`

## Debugging

### Viewing Console Output

The firmware uses **RTT (Real-Time Transfer)** for console output, which requires a J-Link debugger connected to the SWD interface:

**Option 1: Using J-Link Tools (Recommended)**
```bash
# Install Segger J-Link tools from: https://www.segger.com/downloads/jlink/
JLinkRTTClient
# Or for logging to file:
JLinkRTTLogger -Device NRF54L15_XXAA -RTTChannel 0 -if SWD
```

**Option 2: Using OpenOCD with RTT**
```bash
openocd -f interface/jlink.cfg -c "transport select swd" \
        -f target/nrf54l15.cfg -c "init; rtt setup; rtt start; rtt server start 9090 0"
# Then connect with: telnet localhost 9090
```

**Important Notes:**
- `/dev/ttyACM0` is **NOT** the console - that's the USB-to-UART bridge (not connected to firmware console)
- Console output requires debugger connected to SWD pins during runtime
- `pio device monitor` will **NOT** work for viewing console logs

### Common Issues

- **Build errors**: Ensure you're in the repository root directory
- **Link errors**: Verify all source files listed in `zephyr/CMakeLists.txt`
- **No console output**: Connect J-Link debugger to SWD pins and use JLinkRTTClient
- **UART not working**: Verify connections to D6/D7 pins (P2.8/P2.7)
- **Security issues**: Check pairing window hasn't expired (visible in RTT logs)
- **Connection failures**: Verify device not bonded to max devices already
