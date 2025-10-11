# RadPro-Link

Wireless interface firmware for RadPro radiation detector devices, built for the Seeed Studio XIAO nRF54L15 board.

## Purpose

RadPro-Link connects to **RadPro radiation detector devices via UART** and provides a **secure wireless BLE link** for remote command and control. This allows you to:

- Wirelessly communicate with RadPro devices from your phone or computer
- Send commands and receive data over Bluetooth Low Energy
- Access RadPro functionality without physical cable connections
- Maintain secure, encrypted communication with bonded devices

The XIAO nRF54L15 board acts as a bridge, translating between the RadPro device's UART interface and BLE Nordic UART Service (NUS).

## Features

- **Bidirectional Data Bridge**: Seamlessly forwards data between UART and BLE Nordic UART Service
- **Secure Pairing**: Time-limited pairing window (1 minute default) for enhanced security
- **Device Bonding**: Remembers up to 4 bonded devices with persistent storage
- **Automatic Bond Management**: Oldest unused bonds are automatically replaced when limit is reached
- **High Throughput**: Supports MTU negotiation up to 247 bytes
- **Status Indication**: LED blink patterns for pairing, connection, and error states
- **Efficient Operation**: Async UART with DMA for optimal performance

## Hardware Requirements

- **Board**: Seeed Studio XIAO nRF54L15
- **RadPro Device**: Any RadPro radiation detector with UART interface
- **Connections to RadPro Device**:
  - Connect XIAO TX (Pin D6/P2.8) to RadPro RX
  - Connect XIAO RX (Pin D7/P2.7) to RadPro TX
  - Connect GND between devices
  - Connect XIAO Bat+ to RadPro VCC/3.3V
  - Baud Rate: 115200 (default)

## Quick Start

### Prerequisites

- [PlatformIO Core](https://platformio.org/install/cli) or [PlatformIO IDE](https://platformio.org/platformio-ide)
- Python 3.6 or later

### Building and Flashing

```bash
# Clone the repository
git clone <repository-url>
cd radpro-link/

# Build the firmware
pio run

# Upload to device
pio run -t upload

# Monitor serial output
pio device monitor
```

### Hardware Setup

1. Connect your XIAO nRF54L15 to your RadPro device via UART (see Hardware Requirements above)
2. Flash the firmware to your XIAO nRF54L15
3. Power on the XIAO board

### First-Time Pairing

1. Within 1 minute of boot, the device enters pairing mode (LED blinking)
2. Connect via BLE using your phone/computer (device name: `RadPro-Link`)
3. After pairing, the device will remember your connection
4. The LED will turn off once the pairing window closes
5. Subsequent connections only work with bonded devices
6. You can now send commands to your RadPro device wirelessly over BLE

## LED Status Patterns

The single onboard LED (P2.0) uses different blink patterns to indicate device status:

- **Rapid Flash (100ms)**: Error state
- **Fast Blink (250ms)**: Pairing window active, not connected
- **Medium Blink (500ms)**: Pairing window active AND connected
- **Off**: Pairing window closed (regardless of connection state)

## Project Structure

```
radpro-link/
├── platformio.ini              # PlatformIO configuration
├── src/                        # Source code
│   ├── main.c                  # Application entry point
│   ├── ble/                    # BLE service and stack management
│   ├── uart/                   # UART bridge with async API
│   ├── security/               # Pairing window and bonding
│   ├── led/                    # LED status indication
│   └── board/                  # Hardware initialization
└── zephyr/                     # Zephyr RTOS configuration
    ├── prj.conf                # Kernel and subsystem config
    ├── CMakeLists.txt          # Build configuration
    └── boards/                 # Board-specific overlays
```

## Configuration

### Pairing Window Duration

Edit `src/main.c:23`:
```c
#define PAIRING_WINDOW_MS (1 * 60 * 1000)  // 1 minute
```

### BLE Device Name

Edit `zephyr/prj.conf:33`:
```
CONFIG_BT_DEVICE_NAME="RadPro-Link"
```

### UART Settings

Edit `zephyr/boards/xiao_nrf54l15_nrf54l15_cpuapp.overlay` to change pins or baud rate.

### Maximum Bonded Devices

Edit `zephyr/prj.conf:36`:
```
CONFIG_BT_MAX_PAIRED=4
```

When this limit is reached, the oldest unused bond is automatically replaced by new pairings (CONFIG_BT_KEYS_OVERWRITE_OLDEST=y). This ensures seamless pairing without manual bond management.

## Security

- **Encryption**: BLE Security Level 2 (authenticated encryption)
- **Pairing Window**: Limited time window for initial pairing
- **Bonding**: Persistent device whitelist stored in NVS (up to 4 devices)
- **Bond Management**: Automatic eviction of oldest unused bonds when storage is full
- **Authentication**: All data transfer requires authenticated connection

## Debugging

```bash
# Clean build
pio run -t clean

# Verbose build output
pio run -v

# Monitor RTT console
pio device monitor
```

### Common Issues

- **Build errors**: Ensure you're in the repository root directory
- **Upload fails**: Check USB connection and try resetting the board
- **Can't pair**: Verify pairing window hasn't expired (check serial logs)
- **Old device can't connect**: If you've paired 4+ devices, the oldest bond may have been automatically replaced. Re-pair the device during the pairing window.

## Technical Details

- **Framework**: Zephyr RTOS 3.4+
- **SoC**: nRF54L15 (ARM Cortex-M33)
- **BLE**: Nordic UART Service (NUS)
- **Console**: RTT (Real-Time Transfer, no USB CDC)
- **Memory**: Optimized for limited RAM with async operations

## Development

See [CLAUDE.md](CLAUDE.md) for detailed architecture documentation and development guidelines.

## License

See [LICENSE.md](LICENSE.md) for license information.

## Contributing

Contributions are welcome! Please ensure code follows the existing modular architecture and passes all tests.
