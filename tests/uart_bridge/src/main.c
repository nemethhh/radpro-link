/*
 * SPDX-License-Identifier: MIT
 * Unit tests for uart_bridge module.
 */

#define _GNU_SOURCE /* for memmem */

#include <zephyr/ztest.h>
#include <zephyr/fff.h>
#include <stdlib.h>
#include <string.h>

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

#ifdef LOG_DBG
#undef LOG_DBG
#endif
#define LOG_DBG(...)

/* Block Zephyr logging — prevent CUT from pulling in real logging */
#define ZEPHYR_INCLUDE_LOGGING_LOG_H_
#define ZEPHYR_INCLUDE_LOGGING_LOG_CORE_H_

/* UART type stubs */
#include "uart_mocks.h"

/* Provide the test UART device referenced by DEVICE_DT_GET */
static struct device test_uart_device = { .name = "test_uart" };

/* FFF fakes — UART API — DECLARE then DEFINE */
DECLARE_FAKE_VALUE_FUNC(bool, device_is_ready, const struct device *);
DEFINE_FAKE_VALUE_FUNC(bool, device_is_ready, const struct device *);

DECLARE_FAKE_VALUE_FUNC(int, uart_callback_set, const struct device *,
			uart_callback_t, void *);
DEFINE_FAKE_VALUE_FUNC(int, uart_callback_set, const struct device *,
		       uart_callback_t, void *);

DECLARE_FAKE_VALUE_FUNC(int, uart_tx, const struct device *,
			const uint8_t *, size_t, int32_t);
DEFINE_FAKE_VALUE_FUNC(int, uart_tx, const struct device *,
		       const uint8_t *, size_t, int32_t);

DECLARE_FAKE_VALUE_FUNC(int, uart_rx_enable, const struct device *,
			uint8_t *, size_t, int32_t);
DEFINE_FAKE_VALUE_FUNC(int, uart_rx_enable, const struct device *,
		       uint8_t *, size_t, int32_t);

DECLARE_FAKE_VALUE_FUNC(int, uart_rx_disable, const struct device *);
DEFINE_FAKE_VALUE_FUNC(int, uart_rx_disable, const struct device *);

DECLARE_FAKE_VALUE_FUNC(int, uart_rx_buf_rsp, const struct device *,
			uint8_t *, size_t);
DEFINE_FAKE_VALUE_FUNC(int, uart_rx_buf_rsp, const struct device *,
		       uint8_t *, size_t);

/* k_malloc and k_free are provided by the unit test kernel — use macro redirect */
static void *k_malloc_fake_return_val;
static int k_malloc_fake_call_count;
static void *(*k_malloc_fake_custom_fake)(size_t);
static void *test_k_malloc(size_t size)
{
	k_malloc_fake_call_count++;
	if (k_malloc_fake_custom_fake) {
		return k_malloc_fake_custom_fake(size);
	}
	return k_malloc_fake_return_val;
}
#define k_malloc(size) test_k_malloc(size)

static int k_free_fake_call_count;
static void (*k_free_fake_custom_fake)(void *);
static void test_k_free(void *ptr)
{
	k_free_fake_call_count++;
	if (k_free_fake_custom_fake) {
		k_free_fake_custom_fake(ptr);
	}
}
#define k_free(ptr) test_k_free(ptr)

/* FFF fakes — kernel work */
DECLARE_FAKE_VOID_FUNC(k_work_init_delayable, struct k_work_delayable *,
			k_work_handler_t);
DEFINE_FAKE_VOID_FUNC(k_work_init_delayable, struct k_work_delayable *,
		      k_work_handler_t);

DECLARE_FAKE_VALUE_FUNC(int, k_work_reschedule, struct k_work_delayable *,
			k_timeout_t);
DEFINE_FAKE_VALUE_FUNC(int, k_work_reschedule, struct k_work_delayable *,
		       k_timeout_t);

/* k_sleep is static inline in kernel.h — use macro redirect */
static int32_t k_sleep_fake_return_val;
static int k_sleep_fake_call_count;
static int32_t test_k_sleep(k_timeout_t timeout)
{
	k_sleep_fake_call_count++;
	return k_sleep_fake_return_val;
}
#define k_sleep(t) test_k_sleep(t)

/* k_fifo_put and k_fifo_get are macros in kernel.h — undef then fake */
#ifdef k_fifo_put
#undef k_fifo_put
#endif

DECLARE_FAKE_VOID_FUNC(k_fifo_put, struct k_fifo *, void *);
DEFINE_FAKE_VOID_FUNC(k_fifo_put, struct k_fifo *, void *);

#ifdef k_fifo_get
#undef k_fifo_get
#endif

DECLARE_FAKE_VALUE_FUNC(void *, k_fifo_get, struct k_fifo *, k_timeout_t);
DEFINE_FAKE_VALUE_FUNC(void *, k_fifo_get, struct k_fifo *, k_timeout_t);

/* Stub K_FIFO_DEFINE — just declare the struct */
#ifdef K_FIFO_DEFINE
#undef K_FIFO_DEFINE
#endif
#define K_FIFO_DEFINE(name) struct k_fifo name

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
	k_malloc_fake_return_val = NULL;
	k_malloc_fake_call_count = 0;
	k_malloc_fake_custom_fake = k_malloc_from_pool;
	k_free_fake_call_count = 0;
	k_free_fake_custom_fake = k_free_noop;
	RESET_FAKE(k_work_init_delayable);
	RESET_FAKE(k_work_reschedule);
	RESET_FAKE(k_fifo_put);
	RESET_FAKE(k_fifo_get);
	k_sleep_fake_return_val = 0;
	k_sleep_fake_call_count = 0;
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
}

ZTEST_RULE(fff_reset_rule, fff_reset_rule_before, NULL);

/* --- Tests --- */

ZTEST(uart_bridge, test_init_enables_rx)
{
	int err = uart_bridge_init(test_rx_callback);

	zassert_equal(err, 0);
	/* uart_rx_enable called: once for init */
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
	k_malloc_fake_custom_fake = k_malloc_from_pool;

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
	k_malloc_fake_custom_fake = k_malloc_from_pool;

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
	k_malloc_fake_custom_fake = k_malloc_from_pool;

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
	k_malloc_fake_custom_fake = NULL;
	k_malloc_fake_return_val = NULL;
	k_malloc_fake_call_count = 0;
	RESET_FAKE(k_work_reschedule);

	/* Simulate UART_RX_DISABLED event — calls uart_cb */
	struct uart_event evt = { .type = UART_RX_DISABLED };
	uart_cb(uart, &evt, NULL);

	/* Should reschedule work when malloc fails */
	zassert_equal(k_work_reschedule_fake.call_count, 1);
}

ZTEST_SUITE(uart_bridge, NULL, NULL, NULL, NULL, NULL);
