/*
 * SPDX-License-Identifier: MIT
 * Unit tests for ble_service module.
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

#ifdef LOG_DBG
#undef LOG_DBG
#endif
#define LOG_DBG(...)

/* Block Zephyr logging — prevent CUT from pulling in real logging */
#define ZEPHYR_INCLUDE_LOGGING_LOG_H_
#define ZEPHYR_INCLUDE_LOGGING_LOG_CORE_H_

/* BT type stubs */
#include "bt_mocks.h"

/* ble_service.c uses CONFIG_BT_DEVICE_NAME */
#define CONFIG_BT_DEVICE_NAME "TestDevice"

/* ble_service.c uses CONFIG_BT_USER_DATA_LEN_UPDATE — leave undefined */

/* FFF fakes — BT connection functions — DECLARE then DEFINE */
DECLARE_FAKE_VALUE_FUNC(struct bt_conn *, bt_conn_ref, struct bt_conn *);
DEFINE_FAKE_VALUE_FUNC(struct bt_conn *, bt_conn_ref, struct bt_conn *);

DECLARE_FAKE_VOID_FUNC(bt_conn_unref, struct bt_conn *);
DEFINE_FAKE_VOID_FUNC(bt_conn_unref, struct bt_conn *);

DECLARE_FAKE_VALUE_FUNC(bt_security_t, bt_conn_get_security,
			struct bt_conn *);
DEFINE_FAKE_VALUE_FUNC(bt_security_t, bt_conn_get_security,
		       struct bt_conn *);

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

/* FFF fakes — GATT/NUS */
DECLARE_FAKE_VALUE_FUNC(uint16_t, bt_gatt_get_mtu, struct bt_conn *);
DEFINE_FAKE_VALUE_FUNC(uint16_t, bt_gatt_get_mtu, struct bt_conn *);

DECLARE_FAKE_VOID_FUNC(bt_gatt_cb_register, struct bt_gatt_cb *);
DEFINE_FAKE_VOID_FUNC(bt_gatt_cb_register, struct bt_gatt_cb *);

DECLARE_FAKE_VALUE_FUNC(int, bt_nus_cb_register, struct bt_nus_cb *,
			void *);
DEFINE_FAKE_VALUE_FUNC(int, bt_nus_cb_register, struct bt_nus_cb *,
		       void *);

DECLARE_FAKE_VALUE_FUNC(int, bt_nus_send, struct bt_conn *,
			const uint8_t *, uint16_t);
DEFINE_FAKE_VALUE_FUNC(int, bt_nus_send, struct bt_conn *,
		       const uint8_t *, uint16_t);

/* FFF fakes — advertising */
DECLARE_FAKE_VALUE_FUNC(int, bt_le_adv_start, const struct bt_le_adv_param *,
			const struct bt_data *, size_t,
			const struct bt_data *, size_t);
DEFINE_FAKE_VALUE_FUNC(int, bt_le_adv_start, const struct bt_le_adv_param *,
		       const struct bt_data *, size_t,
		       const struct bt_data *, size_t);

/* FFF fakes — kernel work */
DECLARE_FAKE_VOID_FUNC(k_work_init, struct k_work *, k_work_handler_t);
DEFINE_FAKE_VOID_FUNC(k_work_init, struct k_work *, k_work_handler_t);

DECLARE_FAKE_VALUE_FUNC(int, k_work_submit, struct k_work *);
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
