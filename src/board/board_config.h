/*
 * SPDX-License-Identifier: MIT
 * Board Configuration Abstraction - Header
 */

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include <stdbool.h>

/**
 * @brief Board-specific configuration
 */
struct board_config {
	const char *name;
	bool has_usb;
	bool needs_async_adapter;
};

/**
 * @brief Get board configuration
 * @return Pointer to board configuration
 */
const struct board_config *board_get_config(void);

/**
 * @brief Initialize board-specific hardware
 * @return 0 on success, negative errno on failure
 */
int board_init(void);

#endif /* BOARD_CONFIG_H */