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

/* Block Zephyr logging — prevent CUT from pulling in real logging */
#define ZEPHYR_INCLUDE_LOGGING_LOG_H_
#define ZEPHYR_INCLUDE_LOGGING_LOG_CORE_H_

/* BT type stubs */
#include "bt_mocks.h"

/* FFF fakes — DECLARE then DEFINE (Zephyr FFF requires both) */
DECLARE_FAKE_VALUE_FUNC(int, bt_conn_auth_cb_register,
			struct bt_conn_auth_cb *);
DEFINE_FAKE_VALUE_FUNC(int, bt_conn_auth_cb_register,
		       struct bt_conn_auth_cb *);

DECLARE_FAKE_VALUE_FUNC(int, bt_conn_auth_info_cb_register,
			struct bt_conn_auth_info_cb *);
DEFINE_FAKE_VALUE_FUNC(int, bt_conn_auth_info_cb_register,
		       struct bt_conn_auth_info_cb *);

DECLARE_FAKE_VOID_FUNC(bt_conn_auth_cancel, struct bt_conn *);
DEFINE_FAKE_VOID_FUNC(bt_conn_auth_cancel, struct bt_conn *);

DECLARE_FAKE_VOID_FUNC(bt_conn_auth_passkey_confirm, struct bt_conn *);
DEFINE_FAKE_VOID_FUNC(bt_conn_auth_passkey_confirm, struct bt_conn *);

DECLARE_FAKE_VOID_FUNC(bt_conn_auth_pairing_confirm, struct bt_conn *);
DEFINE_FAKE_VOID_FUNC(bt_conn_auth_pairing_confirm, struct bt_conn *);

DECLARE_FAKE_VALUE_FUNC(const bt_addr_le_t *, bt_conn_get_dst,
			const struct bt_conn *);
DEFINE_FAKE_VALUE_FUNC(const bt_addr_le_t *, bt_conn_get_dst,
		       const struct bt_conn *);

DECLARE_FAKE_VOID_FUNC(bt_addr_le_to_str, const bt_addr_le_t *, char *,
			size_t);
DEFINE_FAKE_VOID_FUNC(bt_addr_le_to_str, const bt_addr_le_t *, char *,
		       size_t);

DECLARE_FAKE_VALUE_FUNC(const char *, bt_security_err_to_str,
			enum bt_security_err);
DEFINE_FAKE_VALUE_FUNC(const char *, bt_security_err_to_str,
		       enum bt_security_err);

/* FFF fakes for kernel functions */
DECLARE_FAKE_VOID_FUNC(k_work_init_delayable, struct k_work_delayable *,
			k_work_handler_t);
DEFINE_FAKE_VOID_FUNC(k_work_init_delayable, struct k_work_delayable *,
		      k_work_handler_t);

DECLARE_FAKE_VALUE_FUNC(int, k_work_schedule, struct k_work_delayable *,
			k_timeout_t);
DEFINE_FAKE_VALUE_FUNC(int, k_work_schedule, struct k_work_delayable *,
		       k_timeout_t);
/* k_uptime_get is static inline in kernel.h — override via macro redirect */
static int64_t k_uptime_get_fake_return_val;
static int k_uptime_get_fake_call_count;
static int64_t test_k_uptime_get(void)
{
	k_uptime_get_fake_call_count++;
	return k_uptime_get_fake_return_val;
}
#define k_uptime_get() test_k_uptime_get()

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
	k_uptime_get_fake_return_val = 0;
	k_uptime_get_fake_call_count = 0;
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
	k_uptime_get_fake_return_val = 0;

	int err = security_manager_init(60000);

	zassert_equal(err, 0);
	zassert_equal(k_work_init_delayable_fake.call_count, 1);
	zassert_equal(k_work_schedule_fake.call_count, 1);
}

ZTEST(security_manager, test_pairing_allowed_during_window)
{
	k_uptime_get_fake_return_val = 0;
	security_manager_init(60000);

	zassert_true(security_manager_is_pairing_allowed());
}

ZTEST(security_manager, test_pairing_denied_after_window)
{
	k_uptime_get_fake_return_val = 0;
	security_manager_init(60000);

	/* Simulate timeout handler firing */
	pairing_timeout_handler(NULL);

	zassert_false(security_manager_is_pairing_allowed());
}

ZTEST(security_manager, test_time_remaining_calculation)
{
	/* Init at uptime=1000 with 60s window → end_time=61000 */
	k_uptime_get_fake_return_val = 1000;
	security_manager_init(60000);

	/* 30s later → 31000 remaining */
	k_uptime_get_fake_return_val = 31000;
	uint32_t remaining = security_manager_get_pairing_time_remaining();

	zassert_equal(remaining, 30000);
}

ZTEST(security_manager, test_time_remaining_after_expiry)
{
	k_uptime_get_fake_return_val = 0;
	security_manager_init(60000);

	/* Timeout fires → pairing_allowed = false */
	pairing_timeout_handler(NULL);

	uint32_t remaining = security_manager_get_pairing_time_remaining();

	zassert_equal(remaining, 0);
}

ZTEST(security_manager, test_passkey_confirm_during_window)
{
	k_uptime_get_fake_return_val = 0;
	security_manager_init(60000);

	/* pairing_allowed is true → should confirm */
	auth_passkey_confirm(&test_conn, 123456);

	zassert_equal(bt_conn_auth_passkey_confirm_fake.call_count, 1);
	zassert_equal(bt_conn_auth_cancel_fake.call_count, 0);
}

ZTEST(security_manager, test_passkey_confirm_after_window)
{
	k_uptime_get_fake_return_val = 0;
	security_manager_init(60000);
	pairing_timeout_handler(NULL);

	auth_passkey_confirm(&test_conn, 123456);

	zassert_equal(bt_conn_auth_cancel_fake.call_count, 1);
	zassert_equal(bt_conn_auth_passkey_confirm_fake.call_count, 0);
}

ZTEST(security_manager, test_oob_rejected)
{
	k_uptime_get_fake_return_val = 0;
	security_manager_init(60000);

	struct bt_conn_oob_info info = { 0 };

	/* OOB always rejected, even during pairing window */
	auth_oob_data_request(&test_conn, &info);

	zassert_equal(bt_conn_auth_cancel_fake.call_count, 1);
}

ZTEST_SUITE(security_manager, NULL, NULL, NULL, NULL, NULL);
