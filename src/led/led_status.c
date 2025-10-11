/*
 * SPDX-License-Identifier: MIT
 * LED Status Module - Implementation
 */

#include "led_status.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(led_status, LOG_LEVEL_INF);

/* User LED GPIO definition - from device tree */
static const struct gpio_dt_spec user_led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

#define FAST_BLINK_INTERVAL_MS 250
#define SLOW_BLINK_INTERVAL_MS 1000
#define PULSE_STEP_MS 50

/* State */
static bool is_connected = false;
static bool pairing_window_active = true;
static bool error_mode = false;

/**
 * @brief Set LED state using device tree-aware API
 * @param state true = LED on, false = LED off
 */
static inline void led_set(bool state)
{
	gpio_pin_set_dt(&user_led, state ? 1 : 0);
}

/* Public API */
int led_status_init(void)
{
	int err;

	LOG_INF("Initializing LED status module");

	/* Verify LED GPIO is ready */
	if (!gpio_is_ready_dt(&user_led)) {
		LOG_ERR("User LED GPIO not ready");
		return -ENODEV;
	}

	/* Configure LED GPIO as output, initially off */
	err = gpio_pin_configure_dt(&user_led, GPIO_OUTPUT_INACTIVE);
	if (err) {
		LOG_ERR("Failed to configure user LED: %d", err);
		return err;
	}

	/* Turn on LED and leave it on to test hardware */
	led_set(true);
	LOG_INF("User LED initialized (P2.0) - LED should be ON now");
	return 0;
}

void led_status_set_connected(bool connected)
{
	is_connected = connected;
}

void led_status_set_pairing_window(bool pairing_active)
{
	pairing_window_active = pairing_active;
}

void led_status_error(void)
{
	error_mode = true;
	LOG_ERR("Entering LED error mode");
}

/**
 * @brief LED status thread
 *
 * Status indication patterns (single LED):
 * - Rapid flash (100ms): Error state
 * - Fast blink (250ms): Pairing window active, not connected
 * - Medium blink (500ms): Pairing window active AND connected
 * - Off: Pairing window closed (regardless of connection state)
 */
static void led_status_thread(void)
{
	bool blink_state = false;
	uint32_t blink_interval_ms;

	LOG_INF("LED status thread started");

	for (;;) {
		/* Error mode: Rapid flash */
		if (error_mode) {
			led_set(true);
			k_msleep(100);
			led_set(false);
			k_msleep(100);
			continue;
		}

		/* LED only active during pairing window */
		if (!pairing_window_active) {
			/* Pairing window closed: LED off */
			led_set(false);
			k_msleep(SLOW_BLINK_INTERVAL_MS);
			continue;
		}

		/* Determine blink pattern based on state (pairing window active) */
		if (is_connected) {
			/* Pairing window active AND connected: medium blink */
			blink_interval_ms = 500;
		} else {
			/* Pairing window active, not connected: fast blink */
			blink_interval_ms = FAST_BLINK_INTERVAL_MS;
		}

		/* Toggle LED */
		blink_state = !blink_state;
		led_set(blink_state);
		k_msleep(blink_interval_ms);
	}
}

K_THREAD_DEFINE(led_status_thread_id, 1024, led_status_thread, NULL, NULL, NULL, 7, 0, 0);

void led_status_start(void)
{
	/* Thread starts automatically with K_THREAD_DEFINE */
	LOG_DBG("LED status thread running");
}