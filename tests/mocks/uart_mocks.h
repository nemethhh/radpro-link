/*
 * SPDX-License-Identifier: MIT
 * UART API type stubs for unit testing.
 */

#ifndef UART_MOCKS_H
#define UART_MOCKS_H

/* Block real Zephyr UART/device headers */
#define ZEPHYR_INCLUDE_DRIVERS_UART_H_
#define ZEPHYR_INCLUDE_DEVICE_H_
#define ZEPHYR_INCLUDE_DEVICETREE_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Device type (also used by GPIO) */
#ifndef GPIO_MOCKS_H
struct device { const char *name; };
#endif

/* UART event types */
enum uart_event_type {
	UART_TX_DONE,
	UART_TX_ABORTED,
	UART_RX_RDY,
	UART_RX_BUF_REQUEST,
	UART_RX_BUF_RELEASED,
	UART_RX_DISABLED,
	UART_RX_STOPPED,
};

struct uart_event_tx {
	const uint8_t *buf;
	size_t len;
};

struct uart_event_rx {
	uint8_t *buf;
	size_t offset;
	size_t len;
};

struct uart_event_rx_buf {
	uint8_t *buf;
};

struct uart_event {
	enum uart_event_type type;
	union {
		struct uart_event_tx tx;
		struct uart_event_rx rx;
		struct uart_event_rx_buf rx_buf;
	} data;
};

typedef void (*uart_callback_t)(const struct device *dev,
				struct uart_event *evt,
				void *user_data);

#define SYS_FOREVER_MS (-1)

/* DT macros for UART device selection */
#define DT_HAS_CHOSEN(x) 1
#define DT_CHOSEN(x) 0
#define DT_NODELABEL(x) 0
#define DEVICE_DT_GET(node) (&test_uart_device)

#endif /* UART_MOCKS_H */
