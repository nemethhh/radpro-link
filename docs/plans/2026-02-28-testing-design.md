# RadPro-Link Unit Test Coverage Design

## Goal

Add comprehensive unit test coverage for all RadPro-Link modules. Both regression safety and correctness validation.

## Approach

Per-module unit tests using Zephyr's ztest framework with FFF (Fake Function Framework) mocks. Tests compile as native host binaries on the `unit_testing` pseudo-board and run via Twister.

## Test Infrastructure

### Directory Layout

```
tests/
├── mocks/                        # Shared FFF mock headers
│   ├── CMakeLists.txt
│   ├── bt_mocks.h               # BLE API fakes
│   ├── uart_mocks.h             # UART API fakes
│   ├── gpio_mocks.h             # GPIO API fakes
│   └── kernel_mocks.h           # k_work, k_uptime, settings, etc.
├── security_manager/
│   ├── CMakeLists.txt
│   ├── testcase.yaml
│   └── src/main.c
├── led_status/
│   ├── CMakeLists.txt
│   ├── testcase.yaml
│   └── src/main.c
├── board_config/
│   ├── CMakeLists.txt
│   ├── testcase.yaml
│   └── src/main.c
├── ble_service/
│   ├── CMakeLists.txt
│   ├── testcase.yaml
│   └── src/main.c
├── uart_bridge/
│   ├── CMakeLists.txt
│   ├── testcase.yaml
│   └── src/main.c
└── main_flow/
    ├── CMakeLists.txt
    ├── testcase.yaml
    └── src/main.c
```

### Running Tests

```bash
# Set ZEPHYR_BASE to PlatformIO's Zephyr
export ZEPHYR_BASE=~/.platformio/packages/framework-zephyr

# Run all unit tests
$ZEPHYR_BASE/scripts/twister -p unit_testing -T tests/

# Run a single suite
$ZEPHYR_BASE/scripts/twister -p unit_testing -T tests/security_manager/
```

A helper script `scripts/run_tests.sh` auto-detects ZEPHYR_BASE from PlatformIO.

### CMakeLists.txt Pattern

Each test suite follows:

```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_<module_name>)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../mocks mocks)
target_link_libraries(testbinary PRIVATE mocks)

target_include_directories(testbinary PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/<module>
)

target_sources(testbinary PRIVATE
    src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/<module>/<module>.c
)
```

### testcase.yaml Pattern

```yaml
tests:
  radpro_link.<module_name>:
    tags: unit
    type: unit
```

## Mocking Strategy

Mock at the Zephyr API boundary using FFF. Shared mock headers declare fakes for all Zephyr APIs used in production code.

### Mock Headers

**bt_mocks.h**: `bt_enable`, `bt_le_adv_start`, `bt_nus_send`, `bt_nus_cb_register`, `bt_conn_get_security`, `bt_conn_unref`, `bt_conn_auth_cancel`, `bt_conn_auth_passkey_confirm`

**uart_mocks.h**: `uart_callback_set`, `uart_rx_enable`, `uart_tx`, `device_is_ready`

**gpio_mocks.h**: `gpio_pin_configure_dt`, `gpio_pin_set_dt`

**kernel_mocks.h**: `k_work_schedule`, `k_uptime_get`, `settings_load`

### Reset Pattern

Every test suite uses a ZTEST_RULE to reset all fakes before each test:

```c
DEFINE_FFF_GLOBALS;

static void fff_reset_rule_before(const struct ztest_unit_test *test, void *fixture) {
    RESET_FAKE(bt_enable);
    // ... all fakes
    FFF_RESET_HISTORY();
}
ZTEST_RULE(fff_reset_rule, fff_reset_rule_before, NULL);
```

## Test Cases (42 total)

### security_manager (8 tests)

| Test | Verifies |
|------|----------|
| test_init_schedules_timeout | `k_work_schedule` called with correct delay |
| test_pairing_allowed_during_window | `is_pairing_allowed()` returns true before timeout |
| test_pairing_denied_after_window | Returns false after timeout handler fires |
| test_time_remaining_calculation | Math in `get_pairing_time_remaining()` |
| test_time_remaining_after_expiry | Returns 0 when window closed |
| test_passkey_confirm_during_window | Auth callback calls `bt_conn_auth_passkey_confirm` |
| test_passkey_confirm_after_window | Auth callback calls `bt_conn_auth_cancel` |
| test_oob_rejected | OOB callback always rejects |

### led_status (7 tests)

| Test | Verifies |
|------|----------|
| test_init_configures_gpio | `gpio_pin_configure_dt` called with OUTPUT flag |
| test_init_fails_no_device | Returns -ENODEV when device not ready |
| test_set_connected_flag | `led_status_set_connected(true)` sets internal state |
| test_set_pairing_window_flag | `led_status_set_pairing_window(false)` sets internal state |
| test_error_sets_error_mode | `led_status_error()` sets error flag |
| test_led_off_when_pairing_closed | LED stays off when pairing_window_active=false |
| test_blink_interval_pairing_connected | Thread uses 500ms interval when pairing+connected |

### board_config (4 tests)

| Test | Verifies |
|------|----------|
| test_config_no_usb | nRF54L15 config has has_usb=false |
| test_config_no_async_adapter | nRF54L15 config has needs_async_adapter=false |
| test_board_name_set | Config name is not NULL/empty |
| test_init_skips_usb_when_no_usb | `usb_enable` not called when has_usb=false |

### ble_service (10 tests)

| Test | Verifies |
|------|----------|
| test_init_registers_nus | `bt_nus_cb_register` called during init |
| test_send_no_connection_fails | Error when current_conn is NULL |
| test_send_not_authenticated_fails | Error when security < L2 |
| test_send_authenticated_succeeds | `bt_nus_send` called with correct data |
| test_mtu_default_23 | `get_mtu()` returns 23 before negotiation |
| test_mtu_updates_on_callback | MTU callback updates internal value |
| test_connected_stores_handle | Connection callback stores bt_conn ref |
| test_disconnected_clears_state | Disconnect clears conn, resets MTU to 23 |
| test_disconnected_restarts_adv | Disconnect schedules advertising work |
| test_is_authenticated_checks_l2 | Returns true only when conn + security >= L2 |

### uart_bridge (8 tests)

| Test | Verifies |
|------|----------|
| test_init_enables_rx | `uart_rx_enable` called after init |
| test_init_no_device_fails | Error when UART device not ready |
| test_send_small_data | Data < 255 bytes sent in single uart_tx |
| test_send_large_data_chunked | Data > 255 bytes split into multiple chunks |
| test_cr_gets_lf_appended | CR byte triggers LF append |
| test_send_welcome_message | Init sends welcome string via uart_tx |
| test_rx_callback_invoked | RX data forwarded to application callback |
| test_buf_alloc_failure_reschedules | Allocation failure triggers delayed retry |

### main_flow (5 tests)

| Test | Verifies |
|------|----------|
| test_uart_to_ble_authenticated | uart_data_handler forwards to ble_service_send when authenticated |
| test_uart_to_ble_not_authenticated | uart_data_handler drops data when not authenticated |
| test_ble_to_uart | ble_data_handler forwards to uart_bridge_send |
| test_init_fails_on_ble_error | app_init returns error if bt_enable fails |
| test_init_uart_failure_non_fatal | app_init continues if UART init fails |

## Excluded

**dfu_service**: 6 lines of logging, no testable logic.

## Production Code Changes

None. Module APIs are already clean with public headers. For `main_flow` tests, the test file includes `main.c` directly to access static functions.

## CI Integration

GitHub Actions workflow at `.github/workflows/tests.yml`:

- Installs `west` + Zephyr SDK
- Runs `twister -p unit_testing -T tests/ --inline-logs`
- Uploads twister-out/ as artifact
- Triggers on push and pull_request

Requires a minimal `west.yml` manifest pointing at Zephyr (used only by CI and test runner; PlatformIO remains the firmware build system).
