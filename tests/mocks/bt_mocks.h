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
