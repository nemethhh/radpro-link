/*
 * SPDX-License-Identifier: MIT
 * Board Configuration Abstraction - Implementation
 */

#include "board_config.h"

#include <zephyr/usb/usb_device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(board_config, LOG_LEVEL_INF);

/* Board configurations */
#if defined(CONFIG_BOARD_XIAO_BLE)
static const struct board_config xiao_nrf52840_config = {
	.name = "XIAO nRF52840",
	.has_usb = true,
	.needs_async_adapter = true,
};
#elif defined(CONFIG_BOARD_XIAO_NRF54L15)
static const struct board_config xiao_nrf54l15_config = {
	.name = "XIAO nRF54L15",
	.has_usb = false,  /* nRF54L15 has no USB hardware */
	.needs_async_adapter = false,  /* nRF54L has native async UART */
};
#elif defined(CONFIG_BOARD_NRF54L15DK_NRF54L15_CPUAPP)
static const struct board_config nrf54l15dk_config = {
	.name = "nRF54L15DK (with XIAO overlay)",
	.has_usb = false,  /* nRF54L15 has no USB hardware */
	.needs_async_adapter = false,  /* nRF54L has native async UART */
};
#else
#error "Unsupported board"
#endif

const struct board_config *board_get_config(void)
{
#if defined(CONFIG_BOARD_XIAO_BLE)
	return &xiao_nrf52840_config;
#elif defined(CONFIG_BOARD_XIAO_NRF54L15)
	return &xiao_nrf54l15_config;
#elif defined(CONFIG_BOARD_NRF54L15DK_NRF54L15_CPUAPP)
	return &nrf54l15dk_config;
#else
	return NULL;
#endif
}

int board_init(void)
{
	const struct board_config *config = board_get_config();
	int err = 0;

	if (!config) {
		LOG_ERR("Unknown board configuration");
		return -ENODEV;
	}

	LOG_INF("Initializing board: %s", config->name);

	/* Initialize USB if available */
	if (config->has_usb && IS_ENABLED(CONFIG_USB_DEVICE_STACK)) {
		LOG_INF("Enabling USB");
		err = usb_enable(NULL);
		if (err && (err != -EALREADY)) {
			LOG_WRN("USB not available: %d", err);
		} else {
			LOG_INF("USB enabled");
			/* Wait for USB enumeration */
			k_sleep(K_MSEC(1000));
		}
	}

	LOG_INF("Board initialized: %s", config->name);
	return 0;
}