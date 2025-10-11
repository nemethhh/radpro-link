/*
 * SPDX-License-Identifier: MIT
 * BLE Service Module - Implementation
 */

#include "ble_service.h"
#include "../security/security_manager.h"

#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/services/nus.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ble_service, LOG_LEVEL_INF);

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

/* State */
static struct bt_conn *current_conn;
static struct k_work adv_work;
static uint16_t current_mtu = 23;  /* Default BLE ATT MTU */
static ble_data_received_cb_t data_received_callback;

/* Advertising data */
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_SRV_VAL),
};

/* Forward declarations */
static void handle_mtu_update(struct bt_conn *conn);

/* Connection callbacks */
static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (err) {
		LOG_ERR("Connection failed, err 0x%02x", err);
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Connected to %s", addr);

	current_conn = bt_conn_ref(conn);

	/* Check current MTU */
	handle_mtu_update(conn);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Disconnected from %s, reason 0x%02x", addr, reason);

	if (current_conn) {
		bt_conn_unref(current_conn);
		current_conn = NULL;
		current_mtu = 23;
	}
}

static void recycled_cb(void)
{
	LOG_INF("Connection recycled, restarting advertising");
	ble_service_start_advertising();
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		LOG_INF("Security level changed for %s to level %u", addr, level);
		if (level >= BT_SECURITY_L2) {
			LOG_INF("Device %s is authenticated", addr);
			handle_mtu_update(conn);
		}
	} else {
		LOG_WRN("Security failed for %s level %u err %d %s", addr, level,
			err, bt_security_err_to_str(err));
	}
}

static bool le_param_req(struct bt_conn *conn, struct bt_le_conn_param *param)
{
	LOG_DBG("Connection parameter request");
	return true;
}

static void le_param_updated(struct bt_conn *conn, uint16_t interval,
			     uint16_t latency, uint16_t timeout)
{
	LOG_INF("Connection parameters updated: interval=%d, latency=%d, timeout=%d",
		interval, latency, timeout);
}

#if defined(CONFIG_BT_USER_DATA_LEN_UPDATE)
static void le_data_len_updated(struct bt_conn *conn, struct bt_conn_le_data_len_info *info)
{
	LOG_INF("Data length updated: TX max=%d, RX max=%d",
		info->tx_max_len, info->rx_max_len);
}
#endif

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected        = connected,
	.disconnected     = disconnected,
	.recycled         = recycled_cb,
	.security_changed = security_changed,
	.le_param_req     = le_param_req,
	.le_param_updated = le_param_updated,
#if defined(CONFIG_BT_USER_DATA_LEN_UPDATE)
	.le_data_len_updated = le_data_len_updated,
#endif
};

/* MTU management */
static void handle_mtu_update(struct bt_conn *conn)
{
	uint16_t mtu = bt_gatt_get_mtu(conn);

	if (mtu != current_mtu) {
		current_mtu = mtu;
		LOG_INF("MTU updated to %d bytes (payload: %d bytes)", mtu, mtu - 3);
	}
}

static void gatt_mtu_updated_cb(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
	uint16_t mtu = MIN(tx, rx);

	if (mtu != current_mtu) {
		current_mtu = mtu;
		LOG_INF("MTU negotiated to %d bytes (payload: %d bytes)", mtu, mtu - 3);
	}
}

static struct bt_gatt_cb gatt_callbacks = {
	.att_mtu_updated = gatt_mtu_updated_cb,
};

/* NUS callbacks - Zephyr API */
static void bt_receive_cb(struct bt_conn *conn, const void *data, uint16_t len, void *ctx)
{
	/* Check if device is authenticated */
	if (bt_conn_get_security(conn) < BT_SECURITY_L2) {
		LOG_WRN("Rejecting %d bytes from non-authenticated device", len);
		return;
	}

	LOG_DBG("Received %d bytes from authenticated device", len);

	/* Forward to application callback */
	if (data_received_callback) {
		data_received_callback(conn, (const uint8_t *)data, len);
	}
}

static struct bt_nus_cb nus_cb = {
	.received = bt_receive_cb,
	.notif_enabled = NULL,  /* Optional: handle notification enable/disable */
};

/* Advertising work handler */
static void adv_work_handler(struct k_work *work)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad),
				  sd, ARRAY_SIZE(sd));

	if (err) {
		LOG_ERR("Advertising failed to start (err %d)", err);
		return;
	}

	LOG_INF("Advertising started");
}

/* Public API */
int ble_service_init(ble_data_received_cb_t data_cb)
{
	int err;

	data_received_callback = data_cb;

	/* Initialize work queue */
	k_work_init(&adv_work, adv_work_handler);

	/* Register NUS callbacks - Zephyr API uses callback registration instead of init */
	err = bt_nus_cb_register(&nus_cb, NULL);
	if (err) {
		LOG_ERR("Failed to register NUS callbacks: %d", err);
		return err;
	}

	/* Register GATT callbacks */
	bt_gatt_cb_register(&gatt_callbacks);

	LOG_INF("BLE service initialized");
	return 0;
}

int ble_service_start_advertising(void)
{
	k_work_submit(&adv_work);
	return 0;
}

int ble_service_send(const uint8_t *data, uint16_t len)
{
	if (!current_conn || bt_conn_get_security(current_conn) < BT_SECURITY_L2) {
		return -ENOTCONN;
	}

	return bt_nus_send(current_conn, data, len);
}

uint16_t ble_service_get_mtu(void)
{
	return current_mtu;
}

struct bt_conn *ble_service_get_connection(void)
{
	return current_conn;
}

bool ble_service_is_authenticated(void)
{
	return current_conn && (bt_conn_get_security(current_conn) >= BT_SECURITY_L2);
}