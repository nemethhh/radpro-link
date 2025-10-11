/*
 * SPDX-License-Identifier: MIT
 * BLE Service Module - Header
 */

#ifndef BLE_SERVICE_H
#define BLE_SERVICE_H

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>

/**
 * @brief Callback type for receiving data from BLE
 * @param conn BLE connection
 * @param data Received data buffer
 * @param len Length of received data
 */
typedef void (*ble_data_received_cb_t)(struct bt_conn *conn, const uint8_t *data, uint16_t len);

/**
 * @brief Initialize BLE service
 * @param data_cb Callback for received data
 * @return 0 on success, negative errno on failure
 */
int ble_service_init(ble_data_received_cb_t data_cb);

/**
 * @brief Start BLE advertising
 * @return 0 on success, negative errno on failure
 */
int ble_service_start_advertising(void);

/**
 * @brief Send data over BLE NUS
 * @param data Data buffer to send
 * @param len Length of data
 * @return 0 on success, negative errno on failure
 */
int ble_service_send(const uint8_t *data, uint16_t len);

/**
 * @brief Get current MTU size
 * @return Current MTU (includes 3-byte ATT header)
 */
uint16_t ble_service_get_mtu(void);

/**
 * @brief Get current connection handle
 * @return Connection handle or NULL if not connected
 */
struct bt_conn *ble_service_get_connection(void);

/**
 * @brief Check if connected and authenticated
 * @return true if connected with sufficient security level
 */
bool ble_service_is_authenticated(void);

#endif /* BLE_SERVICE_H */