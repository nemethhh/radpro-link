/*
 * SPDX-License-Identifier: MIT
 * Unit tests for led_status module.
 */

#include <zephyr/ztest.h>
#include <zephyr/fff.h>
#include <setjmp.h>

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

/* GPIO type stubs */
#include "gpio_mocks.h"

/* FFF fakes for GPIO functions — DECLARE then DEFINE */
DECLARE_FAKE_VALUE_FUNC(bool, gpio_is_ready_dt, const struct gpio_dt_spec *);
DEFINE_FAKE_VALUE_FUNC(bool, gpio_is_ready_dt, const struct gpio_dt_spec *);

DECLARE_FAKE_VALUE_FUNC(int, gpio_pin_configure_dt,
			const struct gpio_dt_spec *, gpio_flags_t);
DEFINE_FAKE_VALUE_FUNC(int, gpio_pin_configure_dt,
		       const struct gpio_dt_spec *, gpio_flags_t);

DECLARE_FAKE_VALUE_FUNC(int, gpio_pin_set_dt,
			const struct gpio_dt_spec *, int);
DEFINE_FAKE_VALUE_FUNC(int, gpio_pin_set_dt,
		       const struct gpio_dt_spec *, int);

/* k_msleep is static inline in kernel.h — override via macro redirect.
 * Same pattern as k_uptime_get in security_manager tests.
 * Supports custom_fake for the longjmp thread escape. */
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
	k_msleep_fake_return_val = 0;
	k_msleep_fake_call_count = 0;
	k_msleep_custom_fake = NULL;
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

	k_msleep_custom_fake = k_msleep_longjmp;

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

	k_msleep_custom_fake = k_msleep_longjmp;

	if (setjmp(test_jmp) == 0) {
		led_status_thread();
	}

	zassert_equal(captured_sleep_ms, 500,
		      "Should use MEDIUM_BLINK_INTERVAL_MS (500ms)");
}

ZTEST_SUITE(led_status, NULL, NULL, NULL, NULL, NULL);
