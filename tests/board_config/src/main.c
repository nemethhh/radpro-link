/*
 * SPDX-License-Identifier: MIT
 * Unit tests for board_config module.
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

/* Block settings and USB headers */
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
static int32_t test_k_sleep(k_timeout_t timeout)
{
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
