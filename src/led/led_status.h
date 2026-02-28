/*
 * SPDX-License-Identifier: MIT
 * LED Status Module - Header
 */

#ifndef LED_STATUS_H
#define LED_STATUS_H

#include <stdbool.h>

/**
 * @brief LED status patterns (single user LED)
 * - Rapid flash (100ms): Error state
 * - Fast blink (250ms): Pairing window active, not connected
 * - Medium blink (500ms): Pairing window active AND connected
 * - Off: Pairing window closed (regardless of connection state)
 */

/**
 * @brief Initialize LED status module
 * @return 0 on success, negative errno on failure
 */
int led_status_init(void);

/**
 * @brief Set connection status
 * @param connected true if BLE connected
 */
void led_status_set_connected(bool connected);

/**
 * @brief Set pairing window status
 * @param pairing_active true if in pairing window
 */
void led_status_set_pairing_window(bool pairing_active);

/**
 * @brief Indicate error condition
 */
void led_status_error(void);

#endif /* LED_STATUS_H */