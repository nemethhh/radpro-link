/*
 * SPDX-License-Identifier: MIT
 *
 * RadPro-Link - BLE Bridge for RadPro Radiation Detectors
 * Main Application Entry Point
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>

#include "board/board_config.h"
#include "ble/ble_service.h"
#include "uart/uart_bridge.h"
#include "security/security_manager.h"
#include "led/led_status.h"
#include "dfu/dfu_service.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Configuration */
#define PAIRING_WINDOW_MS (1 * 60 * 1000)  /* 1 minute */

/* Forward declarations */
static void uart_data_handler(const uint8_t *data, uint16_t len);
static void ble_data_handler(struct bt_conn *conn, const uint8_t *data, uint16_t len);

/* Application initialization */
static int app_init(void)
{
	int err;

	LOG_INF("=== RadPro-Link Starting ===");
	LOG_INF("Pairing window: %d minutes", PAIRING_WINDOW_MS / 60000);
	LOG_INF("Device: %s", CONFIG_BT_DEVICE_NAME);

	/* Initialize board-specific hardware */
	LOG_INF("Initializing board hardware");
	err = board_init();
	if (err) {
		LOG_ERR("Board init failed: %d", err);
		return err;
	}
	LOG_INF("Board hardware initialized");

	/* Initialize LED status */
	LOG_INF("Initializing LED status");
	err = led_status_init();
	if (err) {
		LOG_ERR("LED init failed: %d", err);
		return err;
	}
	LOG_INF("LED status initialized");

	/* Initialize security manager */
	LOG_INF("Initializing security manager");
	err = security_manager_init(PAIRING_WINDOW_MS);
	if (err) {
		LOG_ERR("Security manager init failed: %d", err);
		return err;
	}
	LOG_INF("Security manager initialized");

	/* Initialize UART bridge (non-fatal - BLE can work without it) */
	LOG_INF("Initializing UART bridge");
	err = uart_bridge_init(uart_data_handler);
	if (err) {
		LOG_WRN("UART bridge init failed: %d", err);
		LOG_WRN("BLE will work but UART forwarding is disabled");
	} else {
		LOG_INF("UART bridge initialized");
	}

	/* Initialize Bluetooth */
	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed: %d", err);
		return err;
	}

	/* Print device MAC address */
	size_t count = 1;
	bt_addr_le_t addr;
	bt_id_get(&addr, &count);
	if (count > 0) {
		LOG_INF("MAC: %02X:%02X:%02X:%02X:%02X:%02X",
			addr.a.val[5], addr.a.val[4], addr.a.val[3],
			addr.a.val[2], addr.a.val[1], addr.a.val[0]);
	}

	LOG_INF("Bluetooth initialized");

	/* Load settings (bonding info) */
	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
		LOG_INF("Settings loaded");
	}

	/* Initialize BLE service */
	err = ble_service_init(ble_data_handler);
	if (err) {
		LOG_ERR("BLE service init failed: %d", err);
		return err;
	}

	/* Initialize DFU service (MCUmgr SMP) */
	err = dfu_service_init();
	if (err) {
		LOG_ERR("DFU service init failed: %d", err);
		return err;
	}

	/* Start advertising */
	err = ble_service_start_advertising();
	if (err) {
		LOG_ERR("Advertising start failed: %d", err);
		return err;
	}

	LOG_INF("System initialized - ready for connections");
	return 0;
}

/* Data flow handlers */
static void uart_data_handler(const uint8_t *data, uint16_t len)
{
	/* UART → BLE: Forward data from UART to BLE */
	if (ble_service_is_authenticated()) {
		int err = ble_service_send(data, len);
		if (err) {
			LOG_WRN("Failed to send to BLE: %d", err);
		}
	}
}

static void ble_data_handler(struct bt_conn *conn, const uint8_t *data, uint16_t len)
{
	/* BLE → UART: Forward data from BLE to UART */
	int err = uart_bridge_send(data, len);
	if (err) {
		LOG_WRN("Failed to send to UART: %d", err);
	}
}

/* Status monitoring thread */
static void status_monitor_thread(void)
{
	LOG_INF("Status monitor started");

	for (;;) {
		/* Update LED status based on pairing window */
		bool pairing_allowed = security_manager_is_pairing_allowed();
		led_status_set_pairing_window(pairing_allowed);

		/* Update connection status */
		bool connected = ble_service_is_authenticated();
		led_status_set_connected(connected);

		k_sleep(K_SECONDS(1));
	}
}

K_THREAD_DEFINE(status_monitor_id, 1024, status_monitor_thread, NULL, NULL, NULL, 7, 0, 0);

/* Main entry point */
int main(void)
{
	int err = app_init();

	if (err) {
		LOG_ERR("Initialization failed: %d", err);
		led_status_error();
		return err;
	}

	LOG_INF("=== System Running ===");

	/* Main loop - just sleep, threads handle everything */
	while (1) {
		k_sleep(K_FOREVER);
	}

	return 0;
}
