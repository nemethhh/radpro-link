/*
 * SPDX-License-Identifier: MIT
 * Unit tests for main application flow.
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

/* Block Zephyr logging — prevent CUT from pulling in real logging */
#define ZEPHYR_INCLUDE_LOGGING_LOG_H_
#define ZEPHYR_INCLUDE_LOGGING_LOG_CORE_H_

/* BT type stubs */
#include "bt_mocks.h"

/* Block settings header */
#include "kernel_mocks.h"

/* Kconfig defines needed by main.c */
#define CONFIG_BT_DEVICE_NAME "TestDevice"
/* CONFIG_SETTINGS intentionally left undefined -> IS_ENABLED returns 0 */

#ifndef IS_ENABLED
#define IS_ENABLED(config) 0
#endif

/* Include module headers for type definitions (before FFF fakes).
 * BT headers are already blocked by bt_mocks.h.
 * Settings/USB headers are blocked by kernel_mocks.h. */
#include "board/board_config.h"
#include "ble/ble_service.h"
#include "uart/uart_bridge.h"
#include "security/security_manager.h"
#include "led/led_status.h"
#include "dfu/dfu_service.h"

/* Stub K_THREAD_DEFINE — don't create threads */
#ifdef K_THREAD_DEFINE
#undef K_THREAD_DEFINE
#endif
#define K_THREAD_DEFINE(name, stack, entry, p1, p2, p3, prio, opts, delay)

/* --- Manual fakes for zero-arg functions ---
 * Zephyr FFF has type conflicts between DECLARE/DEFINE for zero-arg funcs.
 * Use manual stubs with .return_val / .call_count API. */
#define MANUAL_FAKE_VALUE_FUNC0(ret_type, fname) \
	static struct { ret_type return_val; int call_count; } fname##_fake; \
	ret_type fname(void) { fname##_fake.call_count++; return fname##_fake.return_val; }

#define MANUAL_FAKE_VOID_FUNC0(fname) \
	static struct { int call_count; } fname##_fake; \
	void fname(void) { fname##_fake.call_count++; }

#define RESET_MANUAL_FAKE(fname) memset(&fname##_fake, 0, sizeof(fname##_fake))

MANUAL_FAKE_VALUE_FUNC0(int, board_init)
MANUAL_FAKE_VALUE_FUNC0(int, led_status_init)
MANUAL_FAKE_VALUE_FUNC0(int, ble_service_start_advertising)
MANUAL_FAKE_VALUE_FUNC0(int, dfu_service_init)
MANUAL_FAKE_VALUE_FUNC0(int, settings_load)
MANUAL_FAKE_VALUE_FUNC0(bool, ble_service_is_authenticated)
MANUAL_FAKE_VALUE_FUNC0(bool, security_manager_is_pairing_allowed)
MANUAL_FAKE_VOID_FUNC0(led_status_error)

/* --- FFF fakes for functions with args (DECLARE+DEFINE) --- */
DECLARE_FAKE_VALUE_FUNC(int, security_manager_init, uint32_t);
DEFINE_FAKE_VALUE_FUNC(int, security_manager_init, uint32_t);

DECLARE_FAKE_VALUE_FUNC(int, uart_bridge_init, uart_data_received_cb_t);
DEFINE_FAKE_VALUE_FUNC(int, uart_bridge_init, uart_data_received_cb_t);

DECLARE_FAKE_VALUE_FUNC(int, bt_enable, bt_ready_cb_t);
DEFINE_FAKE_VALUE_FUNC(int, bt_enable, bt_ready_cb_t);

DECLARE_FAKE_VALUE_FUNC(int, ble_service_init, ble_data_received_cb_t);
DEFINE_FAKE_VALUE_FUNC(int, ble_service_init, ble_data_received_cb_t);

DECLARE_FAKE_VOID_FUNC(bt_id_get, bt_addr_le_t *, size_t *);
DEFINE_FAKE_VOID_FUNC(bt_id_get, bt_addr_le_t *, size_t *);

DECLARE_FAKE_VALUE_FUNC(int, ble_service_send, const uint8_t *, uint16_t);
DEFINE_FAKE_VALUE_FUNC(int, ble_service_send, const uint8_t *, uint16_t);

DECLARE_FAKE_VALUE_FUNC(int, uart_bridge_send, const uint8_t *, uint16_t);
DEFINE_FAKE_VALUE_FUNC(int, uart_bridge_send, const uint8_t *, uint16_t);

DECLARE_FAKE_VOID_FUNC(led_status_set_connected, bool);
DEFINE_FAKE_VOID_FUNC(led_status_set_connected, bool);

DECLARE_FAKE_VOID_FUNC(led_status_set_pairing_window, bool);
DEFINE_FAKE_VOID_FUNC(led_status_set_pairing_window, bool);

/* k_sleep is static inline in kernel.h — use macro redirect */
static int32_t k_sleep_fake_return_val;
static int k_sleep_fake_call_count;
static int32_t test_k_sleep(k_timeout_t timeout)
{
	k_sleep_fake_call_count++;
	return k_sleep_fake_return_val;
}
#define k_sleep(t) test_k_sleep(t)

/* Rename CUT's main() to avoid conflict with ztest's main */
#define main radpro_main

/* Include CUT (main.c) — provides static functions */
#include "main.c"

#undef main

/* --- FFF reset rule --- */
static void fff_reset_rule_before(const struct ztest_unit_test *test,
				  void *fixture)
{
	/* Reset manual zero-arg fakes */
	RESET_MANUAL_FAKE(board_init);
	RESET_MANUAL_FAKE(led_status_init);
	RESET_MANUAL_FAKE(ble_service_start_advertising);
	RESET_MANUAL_FAKE(dfu_service_init);
	RESET_MANUAL_FAKE(settings_load);
	RESET_MANUAL_FAKE(ble_service_is_authenticated);
	RESET_MANUAL_FAKE(security_manager_is_pairing_allowed);
	RESET_MANUAL_FAKE(led_status_error);

	/* Reset FFF fakes (functions with args) */
	RESET_FAKE(security_manager_init);
	RESET_FAKE(uart_bridge_init);
	RESET_FAKE(bt_enable);
	RESET_FAKE(ble_service_init);
	RESET_FAKE(bt_id_get);
	RESET_FAKE(ble_service_send);
	RESET_FAKE(uart_bridge_send);
	RESET_FAKE(led_status_set_connected);
	RESET_FAKE(led_status_set_pairing_window);
	k_sleep_fake_return_val = 0;
	k_sleep_fake_call_count = 0;
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
