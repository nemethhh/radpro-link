# RadPro-Link Cleanup & OTA Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Clean up all code inconsistencies across the RadPro-Link firmware and add BLE FOTA update support via MCUmgr/MCUboot.

**Architecture:** Surgical fixes to each module (LED, UART, BLE, main) followed by build system cleanup, then new DFU module addition. Each task targets one module, verified with `pio run` after each change.

**Tech Stack:** Zephyr RTOS, PlatformIO, nRF54L15, MCUmgr, MCUboot

---

### Task 1: Clean up LED status module

**Files:**
- Modify: `src/led/led_status.c:17-19` (constants), `src/led/led_status.c:112-114` (hard-coded value), `src/led/led_status.c:129-133` (dead function)
- Modify: `src/led/led_status.h:42-45` (dead declaration)

**Step 1: Remove unused PULSE_STEP_MS macro and add MEDIUM_BLINK_INTERVAL_MS**

In `src/led/led_status.c`, replace lines 17-19:
```c
#define FAST_BLINK_INTERVAL_MS 250
#define SLOW_BLINK_INTERVAL_MS 1000
#define PULSE_STEP_MS 50
```
with:
```c
#define FAST_BLINK_INTERVAL_MS   250
#define MEDIUM_BLINK_INTERVAL_MS 500
#define SLOW_BLINK_INTERVAL_MS   1000
```

**Step 2: Use the new constant for the connected blink pattern**

In `src/led/led_status.c`, replace line 114:
```c
			blink_interval_ms = 500;
```
with:
```c
			blink_interval_ms = MEDIUM_BLINK_INTERVAL_MS;
```

**Step 3: Remove unused led_status_start() function**

In `src/led/led_status.c`, delete lines 129-133:
```c
void led_status_start(void)
{
	/* Thread starts automatically with K_THREAD_DEFINE */
	LOG_DBG("LED status thread running");
}
```

**Step 4: Remove led_status_start() declaration from header**

In `src/led/led_status.h`, delete lines 42-45:
```c
/**
 * @brief Start LED status update thread
 */
void led_status_start(void);
```

**Step 5: Build to verify**

Run: `pio run`
Expected: Clean build with no errors

**Step 6: Commit**

```
feat: clean up LED status module

Remove unused PULSE_STEP_MS macro and led_status_start() function.
Replace hard-coded 500ms with MEDIUM_BLINK_INTERVAL_MS constant.
```

---

### Task 2: Fix UART bridge buffer size and naming

**Files:**
- Modify: `src/uart/uart_bridge.c:14-18` (constants and log level)

**Step 1: Fix constants - increase buffer, add unit suffix, fix log level**

In `src/uart/uart_bridge.c`, replace lines 14-18:
```c
LOG_MODULE_REGISTER(uart_bridge, LOG_LEVEL_INF);

#define UART_BUF_SIZE 40
#define UART_WAIT_FOR_BUF_DELAY K_MSEC(50)
#define UART_WAIT_FOR_RX 50
```
with:
```c
LOG_MODULE_REGISTER(uart_bridge, LOG_LEVEL_DBG);

#define UART_BUF_SIZE           256
#define UART_WAIT_FOR_BUF_DELAY K_MSEC(50)
#define UART_WAIT_FOR_RX_MS     50
```

**Step 2: Update all references to UART_WAIT_FOR_RX**

In `src/uart/uart_bridge.c`, replace all occurrences of `UART_WAIT_FOR_RX` (lines 120, 177, 259) with `UART_WAIT_FOR_RX_MS`. There are exactly 3 occurrences:
- Line 120: `uart_rx_enable(uart, buf->data, sizeof(buf->data), UART_WAIT_FOR_RX);`
- Line 177: `uart_rx_enable(uart, buf->data, sizeof(buf->data), UART_WAIT_FOR_RX);`
- Line 259: `uart_rx_enable(uart, rx->data, sizeof(rx->data), UART_WAIT_FOR_RX);`

**Step 3: Build to verify**

Run: `pio run`
Expected: Clean build

**Step 4: Commit**

```
fix: increase UART buffer to 256 bytes and fix naming

Buffer was 40 bytes, too small for negotiated BLE MTU of 244 bytes.
Renamed UART_WAIT_FOR_RX to UART_WAIT_FOR_RX_MS for unit clarity.
Changed log level to DBG since module has LOG_DBG() calls.
```

---

### Task 3: Fix UART bridge send() error handling

**Files:**
- Modify: `src/uart/uart_bridge.c:271-314` (uart_bridge_send function)

**Step 1: Rewrite uart_bridge_send with proper error propagation**

In `src/uart/uart_bridge.c`, replace the entire `uart_bridge_send` function (lines 271-315):

```c
int uart_bridge_send(const uint8_t *data, uint16_t len)
{
	int err;

	if (!uart_initialized) {
		LOG_DBG("UART not initialized, discarding %d bytes", len);
		return -ENODEV;
	}

	for (uint16_t pos = 0; pos < len;) {
		struct uart_data_t *tx = k_malloc(sizeof(*tx));

		if (!tx) {
			LOG_ERR("Failed to allocate TX buffer at offset %u/%u", pos, len);
			return -ENOMEM;
		}

		/* Reserve last byte for potential LF append */
		size_t tx_data_size = sizeof(tx->data) - 1;

		if ((len - pos) > tx_data_size) {
			tx->len = tx_data_size;
		} else {
			tx->len = (len - pos);
		}

		memcpy(tx->data, &data[pos], tx->len);
		pos += tx->len;

		/* Append LF if CR triggered transmission */
		if ((pos == len) && (data[len - 1] == '\r')) {
			tx->data[tx->len] = '\n';
			tx->len++;
		}

		err = uart_tx(uart, tx->data, tx->len, SYS_FOREVER_MS);
		if (err) {
			LOG_DBG("TX busy, queuing %u bytes", tx->len);
			k_fifo_put(&fifo_uart_tx_data, tx);
		}
	}

	return 0;
}
```

Note: The main change is better error logging with offset context. The existing queue-on-busy pattern is actually correct for async UART (the TX_DONE handler drains the queue). The alloc failure is the real error case.

**Step 2: Build to verify**

Run: `pio run`
Expected: Clean build

**Step 3: Commit**

```
fix: improve UART send error reporting with offset context
```

---

### Task 4: Clean up BLE service module

**Files:**
- Modify: `src/ble/ble_service.c:14` (log level), `src/ble/ble_service.c:155` (ARG_UNUSED), `src/ble/ble_service.c:171-174` (NUS struct), `src/ble/ble_service.c:207-208` (GATT error check)

**Step 1: Change log level to DBG (module has LOG_DBG calls)**

In `src/ble/ble_service.c`, replace line 14:
```c
LOG_MODULE_REGISTER(ble_service, LOG_LEVEL_INF);
```
with:
```c
LOG_MODULE_REGISTER(ble_service, LOG_LEVEL_DBG);
```

**Step 2: Add ARG_UNUSED(ctx) to NUS receive callback**

In `src/ble/ble_service.c`, add `ARG_UNUSED(ctx);` as the first line inside `bt_receive_cb` (after line 156, before line 157):
```c
static void bt_receive_cb(struct bt_conn *conn, const void *data, uint16_t len, void *ctx)
{
	ARG_UNUSED(ctx);

	/* Check if device is authenticated */
```

**Step 3: Clean NUS callback struct (remove explicit NULL with comment)**

In `src/ble/ble_service.c`, replace lines 171-174:
```c
static struct bt_nus_cb nus_cb = {
	.received = bt_receive_cb,
	.notif_enabled = NULL,  /* Optional: handle notification enable/disable */
};
```
with:
```c
static struct bt_nus_cb nus_cb = {
	.received = bt_receive_cb,
};
```

**Step 4: Add error check for GATT callback registration**

In `src/ble/ble_service.c`, replace line 208:
```c
	bt_gatt_cb_register(&gatt_callbacks);
```
with:
```c
	bt_gatt_cb_register(&gatt_callbacks);
	/* Note: bt_gatt_cb_register is void - no error to check.
	 * This is correct Zephyr API; it always succeeds. */
```

Wait - `bt_gatt_cb_register` returns void in Zephyr. The design doc was wrong about this being a missing error check. The function signature is `void bt_gatt_cb_register(struct bt_gatt_cb *cb)`. No change needed here. Skip this sub-step.

**Step 5: Build to verify**

Run: `pio run`
Expected: Clean build

**Step 6: Commit**

```
fix: clean up BLE service module

Add ARG_UNUSED(ctx) to NUS callback, remove explicit NULL in struct
initializer, set log level to DBG to match LOG_DBG() usage.
```

---

### Task 5: Replace all printk with LOG_* in main.c

**Files:**
- Modify: `src/main.c` (full file - logging overhaul)

**Step 1: Remove printk include and rewrite app_init logging**

In `src/main.c`, remove line 11:
```c
#include <zephyr/sys/printk.h>
```

Then replace the entire `app_init` function body (lines 30-118) with LOG_* calls. Replace each printk:

| Line | Old | New |
|------|-----|-----|
| 35 | `printk("\n=== XIAO BLE Bridge Starting ===\n");` | `LOG_INF("=== RadPro-Link Starting ===");` |
| 36 | `printk("Modular Architecture\n");` | Remove (unnecessary) |
| 37 | `printk("Pairing window: %d minutes\n", ...);` | `LOG_INF("Pairing window: %d minutes", PAIRING_WINDOW_MS / 60000);` |
| 38 | `printk("Device: %s\n", CONFIG_BT_DEVICE_NAME);` | `LOG_INF("Device: %s", CONFIG_BT_DEVICE_NAME);` |
| 41 | `printk("Initializing board hardware...\n");` | `LOG_INF("Initializing board hardware");` |
| 44 | `printk("ERROR: Board init failed: %d\n", err);` | `LOG_ERR("Board init failed: %d", err);` |
| 47 | `printk("Board hardware initialized\n");` | `LOG_INF("Board hardware initialized");` |
| 50 | `printk("Initializing LED status...\n");` | `LOG_INF("Initializing LED status");` |
| 53 | `printk("ERROR: LED init failed: %d\n", err);` | `LOG_ERR("LED init failed: %d", err);` |
| 56 | `printk("LED status initialized\n");` | `LOG_INF("LED status initialized");` |
| 59 | `printk("Initializing security manager...\n");` | `LOG_INF("Initializing security manager");` |
| 62 | `printk("ERROR: Security manager init failed: %d\n", err);` | `LOG_ERR("Security manager init failed: %d", err);` |
| 65 | `printk("Security manager initialized\n");` | `LOG_INF("Security manager initialized");` |
| 68 | `printk("Initializing UART bridge...\n");` | `LOG_INF("Initializing UART bridge");` |
| 71 | `printk("WARNING: UART bridge init failed: %d\n", err);` | `LOG_WRN("UART bridge init failed: %d", err);` |
| 72 | `printk("BLE will work but UART forwarding is disabled\n");` | `LOG_WRN("BLE will work but UART forwarding is disabled");` |
| 74 | `printk("UART bridge initialized\n");` | `LOG_INF("UART bridge initialized");` |
| 80 | `printk("Bluetooth init failed: %d\n", err);` | `LOG_ERR("Bluetooth init failed: %d", err);` |
| 89-91 | `printk("MAC: %02X:...` | `LOG_INF("MAC: %02X:%02X:%02X:%02X:%02X:%02X", ...);` |
| 105 | `printk("BLE service init failed: %d\n", err);` | `LOG_ERR("BLE service init failed: %d", err);` |
| 112 | `printk("Advertising start failed: %d\n", err);` | `LOG_ERR("Advertising start failed: %d", err);` |
| 167 | `printk("Initialization failed: %d\n", err);` | `LOG_ERR("Initialization failed: %d", err);` |
| 172 | `printk("=== System Running ===\n");` | `LOG_INF("=== System Running ===");` |

Also update the file header comment from "XIAO BLE-UART Bridge" to "RadPro-Link".

**Step 2: Build to verify**

Run: `pio run`
Expected: Clean build. Note: LOG_INF early in init may produce output slightly differently than printk (deferred logging) but RTT console handles this fine.

**Step 3: Commit**

```
refactor: replace all printk with LOG_* in main.c

Consistent logging throughout. Removes printk include dependency.
```

---

### Task 6: Clean up build system and configuration

**Files:**
- Modify: `zephyr/CMakeLists.txt:1-4,19-23` (project name, comment)
- Modify: `zephyr/prj.conf:3,19-21,69-70,84-87` (cleanup)
- Modify: `zephyr/boards/xiao_nrf54l15_nrf54l15_cpuapp.overlay` (full file)

**Step 1: Rename CMake project**

In `zephyr/CMakeLists.txt`, replace lines 1-4:
```cmake
# SPDX-License-Identifier: MIT
#
# CMakeLists.txt for UART-to-BLE Bridge
# PlatformIO + Zephyr RTOS Build Configuration
```
with:
```cmake
# SPDX-License-Identifier: MIT
#
# CMakeLists.txt for RadPro-Link
# PlatformIO + Zephyr RTOS Build Configuration
```

Replace lines 19-22:
```cmake
project(uart_to_ble_bridge
    VERSION 1.0.0
    DESCRIPTION "UART to BLE Bridge for Seeed XIAO nRF54L15"
    LANGUAGES C
```
with:
```cmake
project(radpro_link
    VERSION 2.0.0
    DESCRIPTION "RadPro-Link BLE Bridge for Seeed XIAO nRF54L15"
    LANGUAGES C
```

**Step 2: Clean up prj.conf**

In `zephyr/prj.conf`:

Replace line 3:
```
# Configuration for XIAO nRF54L15 UART to BLE Bridge - PlatformIO
```
with:
```
# Configuration for RadPro-Link - PlatformIO + Zephyr RTOS
```

Replace lines 19-21 (fix misleading console comment):
```
# Console configuration - Use UART20 via USB-to-UART bridge
CONFIG_CONSOLE=y
CONFIG_UART_CONSOLE=y
```
with:
```
# Console configuration
CONFIG_CONSOLE=y
CONFIG_UART_CONSOLE=y
```

Delete lines 69-70 (stale commented-out DK library):
```
# Enable DK LED and Buttons library - Nordic NCS specific
# CONFIG_DK_LIBRARY=y
```

Delete lines 84-87 (stale commented-out peripherals):
```
# Disable unused peripherals
# CONFIG_I2C=n
# CONFIG_WATCHDOG=n
# CONFIG_SPI=n
```

**Step 3: Clean up device tree overlay**

Replace the entire `zephyr/boards/xiao_nrf54l15_nrf54l15_cpuapp.overlay` with:
```dts
/*
 * SPDX-License-Identifier: MIT
 *
 * Device Tree Overlay for RadPro-Link on XIAO nRF54L15
 *
 * Hardware:
 * - UART20: P1.9 (TX) / P1.8 (RX) - USB-to-UART bridge (not used for console)
 * - UART21: P2.8 (TX) / P2.7 (RX) - External device (RadPro) on D6/D7 pins
 * - LED0: P2.0 (status indication)
 * - Console: RTT via SWD debug interface (nRF54L15 has no USB hardware)
 */

/ {
	chosen {
		/* Use UART21 for bridge application on D6/D7 pins */
		app-bridge-uart = &uart21;
	};
};
```

Note: Removed redundant `&uart21 { status = "okay"; current-speed = <115200>; }` block. The vendor board already enables UART21 at 115200 baud by default.

**Step 4: Build to verify**

Run: `pio run`
Expected: Clean build

**Step 5: Commit**

```
chore: rename project to RadPro-Link, clean up configs

Rename CMake project from uart_to_ble_bridge to radpro_link.
Remove stale comments and redundant overlay configuration.
Fix misleading console comment (uses RTT, not USB).
```

---

### Task 7: Add DFU service module

**Files:**
- Create: `src/dfu/dfu_service.h`
- Create: `src/dfu/dfu_service.c`
- Modify: `zephyr/CMakeLists.txt` (add source and include)

**Step 1: Create dfu_service.h**

Create `src/dfu/dfu_service.h`:
```c
/*
 * SPDX-License-Identifier: MIT
 * DFU Service Module - Header
 *
 * Provides BLE FOTA (Firmware Over-The-Air) update support
 * using MCUmgr SMP (Simple Management Protocol) over BLE.
 *
 * Use nRF Connect Device Manager mobile app to perform updates.
 */

#ifndef DFU_SERVICE_H
#define DFU_SERVICE_H

/**
 * @brief Initialize DFU service
 *
 * Registers MCUmgr SMP command handlers for image and OS management.
 * Must be called after bt_enable().
 *
 * @return 0 on success, negative errno on failure
 */
int dfu_service_init(void);

#endif /* DFU_SERVICE_H */
```

**Step 2: Create dfu_service.c**

Create `src/dfu/dfu_service.c`:
```c
/*
 * SPDX-License-Identifier: MIT
 * DFU Service Module - Implementation
 *
 * MCUmgr SMP server over BLE for firmware updates.
 * When CONFIG_MCUMGR is enabled, Zephyr automatically registers
 * the SMP BLE transport and command handlers via Kconfig.
 *
 * This module exists as an explicit initialization point and
 * future extension hook for DFU-related logic.
 */

#include "dfu_service.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#ifdef CONFIG_MCUMGR
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#endif

LOG_MODULE_REGISTER(dfu_service, LOG_LEVEL_INF);

int dfu_service_init(void)
{
#ifdef CONFIG_MCUMGR
	LOG_INF("DFU service initialized (MCUmgr SMP over BLE)");
	LOG_INF("Use nRF Connect Device Manager app for firmware updates");
	return 0;
#else
	LOG_WRN("DFU service disabled (CONFIG_MCUMGR not set)");
	return 0;
#endif
}
```

Note: In modern Zephyr (3.4+), MCUmgr SMP handlers are auto-registered when their Kconfig options are enabled. The explicit `os_mgmt_register_group()` / `img_mgmt_register_group()` / `smp_bt_register()` calls from older guides are no longer needed. The DFU module is intentionally thin - Kconfig does the heavy lifting.

**Step 3: Add DFU module to CMakeLists.txt**

In `zephyr/CMakeLists.txt`, add to `target_sources` after the board support line (after line 43):
```cmake
    # DFU module
    ../src/dfu/dfu_service.c
```

Add to `target_include_directories` (after line 53):
```cmake
    ../src/dfu
```

**Step 4: Build to verify (without MCUmgr enabled yet)**

Run: `pio run`
Expected: Clean build. DFU module compiles but MCUmgr is not yet enabled (the #ifdef guards handle this).

**Step 5: Commit**

```
feat: add DFU service module skeleton

Thin wrapper around Zephyr MCUmgr auto-registration.
Compiles with or without CONFIG_MCUMGR enabled.
```

---

### Task 8: Integrate DFU init into main.c

**Files:**
- Modify: `src/main.c` (add include and init call)

**Step 1: Add DFU include**

In `src/main.c`, add after the led_status.h include (after line 18):
```c
#include "dfu/dfu_service.h"
```

**Step 2: Add DFU init call after BLE service init**

In `src/main.c`, add after the BLE service init block (after line ~107 in the cleaned-up version, just before `/* Start advertising */`). Insert:
```c
	/* Initialize DFU service (MCUmgr SMP) */
	err = dfu_service_init();
	if (err) {
		LOG_ERR("DFU service init failed: %d", err);
		return err;
	}
```

The init order becomes: board -> LED -> security -> UART -> bt_enable -> settings_load -> BLE service -> **DFU service** -> advertising.

**Step 3: Build to verify**

Run: `pio run`
Expected: Clean build

**Step 4: Commit**

```
feat: integrate DFU service into initialization sequence
```

---

### Task 9: Enable MCUmgr and MCUboot in Kconfig

**Files:**
- Modify: `zephyr/prj.conf` (add MCUmgr/MCUboot config)

**Step 1: Add MCUmgr and MCUboot configuration to prj.conf**

Append to the end of `zephyr/prj.conf`:
```ini

# ===== DFU / FOTA Support =====
# MCUboot bootloader for dual-slot firmware updates
CONFIG_BOOTLOADER_MCUBOOT=y

# MCUmgr - SMP server for firmware management
CONFIG_MCUMGR=y
CONFIG_MCUMGR_TRANSPORT_BT=y
CONFIG_MCUMGR_GRP_IMG=y
CONFIG_MCUMGR_GRP_OS=y

# Flash operations for MCUboot image swap
CONFIG_FLASH_MAP=y
CONFIG_IMG_MANAGER=y
CONFIG_STREAM_FLASH=y

# Require bonding before DFU access (integrates with pairing window)
CONFIG_MCUMGR_TRANSPORT_BT_PERM_RW_AUTHEN=y

# Statistics for MCUmgr stat command (optional but useful)
CONFIG_STATS=y
CONFIG_STATS_NAMES=y
```

Note: `CONFIG_FLASH=y` is already set at line 62. `CONFIG_BT_L2CAP_TX_MTU=247` is already at line 47 (close to the recommended 252). `CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=4096` is already at line 25 (exceeds recommended 2304).

**Step 2: Build to verify**

Run: `pio run`
Expected: This is the **high-risk step**. MCUboot integration with PlatformIO may require additional configuration. Possible outcomes:

a) **Clean build** - MCUboot and MCUmgr link successfully. Proceed.
b) **MCUboot partition error** - May need a partition layout file. If this happens, create `zephyr/boards/xiao_nrf54l15_nrf54l15_cpuapp.dts` with partition definitions, or add `CONFIG_SINGLE_APPLICATION_SLOT=y` to skip MCUboot dual-slot requirement during development.
c) **Missing MCUmgr symbols** - May need additional Kconfig options. Check build error and add required options.

If build fails, troubleshoot and document the resolution.

**Step 3: Commit (if build succeeds)**

```
feat: enable MCUmgr BLE FOTA and MCUboot bootloader

Enables wireless firmware updates via nRF Connect Device Manager.
MCUmgr SMP over BLE with authenticated access (bonding required).
MCUboot bootloader validates and swaps firmware images.
```

---

### Task 10: Update CLAUDE.md documentation

**Files:**
- Modify: `CLAUDE.md` (update architecture, add DFU section)

**Step 1: Update the module structure in CLAUDE.md**

In the `### Module Structure` section, add the DFU module:
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
├── board/
│   └── board_config.c/.h     # Hardware initialization, pin configuration
└── dfu/
    └── dfu_service.c/.h      # BLE FOTA via MCUmgr SMP, MCUboot integration
```

**Step 2: Add DFU section to CLAUDE.md**

Add a new section after `### Security Architecture`:
```markdown
### DFU / FOTA Updates

**Wireless Firmware Update** via MCUmgr SMP over BLE:
- MCUboot bootloader validates and swaps firmware images (dual-slot)
- MCUmgr SMP server runs over BLE transport
- Requires bonded connection (CONFIG_MCUMGR_TRANSPORT_BT_PERM_RW_AUTHEN=y)
- Use **nRF Connect Device Manager** mobile app to perform updates
- Build generates `dfu_application.zip` for OTA upload

**Update Flow**:
1. Build new firmware with `pio run`
2. Locate `dfu_application.zip` in build output
3. Transfer to phone, open nRF Connect Device Manager
4. Connect to RadPro-Link, upload new image
5. Device reboots, MCUboot swaps and validates new image
6. If new image fails, MCUboot automatically reverts
```

**Step 3: Update initialization sequence line references**

Review and update any line number references in CLAUDE.md that have shifted due to the changes.

**Step 4: Commit**

```
docs: update CLAUDE.md with DFU architecture and cleanup changes
```

---

## Execution Notes

**Build verification**: Every task ends with `pio run`. This is the only test available for embedded Zephyr firmware without hardware. The build itself validates type safety, symbol resolution, and Kconfig consistency.

**Task 9 is the riskiest**: MCUboot integration with PlatformIO's Seeed boards platform may need additional work. If it fails, the DFU module (Tasks 7-8) still compiles cleanly with `#ifdef CONFIG_MCUMGR` guards, so the cleanup tasks (1-6) are independent and safe.

**Task order matters**: Tasks 1-6 are independent cleanup tasks and can be done in any order. Tasks 7-9 depend on each other (module -> integration -> enable). Task 10 should be last.
