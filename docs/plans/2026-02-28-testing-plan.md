# RadPro-Link Unit Test Coverage — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add 42 unit tests across 6 modules covering all testable logic in RadPro-Link.

**Architecture:** Each test suite compiles on the Zephyr `unit_testing` pseudo-board via `find_package(Zephyr COMPONENTS unittest)`. All Zephyr subsystem APIs (BT, GPIO, UART) are mocked with FFF (Fake Function Framework). Each test file `#include`s its module `.c` directly to access static functions/state. Shared mock headers provide type stubs and block real Zephyr subsystem headers.

**Tech Stack:** Zephyr ztest, FFF, Twister, CMake, `unit_testing` pseudo-board

**Design doc:** `docs/plans/2026-02-28-testing-design.md`

---

## Conventions

- **Indentation:** C test files use tabs (width 8), matching `.editorconfig`
- **Test runner:** `make test` (Docker) or `make test-suite SUITE=<name>` for single suite
- **Docker image:** `ghcr.io/zephyrproject-rtos/ci:v0.28.8` with persistent `zephyr-workspace` volume
- **Commit style:** `test: <description>` per Conventional Commits

## Implementation-Verified Patterns

These patterns were discovered during Task 2 (security_manager) implementation and **MUST** be applied to all remaining tasks:

### 1. FFF requires DECLARE + DEFINE (Zephyr's FFF version)
```c
DECLARE_FAKE_VALUE_FUNC(int, some_func, int);
DEFINE_FAKE_VALUE_FUNC(int, some_func, int);
```
Every `DEFINE_FAKE_*` must be preceded by matching `DECLARE_FAKE_*`. Omitting DECLARE causes `unknown type name 'some_func_Fake'` errors.

### 2. Block Zephyr logging headers
The CUT re-includes `<zephyr/logging/log.h>` which overrides LOG stubs. Add these **before** mock headers:
```c
#define ZEPHYR_INCLUDE_LOGGING_LOG_H_
#define ZEPHYR_INCLUDE_LOGGING_LOG_CORE_H_
```

### 3. Zero-arg inline kernel functions need macro redirect
`k_uptime_get()` is `static inline` in kernel.h (included by ztest). FFF can't fake it. Use:
```c
static int64_t k_uptime_get_fake_return_val;
static int k_uptime_get_fake_call_count;
static int64_t test_k_uptime_get(void) {
    k_uptime_get_fake_call_count++;
    return k_uptime_get_fake_return_val;
}
#define k_uptime_get() test_k_uptime_get()
```
Same pattern applies to other `static inline` kernel functions like `k_msleep`.
For `k_msleep` specifically, add a `custom_fake` function pointer to support the `setjmp`/`longjmp` thread escape:
```c
static int32_t k_msleep_fake_return_val;
static int k_msleep_fake_call_count;
static int32_t (*k_msleep_custom_fake)(int32_t);
static int32_t test_k_msleep(int32_t ms)
{
	k_msleep_fake_call_count++;
	if (k_msleep_custom_fake) {
		return k_msleep_custom_fake(ms);
	}
	return k_msleep_fake_return_val;
}
#define k_msleep(ms) test_k_msleep(ms)
```
**IMPORTANT:** `k_msleep` is `static inline` in Zephyr 4.2 — using `#undef` + FFF `DEFINE_FAKE` will NOT work. Must use macro redirect.

### 7. Zero-arg FFF functions have type conflicts
Zephyr's FFF has a bug where `DECLARE_FAKE_VALUE_FUNC(int, func_name)` (zero args) and `DEFINE_FAKE_VALUE_FUNC(int, func_name)` produce conflicting type definitions for the `_Fake` struct. DEFINE-only also fails (`unknown type name`). Use manual fakes instead:
```c
#define MANUAL_FAKE_VALUE_FUNC0(ret_type, fname) \
	static struct { ret_type return_val; int call_count; } fname##_fake; \
	ret_type fname(void) { fname##_fake.call_count++; return fname##_fake.return_val; }
#define RESET_MANUAL_FAKE(fname) memset(&fname##_fake, 0, sizeof(fname##_fake))
```

### 8. k_malloc/k_free are provided by the unit test kernel
Cannot use FFF fakes (causes `multiple definition` linker errors). Use macro redirect:
```c
static void *k_malloc_fake_return_val;
static int k_malloc_fake_call_count;
static void *(*k_malloc_fake_custom_fake)(size_t);
static void *test_k_malloc(size_t size) { ... }
#define k_malloc(size) test_k_malloc(size)
```

### 9. k_fifo_put/k_fifo_get are macros in kernel.h
Must `#undef` before creating FFF fakes:
```c
#undef k_fifo_put
DECLARE_FAKE_VOID_FUNC(k_fifo_put, struct k_fifo *, void *);
DEFINE_FAKE_VOID_FUNC(k_fifo_put, struct k_fifo *, void *);
```

### 10. K_FIFO_DEFINE must not include `static`
The CUT uses `static K_FIFO_DEFINE(name)`, so our stub must NOT add `static`:
```c
#define K_FIFO_DEFINE(name) struct k_fifo name
```

### 11. Test file must not be named main.c when testing main.c
`#include "main.c"` resolves to the test file itself (self-inclusion loop).
Rename the test file to `test_main.c` and update CMakeLists.txt. Also use `#define main radpro_main` to avoid conflict with ztest's main().

### 4. Include path is `../../src` (not `../../../src`)
`tests/<suite>/CMakeLists.txt` is 2 levels deep from project root:
```cmake
target_include_directories(testbinary PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../mocks
)
```

### 5. Each test suite needs an empty `prj.conf`
Zephyr's cmake requires it even for unit tests. Create `tests/<suite>/prj.conf` (empty file).

### 6. Test execution via Docker
```bash
make test-suite SUITE=security_manager  # single suite
make test                                # all suites
make test-clean                          # remove build dirs
```

## Overview

| Task | Suite | Tests | Status |
|------|-------|-------|--------|
| 1 | Mock infrastructure | — | DONE |
| 2 | security_manager | 8 | 8/8 PASS |
| 3 | board_config | 4 | 4/4 PASS |
| 4 | led_status | 7 | 7/7 PASS |
| 5 | ble_service | 10 | 10/10 PASS |
| 6 | uart_bridge | 8 | 8/8 PASS |
| 7 | main_flow | 5 | 5/5 PASS |
| 8 | Runner script + CI | — | DONE (Makefile + docker-compose) |

---

### Task 1: Shared Mock Infrastructure ✅ DONE

**Files:** Created in `tests/mocks/` — `bt_mocks.h`, `gpio_mocks.h`, `uart_mocks.h`, `kernel_mocks.h`

These headers provide type definitions that replace real Zephyr subsystem headers. They block the real headers via include-guard defines, then provide minimal struct/enum/macro stubs matching what the production code uses. No FFF declarations — each test file defines its own fakes.

**Step 1: Create `tests/mocks/bt_mocks.h`**

```c
/*
 * SPDX-License-Identifier: MIT
 * Bluetooth API type stubs for unit testing.
 */

#ifndef BT_MOCKS_H
#define BT_MOCKS_H

/* Block real Zephyr BT headers — CUT #includes become no-ops */
#define ZEPHYR_INCLUDE_BLUETOOTH_BLUETOOTH_H_
#define ZEPHYR_INCLUDE_BLUETOOTH_CONN_H_
#define ZEPHYR_INCLUDE_BLUETOOTH_UUID_H_
#define ZEPHYR_INCLUDE_BLUETOOTH_GATT_H_
#define ZEPHYR_INCLUDE_BLUETOOTH_ADDR_H_
#define ZEPHYR_INCLUDE_BLUETOOTH_GAP_H_
#define ZEPHYR_INCLUDE_BLUETOOTH_HCI_H_
#define ZEPHYR_INCLUDE_BLUETOOTH_HCI_TYPES_H_
#define ZEPHYR_INCLUDE_BLUETOOTH_SERVICES_NUS_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* --- Address types --- */
typedef struct { uint8_t val[6]; } bt_addr_t;
typedef struct { uint8_t type; bt_addr_t a; } bt_addr_le_t;
#define BT_ADDR_LE_STR_LEN 30

/* --- Connection types --- */
struct bt_conn { int _dummy; };

typedef uint8_t bt_security_t;
#define BT_SECURITY_L0 0
#define BT_SECURITY_L1 1
#define BT_SECURITY_L2 2
#define BT_SECURITY_L3 3
#define BT_SECURITY_L4 4

enum bt_security_err {
	BT_SECURITY_ERR_SUCCESS = 0,
	BT_SECURITY_ERR_AUTH_FAIL = 1,
};

/* --- Auth callback types --- */
struct bt_conn_oob_info { int type; };

struct bt_conn_auth_cb {
	void (*passkey_display)(struct bt_conn *conn, unsigned int passkey);
	void (*passkey_confirm)(struct bt_conn *conn, unsigned int passkey);
	void (*passkey_entry)(struct bt_conn *conn);
	void (*cancel)(struct bt_conn *conn);
	void (*pairing_confirm)(struct bt_conn *conn);
	void (*oob_data_request)(struct bt_conn *conn,
				 struct bt_conn_oob_info *info);
};

struct bt_conn_auth_info_cb {
	void (*pairing_complete)(struct bt_conn *conn, bool bonded);
	void (*pairing_failed)(struct bt_conn *conn,
			       enum bt_security_err reason);
	struct bt_conn_auth_info_cb *_next;
};

/* --- Connection callback types --- */
struct bt_le_conn_param {
	uint16_t interval_min;
	uint16_t interval_max;
	uint16_t latency;
	uint16_t timeout;
};

struct bt_conn_le_data_len_info {
	uint16_t tx_max_len;
	uint16_t rx_max_len;
	uint16_t tx_max_time;
	uint16_t rx_max_time;
};

struct bt_conn_cb {
	void (*connected)(struct bt_conn *conn, uint8_t err);
	void (*disconnected)(struct bt_conn *conn, uint8_t reason);
	void (*recycled)(void);
	void (*security_changed)(struct bt_conn *conn, bt_security_t level,
				 enum bt_security_err err);
	bool (*le_param_req)(struct bt_conn *conn,
			     struct bt_le_conn_param *param);
	void (*le_param_updated)(struct bt_conn *conn, uint16_t interval,
				 uint16_t latency, uint16_t timeout);
	void (*le_data_len_updated)(struct bt_conn *conn,
				    struct bt_conn_le_data_len_info *info);
};

#define BT_CONN_CB_DEFINE(name) \
	static const struct bt_conn_cb name

/* --- GATT types --- */
struct bt_gatt_cb {
	void (*att_mtu_updated)(struct bt_conn *conn, uint16_t tx,
				uint16_t rx);
};

/* --- NUS types --- */
struct bt_nus_cb {
	void (*received)(struct bt_conn *conn, const void *data,
			 uint16_t len, void *ctx);
};

/* --- Advertising types --- */
struct bt_data {
	uint8_t type;
	uint8_t data_len;
	const uint8_t *data;
};

struct bt_le_adv_param {
	uint8_t id;
	uint8_t options;
	uint16_t interval_min;
	uint16_t interval_max;
};

#define BT_DATA_FLAGS        0x01
#define BT_DATA_NAME_COMPLETE 0x09
#define BT_DATA_UUID128_ALL  0x07

#define BT_LE_AD_GENERAL     0x02
#define BT_LE_AD_NO_BREDR    0x04

#define BT_DATA_BYTES(_type, ...) \
	{ .type = (_type), \
	  .data = (const uint8_t []){ __VA_ARGS__ }, \
	  .data_len = sizeof((const uint8_t []){ __VA_ARGS__ }) }

#define BT_DATA(_type, _data, _data_len) \
	{ .type = (_type), \
	  .data = (const uint8_t *)(_data), \
	  .data_len = (_data_len) }

#define BT_LE_ADV_CONN_FAST_2 \
	((struct bt_le_adv_param[]){ { 0 } })

/* NUS service UUID (16 bytes) */
#define BT_UUID_NUS_SRV_VAL \
	0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, \
	0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E

typedef void (*bt_ready_cb_t)(int err);

#endif /* BT_MOCKS_H */
```

**Step 2: Create `tests/mocks/gpio_mocks.h`**

```c
/*
 * SPDX-License-Identifier: MIT
 * GPIO API type stubs for unit testing.
 */

#ifndef GPIO_MOCKS_H
#define GPIO_MOCKS_H

/* Block real Zephyr GPIO header */
#define ZEPHYR_INCLUDE_DRIVERS_GPIO_H_

#include <stdint.h>
#include <stdbool.h>

struct device { const char *name; };

typedef uint32_t gpio_flags_t;
#define GPIO_OUTPUT_INACTIVE (1 << 2)

struct gpio_dt_spec {
	const struct device *port;
	uint8_t pin;
	gpio_flags_t dt_flags;
};

/* Stub DT macros — resolved before CUT is included */
#define DT_ALIAS(alias) 0
#define GPIO_DT_SPEC_GET(node_id, prop) \
	{ .port = NULL, .pin = 0, .dt_flags = 0 }

#endif /* GPIO_MOCKS_H */
```

**Step 3: Create `tests/mocks/uart_mocks.h`**

```c
/*
 * SPDX-License-Identifier: MIT
 * UART API type stubs for unit testing.
 */

#ifndef UART_MOCKS_H
#define UART_MOCKS_H

/* Block real Zephyr UART/device headers */
#define ZEPHYR_INCLUDE_DRIVERS_UART_H_
#define ZEPHYR_INCLUDE_DEVICE_H_
#define ZEPHYR_INCLUDE_DEVICETREE_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Device type (also used by GPIO) */
#ifndef GPIO_MOCKS_H
struct device { const char *name; };
#endif

/* UART event types */
enum uart_event_type {
	UART_TX_DONE,
	UART_TX_ABORTED,
	UART_RX_RDY,
	UART_RX_BUF_REQUEST,
	UART_RX_BUF_RELEASED,
	UART_RX_DISABLED,
	UART_RX_STOPPED,
};

struct uart_event_tx {
	const uint8_t *buf;
	size_t len;
};

struct uart_event_rx {
	uint8_t *buf;
	size_t offset;
	size_t len;
};

struct uart_event_rx_buf {
	uint8_t *buf;
};

struct uart_event {
	enum uart_event_type type;
	union {
		struct uart_event_tx tx;
		struct uart_event_rx rx;
		struct uart_event_rx_buf rx_buf;
	} data;
};

typedef void (*uart_callback_t)(const struct device *dev,
				struct uart_event *evt,
				void *user_data);

#define SYS_FOREVER_MS (-1)

/* DT macros for UART device selection */
#define DT_HAS_CHOSEN(x) 1
#define DT_CHOSEN(x) 0
#define DT_NODELABEL(x) 0
#define DEVICE_DT_GET(node) (&test_uart_device)

#endif /* UART_MOCKS_H */
```

**Step 4: Create `tests/mocks/kernel_mocks.h`**

```c
/*
 * SPDX-License-Identifier: MIT
 * Kernel/settings/USB function stubs for unit testing.
 * Type definitions come from <zephyr/kernel.h> via ztest.
 * This header only blocks non-kernel subsystem headers.
 */

#ifndef KERNEL_MOCKS_H
#define KERNEL_MOCKS_H

/* Block settings and USB headers */
#define ZEPHYR_INCLUDE_SETTINGS_SETTINGS_H_
#define ZEPHYR_INCLUDE_USB_USB_DEVICE_H_

#endif /* KERNEL_MOCKS_H */
```

**Step 5: Commit mock headers**

```bash
git add tests/mocks/
git commit -m "test: add shared FFF mock headers for BT, GPIO, UART, kernel"
```

---

### Task 2: security_manager Tests (8 tests) ✅ 8/8 PASS

> **Implementation notes:** This was the first suite implemented. The working test file is the source of truth at `tests/security_manager/src/main.c`. The code below is the ORIGINAL plan — see the actual committed file for the corrected version with DECLARE+DEFINE, log header blocking, and k_uptime_get macro redirect.

**Files:**
- Create: `tests/security_manager/CMakeLists.txt`
- Create: `tests/security_manager/testcase.yaml`
- Create: `tests/security_manager/src/main.c`

**Step 1: Create build files**

`tests/security_manager/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_security_manager)

target_sources(testbinary PRIVATE src/main.c)
target_include_directories(testbinary PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../mocks
)
```

`tests/security_manager/testcase.yaml`:
```yaml
tests:
  radpro_link.security_manager:
    tags: unit
    type: unit
```

**Step 2: Write test file**

`tests/security_manager/src/main.c`:
```c
/*
 * SPDX-License-Identifier: MIT
 * Unit tests for security_manager module.
 */

#include <zephyr/ztest.h>
#include <zephyr/fff.h>

DEFINE_FFF_GLOBALS;

/* Stub logging before including CUT */
#ifdef LOG_MODULE_REGISTER
#undef LOG_MODULE_REGISTER
#endif
#define LOG_MODULE_REGISTER(...)

#ifdef LOG_INF
#undef LOG_INF
#endif
#define LOG_INF(...)

#ifdef LOG_WRN
#undef LOG_WRN
#endif
#define LOG_WRN(...)

#ifdef LOG_ERR
#undef LOG_ERR
#endif
#define LOG_ERR(...)

/* BT type stubs */
#include "bt_mocks.h"

/* FFF fakes for BT functions used by security_manager.c */
DEFINE_FAKE_VALUE_FUNC(int, bt_conn_auth_cb_register, struct bt_conn_auth_cb *);
DEFINE_FAKE_VALUE_FUNC(int, bt_conn_auth_info_cb_register,
		       struct bt_conn_auth_info_cb *);
DEFINE_FAKE_VOID_FUNC(bt_conn_auth_cancel, struct bt_conn *);
DEFINE_FAKE_VOID_FUNC(bt_conn_auth_passkey_confirm, struct bt_conn *);
DEFINE_FAKE_VOID_FUNC(bt_conn_auth_pairing_confirm, struct bt_conn *);
DEFINE_FAKE_VALUE_FUNC(const bt_addr_le_t *, bt_conn_get_dst,
		       const struct bt_conn *);
DEFINE_FAKE_VOID_FUNC(bt_addr_le_to_str, const bt_addr_le_t *, char *,
		       size_t);
DEFINE_FAKE_VALUE_FUNC(const char *, bt_security_err_to_str,
		       enum bt_security_err);

/* FFF fakes for kernel functions */
DEFINE_FAKE_VOID_FUNC(k_work_init_delayable, struct k_work_delayable *,
		      k_work_handler_t);
DEFINE_FAKE_VALUE_FUNC(int, k_work_schedule, struct k_work_delayable *,
		       k_timeout_t);
DEFINE_FAKE_VALUE_FUNC(int64_t, k_uptime_get);

/* Test-controlled state */
static bt_addr_le_t test_addr;
static struct bt_conn test_conn;

/* Include CUT — its Zephyr #includes are blocked/stubbed */
#include "security/security_manager.c"

/* --- FFF reset rule --- */
static void fff_reset_rule_before(const struct ztest_unit_test *test,
				  void *fixture)
{
	RESET_FAKE(bt_conn_auth_cb_register);
	RESET_FAKE(bt_conn_auth_info_cb_register);
	RESET_FAKE(bt_conn_auth_cancel);
	RESET_FAKE(bt_conn_auth_passkey_confirm);
	RESET_FAKE(bt_conn_auth_pairing_confirm);
	RESET_FAKE(bt_conn_get_dst);
	RESET_FAKE(bt_addr_le_to_str);
	RESET_FAKE(bt_security_err_to_str);
	RESET_FAKE(k_work_init_delayable);
	RESET_FAKE(k_work_schedule);
	RESET_FAKE(k_uptime_get);
	FFF_RESET_HISTORY();

	/* Reset module state */
	pairing_allowed = true;
	pairing_window_ms = 0;
	pairing_window_end_time = 0;

	/* Default: bt_conn_get_dst returns valid address */
	bt_conn_get_dst_fake.return_val = &test_addr;
}

ZTEST_RULE(fff_reset_rule, fff_reset_rule_before, NULL);

/* --- Tests --- */

ZTEST(security_manager, test_init_schedules_timeout)
{
	k_uptime_get_fake.return_val = 0;

	int err = security_manager_init(60000);

	zassert_equal(err, 0);
	zassert_equal(k_work_init_delayable_fake.call_count, 1);
	zassert_equal(k_work_schedule_fake.call_count, 1);
}

ZTEST(security_manager, test_pairing_allowed_during_window)
{
	k_uptime_get_fake.return_val = 0;
	security_manager_init(60000);

	zassert_true(security_manager_is_pairing_allowed());
}

ZTEST(security_manager, test_pairing_denied_after_window)
{
	k_uptime_get_fake.return_val = 0;
	security_manager_init(60000);

	/* Simulate timeout handler firing */
	pairing_timeout_handler(NULL);

	zassert_false(security_manager_is_pairing_allowed());
}

ZTEST(security_manager, test_time_remaining_calculation)
{
	/* Init at uptime=1000 with 60s window → end_time=61000 */
	k_uptime_get_fake.return_val = 1000;
	security_manager_init(60000);

	/* 30s later → 31000 remaining */
	k_uptime_get_fake.return_val = 31000;
	uint32_t remaining = security_manager_get_pairing_time_remaining();

	zassert_equal(remaining, 30000);
}

ZTEST(security_manager, test_time_remaining_after_expiry)
{
	k_uptime_get_fake.return_val = 0;
	security_manager_init(60000);

	/* Timeout fires → pairing_allowed = false */
	pairing_timeout_handler(NULL);

	uint32_t remaining = security_manager_get_pairing_time_remaining();

	zassert_equal(remaining, 0);
}

ZTEST(security_manager, test_passkey_confirm_during_window)
{
	k_uptime_get_fake.return_val = 0;
	security_manager_init(60000);

	/* pairing_allowed is true → should confirm */
	auth_passkey_confirm(&test_conn, 123456);

	zassert_equal(bt_conn_auth_passkey_confirm_fake.call_count, 1);
	zassert_equal(bt_conn_auth_cancel_fake.call_count, 0);
}

ZTEST(security_manager, test_passkey_confirm_after_window)
{
	k_uptime_get_fake.return_val = 0;
	security_manager_init(60000);
	pairing_timeout_handler(NULL);

	auth_passkey_confirm(&test_conn, 123456);

	zassert_equal(bt_conn_auth_cancel_fake.call_count, 1);
	zassert_equal(bt_conn_auth_passkey_confirm_fake.call_count, 0);
}

ZTEST(security_manager, test_oob_rejected)
{
	k_uptime_get_fake.return_val = 0;
	security_manager_init(60000);

	struct bt_conn_oob_info info = { 0 };

	/* OOB always rejected, even during pairing window */
	auth_oob_data_request(&test_conn, &info);

	zassert_equal(bt_conn_auth_cancel_fake.call_count, 1);
}

ZTEST_SUITE(security_manager, NULL, NULL, NULL, NULL, NULL);
```

**Step 3: Build and run**

```bash
export ZEPHYR_BASE=~/.platformio/packages/framework-zephyr
$ZEPHYR_BASE/scripts/twister -p unit_testing -T tests/security_manager/ --inline-logs
```

Expected: 8 tests PASS.

**Step 4: Commit**

```bash
git add tests/security_manager/
git commit -m "test: add security_manager unit tests (8 tests)"
```

---

### Task 3: board_config Tests (4 tests)

**Files:**
- Create: `tests/board_config/CMakeLists.txt`
- Create: `tests/board_config/testcase.yaml`
- Create: `tests/board_config/src/main.c`

**Step 1: Create build files**

`tests/board_config/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_board_config)

target_sources(testbinary PRIVATE src/main.c)
target_include_directories(testbinary PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../mocks
)
```

Also create empty `tests/board_config/prj.conf`.

`tests/board_config/testcase.yaml`:
```yaml
tests:
  radpro_link.board_config:
    tags: unit
    type: unit
```

**Step 2: Write test file**

`tests/board_config/src/main.c`:
```c
/*
 * SPDX-License-Identifier: MIT
 * Unit tests for board_config module.
 */

#include <zephyr/ztest.h>
#include <zephyr/fff.h>

DEFINE_FFF_GLOBALS;

/* Stub logging */
#ifdef LOG_MODULE_REGISTER
#undef LOG_MODULE_REGISTER
#endif
#define LOG_MODULE_REGISTER(...)

#ifdef LOG_INF
#undef LOG_INF
#endif
#define LOG_INF(...)

#ifdef LOG_WRN
#undef LOG_WRN
#endif
#define LOG_WRN(...)

#ifdef LOG_ERR
#undef LOG_ERR
#endif
#define LOG_ERR(...)

/* Block logging and USB headers */
#define ZEPHYR_INCLUDE_LOGGING_LOG_H_
#define ZEPHYR_INCLUDE_LOGGING_LOG_CORE_H_
#include "kernel_mocks.h"

/* Select nRF54L15 board config for testing */
#define CONFIG_BOARD_XIAO_NRF54L15 1

/* IS_ENABLED mock — CONFIG_USB_DEVICE_STACK is NOT defined,
 * so IS_ENABLED returns 0. IS_ENABLED should be available
 * from kernel.h. If not, provide stub: */
#ifndef IS_ENABLED
#define IS_ENABLED(config) 0
#endif

/* FFF fake for usb_enable (won't be called for nRF54L15) */
DECLARE_FAKE_VALUE_FUNC(int, usb_enable, void *);
DEFINE_FAKE_VALUE_FUNC(int, usb_enable, void *);

/* FFF fake for k_sleep — static inline in kernel.h, use macro redirect */
static int32_t k_sleep_fake_return_val;
static int k_sleep_fake_call_count;
static int32_t test_k_sleep(k_timeout_t timeout) {
	k_sleep_fake_call_count++;
	return k_sleep_fake_return_val;
}
#define k_sleep(t) test_k_sleep(t)

/* Include CUT */
#include "board/board_config.c"

/* --- FFF reset rule --- */
static void fff_reset_rule_before(const struct ztest_unit_test *test,
				  void *fixture)
{
	RESET_FAKE(usb_enable);
	k_sleep_fake_return_val = 0;
	k_sleep_fake_call_count = 0;
	FFF_RESET_HISTORY();
}

ZTEST_RULE(fff_reset_rule, fff_reset_rule_before, NULL);

/* --- Tests --- */

ZTEST(board_config, test_config_no_usb)
{
	const struct board_config *cfg = board_get_config();

	zassert_false(cfg->has_usb);
}

ZTEST(board_config, test_config_no_async_adapter)
{
	const struct board_config *cfg = board_get_config();

	zassert_false(cfg->needs_async_adapter);
}

ZTEST(board_config, test_board_name_set)
{
	const struct board_config *cfg = board_get_config();

	zassert_not_null(cfg->name);
	zassert_true(strlen(cfg->name) > 0);
}

ZTEST(board_config, test_init_skips_usb_when_no_usb)
{
	int err = board_init();

	zassert_equal(err, 0);
	zassert_equal(usb_enable_fake.call_count, 0,
		      "usb_enable should not be called for nRF54L15");
}

ZTEST_SUITE(board_config, NULL, NULL, NULL, NULL, NULL);
```

**Step 3: Build and run**

```bash
make test-suite SUITE=board_config
```

Expected: 4 tests PASS.

**Step 4: Commit**

```bash
git add tests/board_config/
git commit -m "test: add board_config unit tests (4 tests)"
```

---

### Task 4: led_status Tests (7 tests)

> **Apply patterns:** DECLARE+DEFINE for all fakes, block log headers, prj.conf, macro redirect for k_msleep

**Files:**
- Create: `tests/led_status/CMakeLists.txt`
- Create: `tests/led_status/testcase.yaml`
- Create: `tests/led_status/src/main.c`

**Step 1: Create build files**

`tests/led_status/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_led_status)

target_sources(testbinary PRIVATE src/main.c)
target_include_directories(testbinary PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../mocks
)
```

`tests/led_status/testcase.yaml`:
```yaml
tests:
  radpro_link.led_status:
    tags: unit
    type: unit
```

**Step 2: Write test file**

Uses `setjmp`/`longjmp` to test the infinite-loop thread function by intercepting `k_msleep` calls.

`tests/led_status/src/main.c`:
```c
/*
 * SPDX-License-Identifier: MIT
 * Unit tests for led_status module.
 */

#include <zephyr/ztest.h>
#include <zephyr/fff.h>
#include <setjmp.h>

DEFINE_FFF_GLOBALS;

/* Stub logging */
#ifdef LOG_MODULE_REGISTER
#undef LOG_MODULE_REGISTER
#endif
#define LOG_MODULE_REGISTER(...)

#ifdef LOG_INF
#undef LOG_INF
#endif
#define LOG_INF(...)

#ifdef LOG_WRN
#undef LOG_WRN
#endif
#define LOG_WRN(...)

#ifdef LOG_ERR
#undef LOG_ERR
#endif
#define LOG_ERR(...)

/* GPIO type stubs */
#include "gpio_mocks.h"

/* FFF fakes for GPIO functions */
DEFINE_FAKE_VALUE_FUNC(bool, gpio_is_ready_dt, const struct gpio_dt_spec *);
DEFINE_FAKE_VALUE_FUNC(int, gpio_pin_configure_dt,
		       const struct gpio_dt_spec *, gpio_flags_t);
DEFINE_FAKE_VALUE_FUNC(int, gpio_pin_set_dt,
		       const struct gpio_dt_spec *, int);

/* Override k_msleep for thread testing.
 * k_msleep may be an inline wrapper around k_sleep in Zephyr.
 * We override it with a macro to intercept calls. */
#ifdef k_msleep
#undef k_msleep
#endif
DEFINE_FAKE_VALUE_FUNC(int32_t, k_msleep, int32_t);

/* Stub K_THREAD_DEFINE — don't actually create threads in test */
#ifdef K_THREAD_DEFINE
#undef K_THREAD_DEFINE
#endif
#define K_THREAD_DEFINE(name, stack, entry, p1, p2, p3, prio, opts, delay)

/* Include CUT */
#include "led/led_status.c"

/* --- Thread escape mechanism --- */
static jmp_buf test_jmp;
static int32_t captured_sleep_ms;

static int32_t k_msleep_longjmp(int32_t ms)
{
	captured_sleep_ms = ms;
	longjmp(test_jmp, 1);
	return 0; /* unreachable */
}

/* --- FFF reset rule --- */
static void fff_reset_rule_before(const struct ztest_unit_test *test,
				  void *fixture)
{
	RESET_FAKE(gpio_is_ready_dt);
	RESET_FAKE(gpio_pin_configure_dt);
	RESET_FAKE(gpio_pin_set_dt);
	RESET_FAKE(k_msleep);
	FFF_RESET_HISTORY();

	/* Reset module state */
	is_connected = false;
	pairing_window_active = true;
	error_mode = false;
	captured_sleep_ms = 0;

	/* Defaults */
	gpio_is_ready_dt_fake.return_val = true;
	gpio_pin_configure_dt_fake.return_val = 0;
	gpio_pin_set_dt_fake.return_val = 0;
}

ZTEST_RULE(fff_reset_rule, fff_reset_rule_before, NULL);

/* --- Tests --- */

ZTEST(led_status, test_init_configures_gpio)
{
	int err = led_status_init();

	zassert_equal(err, 0);
	zassert_equal(gpio_pin_configure_dt_fake.call_count, 1);
	zassert_equal(gpio_pin_configure_dt_fake.arg1_val,
		      GPIO_OUTPUT_INACTIVE);
}

ZTEST(led_status, test_init_fails_no_device)
{
	gpio_is_ready_dt_fake.return_val = false;

	int err = led_status_init();

	zassert_equal(err, -ENODEV);
}

ZTEST(led_status, test_set_connected_flag)
{
	led_status_set_connected(true);

	zassert_true(is_connected);
}

ZTEST(led_status, test_set_pairing_window_flag)
{
	led_status_set_pairing_window(false);

	zassert_false(pairing_window_active);
}

ZTEST(led_status, test_error_sets_error_mode)
{
	led_status_error();

	zassert_true(error_mode);
}

ZTEST(led_status, test_led_off_when_pairing_closed)
{
	pairing_window_active = false;
	error_mode = false;

	k_msleep_fake.custom_fake = k_msleep_longjmp;

	if (setjmp(test_jmp) == 0) {
		led_status_thread();
	}

	/* LED should be turned off */
	zassert_equal(gpio_pin_set_dt_fake.arg1_val, 0,
		      "LED should be off when pairing closed");
	zassert_equal(captured_sleep_ms, 1000,
		      "Should use SLOW_BLINK_INTERVAL_MS");
}

ZTEST(led_status, test_blink_interval_pairing_connected)
{
	pairing_window_active = true;
	is_connected = true;
	error_mode = false;

	k_msleep_fake.custom_fake = k_msleep_longjmp;

	if (setjmp(test_jmp) == 0) {
		led_status_thread();
	}

	zassert_equal(captured_sleep_ms, 500,
		      "Should use MEDIUM_BLINK_INTERVAL_MS (500ms)");
}

ZTEST_SUITE(led_status, NULL, NULL, NULL, NULL, NULL);
```

**Step 3: Build and run**

```bash
make test-suite SUITE=led_status
```

Expected: 7 tests PASS.

**Step 4: Commit**

```bash
git add tests/led_status/
git commit -m "test: add led_status unit tests (7 tests)"
```

---

### Task 5: ble_service Tests (10 tests)

> **Apply patterns:** DECLARE+DEFINE for all fakes, block log headers, prj.conf. Also add `LOG_DBG` stub.

**Files:**
- Create: `tests/ble_service/CMakeLists.txt`
- Create: `tests/ble_service/testcase.yaml`
- Create: `tests/ble_service/src/main.c`

**Step 1: Create build files**

`tests/ble_service/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_ble_service)

target_sources(testbinary PRIVATE src/main.c)
target_include_directories(testbinary PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../mocks
)
```

`tests/ble_service/testcase.yaml`:
```yaml
tests:
  radpro_link.ble_service:
    tags: unit
    type: unit
```

**Step 2: Write test file**

`tests/ble_service/src/main.c`:
```c
/*
 * SPDX-License-Identifier: MIT
 * Unit tests for ble_service module.
 */

#include <zephyr/ztest.h>
#include <zephyr/fff.h>

DEFINE_FFF_GLOBALS;

/* Stub logging */
#ifdef LOG_MODULE_REGISTER
#undef LOG_MODULE_REGISTER
#endif
#define LOG_MODULE_REGISTER(...)

#ifdef LOG_INF
#undef LOG_INF
#endif
#define LOG_INF(...)

#ifdef LOG_WRN
#undef LOG_WRN
#endif
#define LOG_WRN(...)

#ifdef LOG_ERR
#undef LOG_ERR
#endif
#define LOG_ERR(...)

#ifdef LOG_DBG
#undef LOG_DBG
#endif
#define LOG_DBG(...)

/* BT type stubs */
#include "bt_mocks.h"

/* ble_service.c uses CONFIG_BT_DEVICE_NAME */
#define CONFIG_BT_DEVICE_NAME "TestDevice"

/* ble_service.c uses CONFIG_BT_USER_DATA_LEN_UPDATE — leave undefined */

/* Block security_manager.h BT include (already blocked by bt_mocks.h).
 * Provide stubs for security_manager functions if ble_service uses them. */

/* FFF fakes — BT connection functions */
DEFINE_FAKE_VALUE_FUNC(struct bt_conn *, bt_conn_ref, struct bt_conn *);
DEFINE_FAKE_VOID_FUNC(bt_conn_unref, struct bt_conn *);
DEFINE_FAKE_VALUE_FUNC(bt_security_t, bt_conn_get_security,
		       struct bt_conn *);
DEFINE_FAKE_VALUE_FUNC(const bt_addr_le_t *, bt_conn_get_dst,
		       const struct bt_conn *);
DEFINE_FAKE_VOID_FUNC(bt_addr_le_to_str, const bt_addr_le_t *, char *,
		       size_t);
DEFINE_FAKE_VALUE_FUNC(const char *, bt_security_err_to_str,
		       enum bt_security_err);

/* FFF fakes — GATT/NUS */
DEFINE_FAKE_VALUE_FUNC(uint16_t, bt_gatt_get_mtu, struct bt_conn *);
DEFINE_FAKE_VOID_FUNC(bt_gatt_cb_register, struct bt_gatt_cb *);
DEFINE_FAKE_VALUE_FUNC(int, bt_nus_cb_register, struct bt_nus_cb *,
		       void *);
DEFINE_FAKE_VALUE_FUNC(int, bt_nus_send, struct bt_conn *,
		       const uint8_t *, uint16_t);

/* FFF fakes — advertising */
DEFINE_FAKE_VALUE_FUNC(int, bt_le_adv_start, const struct bt_le_adv_param *,
		       const struct bt_data *, size_t,
		       const struct bt_data *, size_t);

/* FFF fakes — kernel work */
DEFINE_FAKE_VOID_FUNC(k_work_init, struct k_work *, k_work_handler_t);
DEFINE_FAKE_VALUE_FUNC(int, k_work_submit, struct k_work *);

/* Test state */
static struct bt_conn test_conn;
static bt_addr_le_t test_addr;
static bool test_data_received;
static const uint8_t *test_data_ptr;
static uint16_t test_data_len;

static void test_data_cb(struct bt_conn *conn, const uint8_t *data,
			 uint16_t len)
{
	test_data_received = true;
	test_data_ptr = data;
	test_data_len = len;
}

/* bt_conn_ref returns the conn passed in */
static struct bt_conn *bt_conn_ref_passthrough(struct bt_conn *conn)
{
	return conn;
}

/* Include CUT */
#include "ble/ble_service.c"

/* --- FFF reset rule --- */
static void fff_reset_rule_before(const struct ztest_unit_test *test,
				  void *fixture)
{
	RESET_FAKE(bt_conn_ref);
	RESET_FAKE(bt_conn_unref);
	RESET_FAKE(bt_conn_get_security);
	RESET_FAKE(bt_conn_get_dst);
	RESET_FAKE(bt_addr_le_to_str);
	RESET_FAKE(bt_security_err_to_str);
	RESET_FAKE(bt_gatt_get_mtu);
	RESET_FAKE(bt_gatt_cb_register);
	RESET_FAKE(bt_nus_cb_register);
	RESET_FAKE(bt_nus_send);
	RESET_FAKE(bt_le_adv_start);
	RESET_FAKE(k_work_init);
	RESET_FAKE(k_work_submit);
	FFF_RESET_HISTORY();

	/* Reset module state */
	current_conn = NULL;
	current_mtu = 23;
	data_received_callback = NULL;

	/* Defaults */
	bt_conn_ref_fake.custom_fake = bt_conn_ref_passthrough;
	bt_conn_get_dst_fake.return_val = &test_addr;
	bt_conn_get_security_fake.return_val = BT_SECURITY_L2;

	test_data_received = false;
	test_data_ptr = NULL;
	test_data_len = 0;
}

ZTEST_RULE(fff_reset_rule, fff_reset_rule_before, NULL);

/* --- Tests --- */

ZTEST(ble_service, test_init_registers_nus)
{
	int err = ble_service_init(test_data_cb);

	zassert_equal(err, 0);
	zassert_equal(bt_nus_cb_register_fake.call_count, 1);
	zassert_equal(bt_gatt_cb_register_fake.call_count, 1);
	zassert_equal(k_work_init_fake.call_count, 1);
}

ZTEST(ble_service, test_send_no_connection_fails)
{
	current_conn = NULL;

	int err = ble_service_send((const uint8_t *)"hi", 2);

	zassert_equal(err, -ENOTCONN);
	zassert_equal(bt_nus_send_fake.call_count, 0);
}

ZTEST(ble_service, test_send_not_authenticated_fails)
{
	current_conn = &test_conn;
	bt_conn_get_security_fake.return_val = BT_SECURITY_L1;

	int err = ble_service_send((const uint8_t *)"hi", 2);

	zassert_equal(err, -ENOTCONN);
	zassert_equal(bt_nus_send_fake.call_count, 0);
}

ZTEST(ble_service, test_send_authenticated_succeeds)
{
	current_conn = &test_conn;
	bt_conn_get_security_fake.return_val = BT_SECURITY_L2;
	bt_nus_send_fake.return_val = 0;

	int err = ble_service_send((const uint8_t *)"data", 4);

	zassert_equal(err, 0);
	zassert_equal(bt_nus_send_fake.call_count, 1);
}

ZTEST(ble_service, test_mtu_default_23)
{
	zassert_equal(ble_service_get_mtu(), 23);
}

ZTEST(ble_service, test_mtu_updates_on_callback)
{
	gatt_mtu_updated_cb(NULL, 247, 251);

	/* MIN(tx, rx) = 247 */
	zassert_equal(ble_service_get_mtu(), 247);
}

ZTEST(ble_service, test_connected_stores_handle)
{
	connected(&test_conn, 0);

	zassert_not_null(current_conn);
	zassert_equal(bt_conn_ref_fake.call_count, 1);
}

ZTEST(ble_service, test_disconnected_clears_state)
{
	/* Setup connected state */
	current_conn = &test_conn;
	current_mtu = 247;

	disconnected(&test_conn, 0);

	zassert_is_null(current_conn);
	zassert_equal(current_mtu, 23, "MTU should reset to 23");
	zassert_equal(bt_conn_unref_fake.call_count, 1);
}

ZTEST(ble_service, test_disconnected_restarts_adv)
{
	/* recycled_cb is what restarts advertising after disconnect */
	ble_service_init(test_data_cb);
	RESET_FAKE(k_work_submit);

	recycled_cb();

	/* ble_service_start_advertising → k_work_submit */
	zassert_equal(k_work_submit_fake.call_count, 1);
}

ZTEST(ble_service, test_is_authenticated_checks_l2)
{
	/* No connection → false */
	current_conn = NULL;
	zassert_false(ble_service_is_authenticated());

	/* Connected but L1 → false */
	current_conn = &test_conn;
	bt_conn_get_security_fake.return_val = BT_SECURITY_L1;
	zassert_false(ble_service_is_authenticated());

	/* Connected and L2 → true */
	bt_conn_get_security_fake.return_val = BT_SECURITY_L2;
	zassert_true(ble_service_is_authenticated());
}

ZTEST_SUITE(ble_service, NULL, NULL, NULL, NULL, NULL);
```

**Step 3: Build and run**

```bash
make test-suite SUITE=ble_service
```

Expected: 10 tests PASS.

**Step 4: Commit**

```bash
git add tests/ble_service/
git commit -m "test: add ble_service unit tests (10 tests)"
```

---

### Task 6: uart_bridge Tests (8 tests)

> **Apply patterns:** DECLARE+DEFINE for all fakes, block log headers, prj.conf. Also add `LOG_DBG` stub. Uses K_FIFO_DEFINE and K_THREAD_DEFINE stubs.

**Files:**
- Create: `tests/uart_bridge/CMakeLists.txt`
- Create: `tests/uart_bridge/testcase.yaml`
- Create: `tests/uart_bridge/src/main.c`

**Step 1: Create build files**

`tests/uart_bridge/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_uart_bridge)

target_sources(testbinary PRIVATE src/main.c)
target_include_directories(testbinary PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../mocks
)
```

`tests/uart_bridge/testcase.yaml`:
```yaml
tests:
  radpro_link.uart_bridge:
    tags: unit
    type: unit
```

**Step 2: Write test file**

`tests/uart_bridge/src/main.c`:
```c
/*
 * SPDX-License-Identifier: MIT
 * Unit tests for uart_bridge module.
 */

#include <zephyr/ztest.h>
#include <zephyr/fff.h>
#include <stdlib.h>
#include <string.h>

DEFINE_FFF_GLOBALS;

/* Stub logging */
#ifdef LOG_MODULE_REGISTER
#undef LOG_MODULE_REGISTER
#endif
#define LOG_MODULE_REGISTER(...)

#ifdef LOG_INF
#undef LOG_INF
#endif
#define LOG_INF(...)

#ifdef LOG_WRN
#undef LOG_WRN
#endif
#define LOG_WRN(...)

#ifdef LOG_ERR
#undef LOG_ERR
#endif
#define LOG_ERR(...)

#ifdef LOG_DBG
#undef LOG_DBG
#endif
#define LOG_DBG(...)

/* UART type stubs */
#include "uart_mocks.h"

/* Provide the test UART device referenced by DEVICE_DT_GET */
static struct device test_uart_device = { .name = "test_uart" };

/* FFF fakes — UART API */
DEFINE_FAKE_VALUE_FUNC(bool, device_is_ready, const struct device *);
DEFINE_FAKE_VALUE_FUNC(int, uart_callback_set, const struct device *,
		       uart_callback_t, void *);
DEFINE_FAKE_VALUE_FUNC(int, uart_tx, const struct device *,
		       const uint8_t *, size_t, int32_t);
DEFINE_FAKE_VALUE_FUNC(int, uart_rx_enable, const struct device *,
		       uint8_t *, size_t, int32_t);
DEFINE_FAKE_VALUE_FUNC(int, uart_rx_disable, const struct device *);
DEFINE_FAKE_VALUE_FUNC(int, uart_rx_buf_rsp, const struct device *,
		       uint8_t *, size_t);

/* FFF fakes — kernel memory */
DEFINE_FAKE_VALUE_FUNC(void *, k_malloc, size_t);
DEFINE_FAKE_VOID_FUNC(k_free, void *);

/* FFF fakes — kernel work */
DEFINE_FAKE_VOID_FUNC(k_work_init_delayable, struct k_work_delayable *,
		      k_work_handler_t);
DEFINE_FAKE_VALUE_FUNC(int, k_work_reschedule, struct k_work_delayable *,
		       k_timeout_t);

/* FFF fakes — kernel sleep */
DEFINE_FAKE_VALUE_FUNC(int32_t, k_sleep, k_timeout_t);

/* FFF fakes — FIFO (used by uart_bridge for TX/RX queues) */
DEFINE_FAKE_VOID_FUNC(k_fifo_put, struct k_fifo *, void *);
DEFINE_FAKE_VALUE_FUNC(void *, k_fifo_get, struct k_fifo *, k_timeout_t);

/* Stub K_FIFO_DEFINE — just declare the struct */
#ifdef K_FIFO_DEFINE
#undef K_FIFO_DEFINE
#endif
#define K_FIFO_DEFINE(name) static struct k_fifo name

/* Stub K_THREAD_DEFINE */
#ifdef K_THREAD_DEFINE
#undef K_THREAD_DEFINE
#endif
#define K_THREAD_DEFINE(name, stack, entry, p1, p2, p3, prio, opts, delay)

/* Stub CONTAINER_OF if not available */
#ifndef CONTAINER_OF
#define CONTAINER_OF(ptr, type, field) \
	((type *)((char *)(ptr) - offsetof(type, field)))
#endif

/* Stub ARG_UNUSED if not available */
#ifndef ARG_UNUSED
#define ARG_UNUSED(x) (void)(x)
#endif

/* snprintf should be available from stdlib */

/* Memory pool for k_malloc — provides real allocations for send tests */
#define TEST_BUF_POOL_SIZE 16
static uint8_t test_buf_pool[TEST_BUF_POOL_SIZE][280]; /* > sizeof uart_data_t */
static int test_buf_idx;

static void *k_malloc_from_pool(size_t size)
{
	if (test_buf_idx < TEST_BUF_POOL_SIZE && size <= sizeof(test_buf_pool[0])) {
		return test_buf_pool[test_buf_idx++];
	}
	return NULL;
}

static void k_free_noop(void *ptr)
{
	(void)ptr;
}

/* Capture uart_tx calls */
static uint8_t captured_tx_data[512];
static size_t captured_tx_len;

static int uart_tx_capture(const struct device *dev, const uint8_t *buf,
			   size_t len, int32_t timeout)
{
	if (len <= sizeof(captured_tx_data) - captured_tx_len) {
		memcpy(captured_tx_data + captured_tx_len, buf, len);
		captured_tx_len += len;
	}
	return 0;
}

/* RX callback tracking */
static bool rx_cb_called;
static uint16_t rx_cb_len;

static void test_rx_callback(const uint8_t *data, uint16_t len)
{
	rx_cb_called = true;
	rx_cb_len = len;
}

/* Include CUT */
#include "uart/uart_bridge.c"

/* --- FFF reset rule --- */
static void fff_reset_rule_before(const struct ztest_unit_test *test,
				  void *fixture)
{
	RESET_FAKE(device_is_ready);
	RESET_FAKE(uart_callback_set);
	RESET_FAKE(uart_tx);
	RESET_FAKE(uart_rx_enable);
	RESET_FAKE(uart_rx_disable);
	RESET_FAKE(uart_rx_buf_rsp);
	RESET_FAKE(k_malloc);
	RESET_FAKE(k_free);
	RESET_FAKE(k_work_init_delayable);
	RESET_FAKE(k_work_reschedule);
	RESET_FAKE(k_sleep);
	RESET_FAKE(k_fifo_put);
	RESET_FAKE(k_fifo_get);
	FFF_RESET_HISTORY();

	/* Reset module state */
	uart = NULL;
	uart_initialized = false;
	data_received_callback = NULL;
	uart_log_count = 0;

	/* Reset test state */
	test_buf_idx = 0;
	memset(captured_tx_data, 0, sizeof(captured_tx_data));
	captured_tx_len = 0;
	rx_cb_called = false;
	rx_cb_len = 0;

	/* Defaults */
	device_is_ready_fake.return_val = true;
	uart_callback_set_fake.return_val = 0;
	uart_tx_fake.return_val = 0;
	uart_rx_enable_fake.return_val = 0;
	k_malloc_fake.custom_fake = k_malloc_from_pool;
	k_free_fake.custom_fake = k_free_noop;
}

ZTEST_RULE(fff_reset_rule, fff_reset_rule_before, NULL);

/* --- Tests --- */

ZTEST(uart_bridge, test_init_enables_rx)
{
	int err = uart_bridge_init(test_rx_callback);

	zassert_equal(err, 0);
	/* uart_rx_enable called: once for welcome msg, once for init */
	zassert_true(uart_rx_enable_fake.call_count >= 1,
		     "uart_rx_enable should be called during init");
	zassert_true(uart_initialized);
}

ZTEST(uart_bridge, test_init_no_device_fails)
{
	device_is_ready_fake.return_val = false;

	int err = uart_bridge_init(test_rx_callback);

	zassert_equal(err, -ENODEV);
	zassert_false(uart_initialized);
}

ZTEST(uart_bridge, test_send_small_data)
{
	/* Initialize first */
	uart_bridge_init(test_rx_callback);
	RESET_FAKE(uart_tx);
	uart_tx_fake.return_val = 0;
	test_buf_idx = 0;
	k_malloc_fake.custom_fake = k_malloc_from_pool;

	uint8_t data[] = "hello";
	int err = uart_bridge_send(data, 5);

	zassert_equal(err, 0);
	zassert_equal(uart_tx_fake.call_count, 1);
}

ZTEST(uart_bridge, test_send_large_data_chunked)
{
	uart_bridge_init(test_rx_callback);
	RESET_FAKE(uart_tx);
	uart_tx_fake.return_val = 0;
	test_buf_idx = 0;
	k_malloc_fake.custom_fake = k_malloc_from_pool;

	/* 300 bytes > UART_BUF_SIZE-1 (255), should be split */
	uint8_t data[300];
	memset(data, 'A', sizeof(data));

	int err = uart_bridge_send(data, 300);

	zassert_equal(err, 0);
	zassert_true(uart_tx_fake.call_count >= 2,
		     "Should split into multiple uart_tx calls");
}

ZTEST(uart_bridge, test_cr_gets_lf_appended)
{
	uart_bridge_init(test_rx_callback);
	RESET_FAKE(uart_tx);
	uart_tx_fake.custom_fake = uart_tx_capture;
	captured_tx_len = 0;
	test_buf_idx = 0;
	k_malloc_fake.custom_fake = k_malloc_from_pool;

	uint8_t data[] = "test\r";
	int err = uart_bridge_send(data, 5);

	zassert_equal(err, 0);
	/* Last two bytes should be \r\n */
	zassert_true(captured_tx_len >= 6, "Should include appended LF");
	zassert_equal(captured_tx_data[captured_tx_len - 2], '\r');
	zassert_equal(captured_tx_data[captured_tx_len - 1], '\n');
}

ZTEST(uart_bridge, test_send_welcome_message)
{
	uart_tx_fake.custom_fake = uart_tx_capture;
	captured_tx_len = 0;

	int err = uart_bridge_init(test_rx_callback);

	zassert_equal(err, 0);
	/* Check welcome message was sent */
	zassert_true(captured_tx_len > 0, "Welcome message should be sent");
	zassert_true(memmem(captured_tx_data, captured_tx_len,
			    "BLE Bridge Ready", 16) != NULL,
		     "Welcome message should contain 'BLE Bridge Ready'");
}

ZTEST(uart_bridge, test_rx_callback_invoked)
{
	int err = uart_bridge_init(test_rx_callback);

	zassert_equal(err, 0);
	zassert_equal(data_received_callback, test_rx_callback);
}

ZTEST(uart_bridge, test_buf_alloc_failure_reschedules)
{
	uart_bridge_init(test_rx_callback);
	RESET_FAKE(k_malloc);
	RESET_FAKE(k_work_reschedule);

	/* Make k_malloc fail */
	k_malloc_fake.return_val = NULL;

	/* Simulate UART_RX_DISABLED event — calls uart_cb */
	struct uart_event evt = { .type = UART_RX_DISABLED };
	uart_cb(uart, &evt, NULL);

	/* Should reschedule work when malloc fails */
	zassert_equal(k_work_reschedule_fake.call_count, 1);
}

ZTEST_SUITE(uart_bridge, NULL, NULL, NULL, NULL, NULL);
```

**Step 3: Build and run**

```bash
make test-suite SUITE=uart_bridge
```

Expected: 8 tests PASS.

**Step 4: Commit**

```bash
git add tests/uart_bridge/
git commit -m "test: add uart_bridge unit tests (8 tests)"
```

---

### Task 7: main_flow Tests (5 tests)

> **Apply patterns:** DECLARE+DEFINE for all fakes, block log headers, prj.conf. This test fakes module-level functions (not Zephyr APIs), so DECLARE+DEFINE applies to `board_init`, `led_status_init`, etc. Also needs `k_sleep` macro redirect.

**Files:**
- Create: `tests/main_flow/CMakeLists.txt`
- Create: `tests/main_flow/testcase.yaml`
- Create: `tests/main_flow/src/main.c`

**Step 1: Create build files**

`tests/main_flow/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_main_flow)

target_sources(testbinary PRIVATE src/main.c)
target_include_directories(testbinary PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../mocks
)
```

`tests/main_flow/testcase.yaml`:
```yaml
tests:
  radpro_link.main_flow:
    tags: unit
    type: unit
```

**Step 2: Write test file**

This test `#include`s `main.c` directly to access the static `uart_data_handler`, `ble_data_handler`, and `app_init`.

`tests/main_flow/src/main.c`:
```c
/*
 * SPDX-License-Identifier: MIT
 * Unit tests for main application flow.
 */

#include <zephyr/ztest.h>
#include <zephyr/fff.h>

DEFINE_FFF_GLOBALS;

/* Stub logging */
#ifdef LOG_MODULE_REGISTER
#undef LOG_MODULE_REGISTER
#endif
#define LOG_MODULE_REGISTER(...)

#ifdef LOG_INF
#undef LOG_INF
#endif
#define LOG_INF(...)

#ifdef LOG_WRN
#undef LOG_WRN
#endif
#define LOG_WRN(...)

#ifdef LOG_ERR
#undef LOG_ERR
#endif
#define LOG_ERR(...)

/* BT type stubs */
#include "bt_mocks.h"

/* Block settings header */
#include "kernel_mocks.h"

/* Kconfig defines needed by main.c */
#define CONFIG_BT_DEVICE_NAME "TestDevice"
/* CONFIG_SETTINGS intentionally left undefined → IS_ENABLED returns 0 */

#ifndef IS_ENABLED
#define IS_ENABLED(config) 0
#endif

/* Stub K_THREAD_DEFINE — don't create threads */
#ifdef K_THREAD_DEFINE
#undef K_THREAD_DEFINE
#endif
#define K_THREAD_DEFINE(name, stack, entry, p1, p2, p3, prio, opts, delay)

/* FFF fakes — module init functions */
DEFINE_FAKE_VALUE_FUNC(int, board_init);
DEFINE_FAKE_VALUE_FUNC(int, led_status_init);
DEFINE_FAKE_VALUE_FUNC(int, security_manager_init, uint32_t);
DEFINE_FAKE_VALUE_FUNC(int, uart_bridge_init, uart_data_received_cb_t);
DEFINE_FAKE_VALUE_FUNC(int, bt_enable, bt_ready_cb_t);
DEFINE_FAKE_VALUE_FUNC(int, ble_service_init, ble_data_received_cb_t);
DEFINE_FAKE_VALUE_FUNC(int, ble_service_start_advertising);
DEFINE_FAKE_VALUE_FUNC(int, dfu_service_init);
DEFINE_FAKE_VOID_FUNC(bt_id_get, bt_addr_le_t *, size_t *);
DEFINE_FAKE_VALUE_FUNC(int, settings_load);

/* FFF fakes — data flow functions */
DEFINE_FAKE_VALUE_FUNC(bool, ble_service_is_authenticated);
DEFINE_FAKE_VALUE_FUNC(int, ble_service_send, const uint8_t *, uint16_t);
DEFINE_FAKE_VALUE_FUNC(int, uart_bridge_send, const uint8_t *, uint16_t);

/* FFF fakes — LED status */
DEFINE_FAKE_VOID_FUNC(led_status_set_connected, bool);
DEFINE_FAKE_VOID_FUNC(led_status_set_pairing_window, bool);
DEFINE_FAKE_VOID_FUNC(led_status_error);
DEFINE_FAKE_VALUE_FUNC(bool, security_manager_is_pairing_allowed);

/* FFF fake — k_sleep (used by main loop and K_FOREVER) */
DEFINE_FAKE_VALUE_FUNC(int32_t, k_sleep, k_timeout_t);

/*
 * We need to prevent the CUT's #include of module headers from
 * pulling in function declarations that conflict with our fakes.
 * The module headers only declare functions (no bodies), so the
 * FFF DEFINE_FAKE provides the actual body. C allows redeclaration
 * as long as types match.
 *
 * Block module headers that would include blocked Zephyr headers:
 */
/* ble_service.h includes <zephyr/bluetooth/bluetooth.h> — already blocked */
/* security_manager.h includes <zephyr/bluetooth/conn.h> — already blocked */
/* uart_bridge.h includes <zephyr/types.h> — should be available */
/* led_status.h, board_config.h, dfu_service.h — no Zephyr includes */

/* Include CUT (main.c) — provides static functions */
#include "main.c"

/* --- FFF reset rule --- */
static void fff_reset_rule_before(const struct ztest_unit_test *test,
				  void *fixture)
{
	RESET_FAKE(board_init);
	RESET_FAKE(led_status_init);
	RESET_FAKE(security_manager_init);
	RESET_FAKE(uart_bridge_init);
	RESET_FAKE(bt_enable);
	RESET_FAKE(ble_service_init);
	RESET_FAKE(ble_service_start_advertising);
	RESET_FAKE(dfu_service_init);
	RESET_FAKE(bt_id_get);
	RESET_FAKE(settings_load);
	RESET_FAKE(ble_service_is_authenticated);
	RESET_FAKE(ble_service_send);
	RESET_FAKE(uart_bridge_send);
	RESET_FAKE(led_status_set_connected);
	RESET_FAKE(led_status_set_pairing_window);
	RESET_FAKE(led_status_error);
	RESET_FAKE(security_manager_is_pairing_allowed);
	RESET_FAKE(k_sleep);
	FFF_RESET_HISTORY();

	/* Defaults — all init functions succeed */
	board_init_fake.return_val = 0;
	led_status_init_fake.return_val = 0;
	security_manager_init_fake.return_val = 0;
	uart_bridge_init_fake.return_val = 0;
	bt_enable_fake.return_val = 0;
	ble_service_init_fake.return_val = 0;
	ble_service_start_advertising_fake.return_val = 0;
	dfu_service_init_fake.return_val = 0;
}

ZTEST_RULE(fff_reset_rule, fff_reset_rule_before, NULL);

/* --- Tests --- */

ZTEST(main_flow, test_uart_to_ble_authenticated)
{
	ble_service_is_authenticated_fake.return_val = true;
	ble_service_send_fake.return_val = 0;

	uint8_t data[] = "sensor_data";
	uart_data_handler(data, sizeof(data));

	zassert_equal(ble_service_send_fake.call_count, 1);
}

ZTEST(main_flow, test_uart_to_ble_not_authenticated)
{
	ble_service_is_authenticated_fake.return_val = false;

	uint8_t data[] = "sensor_data";
	uart_data_handler(data, sizeof(data));

	zassert_equal(ble_service_send_fake.call_count, 0,
		      "Data should be dropped when not authenticated");
}

ZTEST(main_flow, test_ble_to_uart)
{
	uart_bridge_send_fake.return_val = 0;

	uint8_t data[] = "ble_command";
	struct bt_conn dummy_conn;
	ble_data_handler(&dummy_conn, data, sizeof(data));

	zassert_equal(uart_bridge_send_fake.call_count, 1);
}

ZTEST(main_flow, test_init_fails_on_ble_error)
{
	bt_enable_fake.return_val = -EIO;

	int err = app_init();

	zassert_not_equal(err, 0, "app_init should fail when bt_enable fails");
}

ZTEST(main_flow, test_init_uart_failure_non_fatal)
{
	uart_bridge_init_fake.return_val = -ENODEV;

	int err = app_init();

	zassert_equal(err, 0,
		      "app_init should succeed even if UART init fails");
	/* bt_enable should still be called after UART failure */
	zassert_equal(bt_enable_fake.call_count, 1);
}

ZTEST_SUITE(main_flow, NULL, NULL, NULL, NULL, NULL);
```

**Step 3: Build and run**

```bash
make test-suite SUITE=main_flow
```

Expected: 5 tests PASS.

**Step 4: Commit**

```bash
git add tests/main_flow/
git commit -m "test: add main_flow unit tests (5 tests)"
```

---

### Task 8: Test Runner Script and CI ✅ DONE (Makefile + docker-compose)

**Implemented as:** `Makefile` + `docker-compose.yml` instead of the originally planned shell script + CI workflow.

The docker-compose approach is simpler and more reliable — uses persistent `zephyr-workspace` Docker volume to cache the Zephyr checkout.

**Original plan files (superseded):**
- ~~`scripts/run_tests.sh`~~ → `Makefile` with `make test` / `make test-suite SUITE=<name>`
- ~~`.github/workflows/tests.yml`~~ → Can be added later if GitHub Actions CI is needed

**Step 1: Create test runner script**

`scripts/run_tests.sh`:
```bash
#!/usr/bin/env bash
set -euo pipefail

# Auto-detect ZEPHYR_BASE from PlatformIO if not set
if [ -z "${ZEPHYR_BASE:-}" ]; then
    ZEPHYR_BASE="$HOME/.platformio/packages/framework-zephyr"
    if [ ! -d "$ZEPHYR_BASE" ]; then
        echo "Error: ZEPHYR_BASE not found at $ZEPHYR_BASE"
        echo "Set ZEPHYR_BASE or install PlatformIO Zephyr package"
        exit 1
    fi
    export ZEPHYR_BASE
fi

echo "Using ZEPHYR_BASE=$ZEPHYR_BASE"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Run twister on all test suites
"$ZEPHYR_BASE/scripts/twister" \
    -p unit_testing \
    -T "$PROJECT_DIR/tests/" \
    --inline-logs \
    "$@"
```

**Step 2: Create CI workflow**

`.github/workflows/tests.yml`:
```yaml
name: Unit Tests

on:
  push:
    branches: [main, master, develop]
  pull_request:
    branches: [main, master, develop]

jobs:
  test:
    runs-on: ubuntu-latest
    container:
      image: ghcr.io/zephyrproject-rtos/ci:v0.27.2

    steps:
      - uses: actions/checkout@v4

      - name: Install west and Zephyr SDK
        run: |
          pip3 install west
          west init -l zephyr/
          west update --narrow --fetch-opt=--depth=1

      - name: Run unit tests
        run: |
          export ZEPHYR_BASE=$(west topdir)/zephyr
          $ZEPHYR_BASE/scripts/twister \
            -p unit_testing \
            -T tests/ \
            --inline-logs

      - name: Upload test results
        if: always()
        uses: actions/upload-artifact@v4
        with:
          name: twister-results
          path: twister-out/
          retention-days: 14
```

> **Note:** The CI workflow requires a `west.yml` manifest in the project root. This is a minimal file that points to the Zephyr repository. It's used only by CI and tests — PlatformIO remains the firmware build system. The exact `west.yml` content depends on the Zephyr version pinned in `platformio.ini` and should be created during implementation.

**Step 3: Commit**

```bash
chmod +x scripts/run_tests.sh
git add scripts/run_tests.sh .github/workflows/tests.yml
git commit -m "chore: add test runner script and CI workflow"
```

---

### Task 9: Run Full Suite and Fix Issues

**Step 1: Run all tests**

```bash
make test
```

**Step 2: Fix any compilation or test failures**

Common issues to watch for:
- **Zephyr header include guard names** may differ from the `ZEPHYR_INCLUDE_*` convention. If a blocked header still gets included, check the actual guard name in `$ZEPHYR_BASE/include/zephyr/` and update the mock header.
- **FFF fake prototypes** must exactly match the Zephyr function signatures. If `k_work_schedule` takes `struct k_work_delayable *` but the header declares it differently, adjust the DEFINE_FAKE.
- **Inline kernel functions** (`k_msleep`, etc.): if a kernel function is `static inline` in the header, it can't be FFF-faked directly. Use the `#undef`/`#define` redirect pattern shown in led_status tests.
- **`k_timeout_t` type**: this is a struct in Zephyr 3.x. FFF handles struct arguments, but assertions on `.ticks` may need the `K_MSEC` expansion.
- **Duplicate symbol errors**: if the unittest kernel provides a function we also FFF-fake, use `--wrap` linker flag: `target_link_options(testbinary PRIVATE -Wl,--wrap=<func_name>)`.

**Step 3: Verify all 42 tests pass**

```
Expected output:
SUITE: security_manager  — 8/8 PASS
SUITE: board_config      — 4/4 PASS
SUITE: led_status        — 7/7 PASS
SUITE: ble_service       — 10/10 PASS
SUITE: uart_bridge       — 8/8 PASS
SUITE: main_flow         — 5/5 PASS
TOTAL: 42/42 PASS
```

**Step 4: Final commit**

```bash
git add -A tests/ scripts/
git commit -m "test: complete unit test coverage (42 tests across 6 modules)"
```

---

## Troubleshooting Reference

| Problem | Fix |
|---------|-----|
| `unknown type name 'func_Fake'` | **Missing DECLARE.** Add `DECLARE_FAKE_*_FUNC(...)` before `DEFINE_FAKE_*_FUNC(...)`. Zephyr FFF requires both. |
| `undefined reference to 'z_log_minimal_printk'` | **Log headers not blocked.** Add `#define ZEPHYR_INCLUDE_LOGGING_LOG_H_` and `ZEPHYR_INCLUDE_LOGGING_LOG_CORE_H_` before mock includes. |
| `redefinition of 'k_uptime_get'` | **Static inline in kernel.h.** Use macro redirect: `#define k_uptime_get() test_k_uptime_get()` with manual stub. |
| `No prj.conf file(s) was found` | **Create empty `prj.conf`** in the test suite directory. |
| `GEN_KOBJECT_LIST-NOTFOUND` | **PlatformIO Zephyr scripts not executable.** Use Docker with proper west-managed Zephyr (`make test`). |
| `fatal error: zephyr/fff.h: No such file` | FFF path may be `<fff.h>` in older Zephyr. Check `$ZEPHYR_BASE/include/`. |
| `multiple definition of 'k_work_schedule'` | Kernel provides it. Add `target_link_options(testbinary PRIVATE -Wl,--wrap=k_work_schedule)` and rename fake to `__wrap_k_work_schedule`. |
| `error: 'K_MSEC' undeclared` | `<zephyr/kernel.h>` not included by ztest. Add `#include <zephyr/kernel.h>` before mock headers. |
| `warning: implicit declaration` | FFF DEFINE_FAKE must appear before `#include "module.c"`. Check ordering. |
| Header guard name wrong | Check actual guard in `$ZEPHYR_BASE/include/zephyr/bluetooth/bluetooth.h` (line 1-3). Update bt_mocks.h. |
| Root-owned build artifacts | Use `make test-clean` (runs Docker to clean). Don't run cmake directly on host. |
