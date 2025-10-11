/*
 * SPDX-License-Identifier: MIT
 * Security Manager Module - Implementation
 */

#include "security_manager.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(security_manager, LOG_LEVEL_INF);

/* State */
static bool pairing_allowed = true;
static struct k_work_delayable pairing_timeout_work;
static uint32_t pairing_window_ms;
static int64_t pairing_window_end_time;

/* Pairing timeout handler */
static void pairing_timeout_handler(struct k_work *work)
{
	pairing_allowed = false;
	LOG_WRN("Pairing window closed - no new pairings allowed");
	LOG_INF("Device will continue with existing paired devices only");
}

/* Authentication callbacks */
static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Passkey for %s: %06u", addr, passkey);
	LOG_INF("Auto-displaying passkey during pairing window");
}

static void auth_passkey_confirm(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (!pairing_allowed) {
		LOG_WRN("Pairing rejected - pairing window closed");
		bt_conn_auth_cancel(conn);
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Passkey for %s: %06u", addr, passkey);
	LOG_INF("Auto-confirming pairing during pairing window");

	bt_conn_auth_passkey_confirm(conn);
}

static void auth_pairing_confirm(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (!pairing_allowed) {
		LOG_WRN("Pairing rejected - pairing window closed");
		bt_conn_auth_cancel(conn);
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Auto-confirming pairing request for %s", addr);

	bt_conn_auth_pairing_confirm(conn);
}

static void auth_oob_data_request(struct bt_conn *conn, struct bt_conn_oob_info *info)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (!pairing_allowed) {
		LOG_WRN("OOB pairing rejected - pairing window closed");
		bt_conn_auth_cancel(conn);
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("OOB data request for %s - rejecting (no OOB data)", addr);

	bt_conn_auth_cancel(conn);
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Pairing cancelled: %s", addr);
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Pairing completed with %s, bonded: %s", addr, bonded ? "Yes" : "No");
	if (bonded) {
		LOG_INF("Device %s will be remembered across reboots", addr);
	}
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_WRN("Pairing failed with %s, reason %d %s", addr, reason,
		bt_security_err_to_str(reason));
}

static struct bt_conn_auth_cb conn_auth_callbacks = {
	.passkey_display = auth_passkey_display,
	.passkey_confirm = auth_passkey_confirm,
	.passkey_entry = NULL,
	.cancel = auth_cancel,
	.pairing_confirm = auth_pairing_confirm,
	.oob_data_request = auth_oob_data_request,
};

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed
};

/* Public API */
int security_manager_init(uint32_t window_ms)
{
	int err;

	pairing_window_ms = window_ms;
	pairing_window_end_time = k_uptime_get() + window_ms;

	/* Register authentication callbacks */
	err = bt_conn_auth_cb_register(&conn_auth_callbacks);
	if (err) {
		LOG_ERR("Failed to register auth callbacks: %d", err);
		return err;
	}

	err = bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);
	if (err) {
		LOG_ERR("Failed to register auth info callbacks: %d", err);
		return err;
	}

	/* Start pairing timeout timer */
	k_work_init_delayable(&pairing_timeout_work, pairing_timeout_handler);
	k_work_schedule(&pairing_timeout_work, K_MSEC(window_ms));

	LOG_INF("Security manager initialized (pairing window: %u minutes)",
		window_ms / 60000);

	return 0;
}

bool security_manager_is_pairing_allowed(void)
{
	return pairing_allowed;
}

uint32_t security_manager_get_pairing_time_remaining(void)
{
	if (!pairing_allowed) {
		return 0;
	}

	int64_t remaining = pairing_window_end_time - k_uptime_get();
	return (remaining > 0) ? (uint32_t)remaining : 0;
}