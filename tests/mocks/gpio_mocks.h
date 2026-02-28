/*
 * SPDX-License-Identifier: MIT
 * GPIO API type stubs for unit testing.
 */

#ifndef GPIO_MOCKS_H
#define GPIO_MOCKS_H

/* Block real Zephyr GPIO header */
#define ZEPHYR_INCLUDE_DRIVERS_GPIO_H_

#include <stdint.h>
#include <stdbool.h>

struct device { const char *name; };

typedef uint32_t gpio_flags_t;
#define GPIO_OUTPUT_INACTIVE (1 << 2)

struct gpio_dt_spec {
	const struct device *port;
	uint8_t pin;
	gpio_flags_t dt_flags;
};

/* Stub DT macros — resolved before CUT is included */
#define DT_ALIAS(alias) 0
#define GPIO_DT_SPEC_GET(node_id, prop) \
	{ .port = NULL, .pin = 0, .dt_flags = 0 }

#endif /* GPIO_MOCKS_H */
