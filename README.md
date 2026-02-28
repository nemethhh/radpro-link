# RadPro-Link

Firmware for Seeed Studio XIAO nRF54L15 that bridges a RadPro device UART link to BLE (Nordic UART Service).

## What It Does

- Bridges UART data bidirectionally between RadPro and BLE NUS.
- Uses a pairing window on boot (default 1 minute), then allows only bonded devices.
- Stores up to 4 bonds in NVS and overwrites the oldest when full.
- Enforces encrypted BLE links (`BT_SECURITY_L2+`) before data forwarding.
- Exposes MCUmgr-over-BLE (SMP) for OTA firmware update flows.

## Hardware

- Board: Seeed Studio XIAO nRF54L15
- RadPro device with UART
- UART wiring (from `zephyr/boards/xiao_nrf54l15_nrf54l15_cpuapp.overlay`):
  - XIAO `D6 / P2.8` (TX) -> RadPro RX
  - XIAO `D7 / P2.7` (RX) -> RadPro TX
  - GND -> GND
- BLE device name: `RadPro-Link`

## Build and Flash

Prerequisite: PlatformIO CLI installed.

```bash
pio run
pio run -t upload
```

Default PlatformIO environment is `seeed-xiao-nrf54l15` (`platformio.ini`).

## Factory Reset

Restore factory settings on XIAO nRF54L15 if the board gets into a bad state
(for example, upload failures caused by internal NVM write protection).

The factory reset flow performs a flash mass erase and programs factory
firmware.

The `platform-seeedboards` scripts are not committed in this repository.
Clone the upstream repository and use the scripts from there:

```bash
git clone https://github.com/Seeed-Studio/platform-seeedboards.git
cd platform-seeedboards/scripts/factory_reset
```

The scripts automatically create/manage a local Python virtual environment and
install required tools.

### Windows

```powershell
cd platform-seeedboards\scripts\factory_reset
.\factory_reset.bat
```

### Linux and macOS

```bash
bash factory_reset.sh
```

Reference:
https://github.com/Seeed-Studio/platform-seeedboards/tree/main/scripts/factory_reset

## Runtime Behavior

### Pairing and Security

- Pairing is accepted only during the startup window (`PAIRING_WINDOW_MS` in `src/main.c`).
- After the window closes, new pairing is rejected and only existing bonds can reconnect.
- Data path checks:
  - UART -> BLE sends only when BLE link is authenticated/encrypted.
  - BLE -> UART input is dropped when connection security is below L2.

### LED Status

Single onboard LED (`led0`) patterns:

- `100 ms` blink: error
- `250 ms` blink: pairing window open, not connected
- `500 ms` blink: pairing window open, connected
- off: pairing window closed

## OTA / DFU

DFU module initializes MCUmgr SMP over BLE (`src/dfu/dfu_service.c`).

Relevant config in `zephyr/prj.conf`:

- `CONFIG_BOOTLOADER_MCUBOOT=y`
- `CONFIG_MCUMGR=y`
- `CONFIG_MCUMGR_TRANSPORT_BT=y`
- `CONFIG_MCUMGR_TRANSPORT_BT_PERM_RW_AUTHEN=y`

Use an MCUmgr-compatible client (for example nRF Connect Device Manager) for OTA operations.

## Logs and Monitoring

This repo currently has mixed console/log settings:

- `zephyr/prj.conf` enables `CONFIG_UART_CONSOLE` and `CONFIG_LOG_BACKEND_UART`
- `zephyr/boards/xiao_nrf54l15_nrf54l15_cpuapp.conf` enables `CONFIG_RTT_CONSOLE`

If `pio device monitor` does not show logs in your setup, use RTT tools (for example Segger RTT client) via SWD.

## Configuration Knobs

- Pairing window: `src/main.c` (`PAIRING_WINDOW_MS`)
- BLE name / bond limit / MCUmgr: `zephyr/prj.conf`
- Bridge UART selection/pins: `zephyr/boards/xiao_nrf54l15_nrf54l15_cpuapp.overlay`

## Repo Layout

```text
src/
  main.c                  app init and data flow wiring
  ble/                    BLE advertising, NUS, connection/security callbacks
  uart/                   async UART bridge and buffering
  security/               pairing-window policy and auth callbacks
  led/                    status LED thread/patterns
  board/                  board abstraction/init
  dfu/                    MCUmgr/OTA init hook
zephyr/
  prj.conf                Zephyr/Kconfig settings
  CMakeLists.txt          app sources/includes
  boards/                 board overlay/conf
```

## License

[MIT](LICENSE)
