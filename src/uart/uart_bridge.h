/*
 * SPDX-License-Identifier: MIT
 * UART Bridge Module - Header
 */

#ifndef UART_BRIDGE_H
#define UART_BRIDGE_H

#include <zephyr/types.h>

/**
 * @brief Callback type for UART data received
 * @param data Data buffer
 * @param len Length of data
 */
typedef void (*uart_data_received_cb_t)(const uint8_t *data, uint16_t len);

/**
 * @brief Initialize UART bridge
 * @param data_cb Callback for received UART data
 * @return 0 on success, negative errno on failure
 */
int uart_bridge_init(uart_data_received_cb_t data_cb);

/**
 * @brief Send data to UART
 * @param data Data buffer to send
 * @param len Length of data
 * @return 0 on success, negative errno on failure
 */
int uart_bridge_send(const uint8_t *data, uint16_t len);

#endif /* UART_BRIDGE_H */