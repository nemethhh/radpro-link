/*
 * SPDX-License-Identifier: MIT
 * Security Manager Module - Header
 */

#ifndef SECURITY_MANAGER_H
#define SECURITY_MANAGER_H

#include <zephyr/bluetooth/conn.h>

/**
 * @brief Initialize security manager
 * @param pairing_window_ms Time window for automatic pairing (milliseconds)
 * @return 0 on success, negative errno on failure
 */
int security_manager_init(uint32_t pairing_window_ms);

/**
 * @brief Check if pairing is currently allowed
 * @return true if within pairing window
 */
bool security_manager_is_pairing_allowed(void);

/**
 * @brief Get remaining pairing window time
 * @return Remaining time in milliseconds, 0 if window closed
 */
uint32_t security_manager_get_pairing_time_remaining(void);

#endif /* SECURITY_MANAGER_H */