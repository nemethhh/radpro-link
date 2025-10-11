/*
 * SPDX-License-Identifier: MIT
 *
 * XIAO BLE-UART Bridge - Modular Architecture
 * Main Application Entry Point
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>

#include "board/board_config.h"
#include "ble/ble_service.h"
#include "uart/uart_bridge.h"
#include "security/security_manager.h"
#include "led/led_status.h"

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

	/* Use printk for early logging before log subsystem is ready */
	printk("\n=== XIAO BLE Bridge Starting ===\n");
	printk("Modular Architecture\n");
	printk("Pairing window: %d minutes\n", PAIRING_WINDOW_MS / 60000);
	printk("Device: %s\n", CONFIG_BT_DEVICE_NAME);

	/* Initialize board-specific hardware */
	printk("Initializing board hardware...\n");
	err = board_init();
	if (err) {
		printk("ERROR: Board init failed: %d\n", err);
		return err;
	}
	printk("Board hardware initialized\n");

	/* Initialize LED status */
	printk("Initializing LED status...\n");
	err = led_status_init();
	if (err) {
		printk("ERROR: LED init failed: %d\n", err);
		return err;
	}
	printk("LED status initialized\n");

	/* Initialize security manager */
	printk("Initializing security manager...\n");
	err = security_manager_init(PAIRING_WINDOW_MS);
	if (err) {
		printk("ERROR: Security manager init failed: %d\n", err);
		return err;
	}
	printk("Security manager initialized\n");

	/* Initialize UART bridge (non-fatal - BLE can work without it) */
	printk("Initializing UART bridge...\n");
	err = uart_bridge_init(uart_data_handler);
	if (err) {
		printk("WARNING: UART bridge init failed: %d\n", err);
		printk("BLE will work but UART forwarding is disabled\n");
	} else {
		printk("UART bridge initialized\n");
	}

	/* Initialize Bluetooth */
	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed: %d\n", err);
		return err;
	}

	/* Print device MAC address */
	size_t count = 1;
	bt_addr_le_t addr;
	bt_id_get(&addr, &count);
	if (count > 0) {
		printk("MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
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
		printk("BLE service init failed: %d\n", err);
		return err;
	}

	/* Start advertising */
	err = ble_service_start_advertising();
	if (err) {
		printk("Advertising start failed: %d\n", err);
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
		printk("Initialization failed: %d\n", err);
		led_status_error();
		return err;
	}

	printk("=== System Running ===\n");

	/* Main loop - just sleep, threads handle everything */
	while (1) {
		k_sleep(K_FOREVER);
	}

	return 0;
}
