# RadPro-Link: Cleanup & OTA Feature Design

**Date**: 2026-02-28
**Scope**: Full code cleanup + BLE FOTA (Firmware Over-The-Air) update support

## Context

RadPro-Link is firmware for the Seeed Studio XIAO nRF54L15, providing a BLE-to-UART bridge for RadPro radiation detectors. The codebase works but has accumulated inconsistencies, dead code, and missing safety checks. Additionally, wireless firmware update capability is needed for field deployment.

## Board Specs

- MCU: Arm Cortex-M33 128 MHz + RISC-V coprocessor 128 MHz (FLPR)
- Memory: 1.5 MB NVM, 256 KB RAM
- Wireless: BLE 6.0, Thread, Zigbee, NFC, Matter
- Battery: Built-in Li-ion management

## Part 1: Code Cleanup

### 1.1 Functional/Safety Fixes

| Issue | Location | Fix |
|-------|----------|-----|
| UART buffer too small for negotiated MTU (40 bytes vs 244 byte max payload) | uart_bridge.c:16 | Increase UART_BUF_SIZE to 256 |
| uart_bridge_send() returns 0 after partial failure | uart_bridge.c:271-314 | Return actual error; track partial sends |
| GATT callback registration lacks error check | ble_service.c:208 | Add error check like NUS registration |
| Missing ARG_UNUSED(ctx) in NUS callback | ble_service.c:155 | Add ARG_UNUSED(ctx) |

### 1.2 Logging Consistency

| Issue | Location | Fix |
|-------|----------|-----|
| 25 printk() calls mixed with LOG_*() | main.c | Replace all printk with LOG_INF/LOG_ERR |
| DBG calls in modules registered at INF level | ble_service.c, uart_bridge.c | Register at LOG_LEVEL_DBG |

### 1.3 Dead Code Removal

| Item | Location | Action |
|------|----------|--------|
| Unused PULSE_STEP_MS macro | led_status.c:19 | Remove |
| Unused led_status_start() function | led_status.c:129-133, .h:45 | Remove from both files |
| Explicit .notif_enabled = NULL | ble_service.c:173 | Remove (struct zeroes it) |
| Commented-out config options | prj.conf:84-87 | Remove |

### 1.4 Naming & Constants

| Issue | Location | Fix |
|-------|----------|-----|
| Hard-coded 500ms blink interval | led_status.c:114 | Define MEDIUM_BLINK_INTERVAL_MS |
| UART_WAIT_FOR_RX missing unit suffix | uart_bridge.c:18 | Rename to UART_WAIT_FOR_RX_MS |

### 1.5 Build System & Config

| Issue | Location | Fix |
|-------|----------|-----|
| CMake project name "uart_to_ble_bridge" | CMakeLists.txt:19 | Rename to "radpro_link" |
| Redundant UART21 overlay config | .overlay | Remove redundant status/current-speed |
| Misleading overlay comments about USB console | .overlay:7 | Update to mention RTT |

## Part 2: BLE FOTA Support

### Architecture

```
+------------------+     +------------------+     +------------------+
| nRF Connect      |     | MCUmgr SMP       |     | MCUboot          |
| Device Manager   | --> | Server (BLE)     | --> | Bootloader       |
| (Mobile App)     |     | (dfu_service.c)  |     | (validates/swaps)|
+------------------+     +------------------+     +------------------+
```

**Update flow**:
1. User builds new firmware -> generates `app_update.bin` / `dfu_application.zip`
2. User opens nRF Connect Device Manager app on phone
3. App connects to RadPro-Link via BLE
4. MCUmgr SMP server receives image over BLE, writes to secondary flash slot
5. Device reboots; MCUboot validates new image and swaps slots
6. New firmware runs; if valid, confirms image; if crash, MCUboot reverts

### New Module: dfu_service

```
src/dfu/
├── dfu_service.c  # MCUmgr SMP BLE transport init, image mgmt registration
└── dfu_service.h  # Public API: dfu_service_init()
```

**API**:
```c
int dfu_service_init(void);  // Call after bt_enable(), registers SMP handlers
```

**Responsibilities**:
- Register MCUmgr image management group
- Register MCUmgr OS management group (reboot support)
- Initialize SMP BLE transport
- Optionally add SMP service UUID to advertising data

### Kconfig Additions (prj.conf)

```ini
# MCUboot bootloader
CONFIG_BOOTLOADER_MCUBOOT=y

# MCUmgr core
CONFIG_MCUMGR=y
CONFIG_MCUMGR_TRANSPORT_BT=y
CONFIG_MCUMGR_GRP_IMG=y
CONFIG_MCUMGR_GRP_OS=y

# Flash operations for image swap
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_IMG_MANAGER=y
CONFIG_STREAM_FLASH=y

# BLE buffers for FOTA throughput
CONFIG_BT_L2CAP_TX_MTU=252
CONFIG_BT_BUF_ACL_RX_SIZE=256

# Work queue stack for large MCUmgr operations
CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=2304

# SMP authentication (require bonding before DFU)
CONFIG_MCUMGR_TRANSPORT_BT_PERM_RW_AUTHEN=y
```

### Security Considerations

- **SMP authentication**: `CONFIG_MCUMGR_TRANSPORT_BT_PERM_RW_AUTHEN=y` requires the remote device to be bonded before accessing SMP characteristics. This integrates with the existing pairing window mechanism.
- **Image validation**: MCUboot validates image signatures before booting
- **Rollback**: If new image crashes, MCUboot automatically reverts to previous working image

### Flash Partition Layout

MCUboot requires dual-slot partitioning. With 1.5MB NVM:
- Bootloader: ~48KB
- Slot 0 (active): ~700KB
- Slot 1 (update): ~700KB
- NVS/Settings: ~32KB

This may need a static partition file or PlatformIO-specific configuration.

### Risk: PlatformIO + MCUboot

PlatformIO's Zephyr framework support for MCUboot may need:
- Custom partition layout file
- sysbuild configuration
- Specific board-level MCUboot overlay

This needs validation during implementation. If PlatformIO doesn't support MCUboot natively, we may need to build MCUboot separately and flash it as a prerequisite.

## Out of Scope

- Callback signature unification (different semantics justify different signatures)
- UART callback static variable pattern (fragile but correct Zephyr pattern)
- Thread stack size changes (no evidence of issues)
- Additional peripherals (I2C, SPI, ADC, button) - future work

## File Change Summary

| File | Action |
|------|--------|
| src/main.c | Rewrite (logging, DFU init) |
| src/ble/ble_service.c | Rewrite (error checks, cleanup) |
| src/ble/ble_service.h | Minor edit |
| src/uart/uart_bridge.c | Rewrite (buffers, error handling, naming) |
| src/uart/uart_bridge.h | No change |
| src/security/security_manager.c | Minor cleanup (logging) |
| src/security/security_manager.h | No change |
| src/led/led_status.c | Rewrite (dead code, constants) |
| src/led/led_status.h | Edit (remove led_status_start) |
| src/board/board_config.c | Minor cleanup (logging) |
| src/board/board_config.h | No change |
| src/dfu/dfu_service.c | **NEW** |
| src/dfu/dfu_service.h | **NEW** |
| zephyr/prj.conf | Rewrite (MCUboot, MCUmgr, cleanup) |
| zephyr/CMakeLists.txt | Edit (rename, add dfu) |
| zephyr/boards/*.overlay | Edit (remove redundancy, fix comments) |
| CLAUDE.md | Update (reflect new architecture) |

## Sources

- [Nordic nRF54L FOTA documentation](https://github.com/nrfconnect/sdk-nrf/blob/main/doc/nrf/app_dev/device_guides/nrf54l/fota_update.rst)
- [Nordic DevZone: DFU and OTA on nRF54L15](https://devzone.nordicsemi.com/f/nordic-q-a/125576/enabled-dfu-and-ota-in-nrf54l15)
- [Seeed Studio XIAO nRF54L15 Wiki](https://wiki.seeedstudio.com/xiao_nrf54l15_sense_getting_started/)
- [Adding OTA DFU to Nordic Zephyr Project](https://getwavecake.com/blog/adding-ota-dfu-to-a-nordic-zephyr-project/)
- [Zephyr DFU Documentation](https://docs.zephyrproject.org/latest/services/device_mgmt/dfu.html)
