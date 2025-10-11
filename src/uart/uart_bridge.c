/*
 * SPDX-License-Identifier: MIT
 * UART Bridge Module - Implementation
 */

#include "uart_bridge.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(uart_bridge, LOG_LEVEL_INF);

#define UART_BUF_SIZE 40
#define UART_WAIT_FOR_BUF_DELAY K_MSEC(50)
#define UART_WAIT_FOR_RX 50

struct uart_data_t {
	void *fifo_reserved;
	uint8_t data[UART_BUF_SIZE];
	uint16_t len;
};

/* State */
static const struct device *uart;
static struct k_work_delayable uart_work;
static K_FIFO_DEFINE(fifo_uart_tx_data);
static K_FIFO_DEFINE(fifo_uart_rx_data);
static uart_data_received_cb_t data_received_callback;
static uint32_t uart_log_count = 0;
static bool uart_initialized = false;

/* Get UART device from device tree chosen node */
static const struct device *get_uart_device(void)
{
	/* Use the UART specified in device tree chosen node "app-bridge-uart" */
#if DT_HAS_CHOSEN(app_bridge_uart)
	return DEVICE_DT_GET(DT_CHOSEN(app_bridge_uart));
#else
	/* Fallback: directly use uart21 */
	return DEVICE_DT_GET(DT_NODELABEL(uart21));
#endif
}

/* nRF54L15 has native async UART support - no adapter needed */

/* UART callback */
static void uart_cb(const struct device *dev, struct uart_event *evt, void *user_data)
{
	ARG_UNUSED(dev);

	static size_t aborted_len;
	struct uart_data_t *buf;
	static uint8_t *aborted_buf;
	static bool disable_req;

	switch (evt->type) {
	case UART_TX_DONE:
		LOG_DBG("TX done");
		if ((evt->data.tx.len == 0) || (!evt->data.tx.buf)) {
			return;
		}

		if (aborted_buf) {
			buf = CONTAINER_OF(aborted_buf, struct uart_data_t, data[0]);
			aborted_buf = NULL;
			aborted_len = 0;
		} else {
			buf = CONTAINER_OF(evt->data.tx.buf, struct uart_data_t, data[0]);
		}

		k_free(buf);

		buf = k_fifo_get(&fifo_uart_tx_data, K_NO_WAIT);
		if (!buf) {
			return;
		}

		if (uart_tx(uart, buf->data, buf->len, SYS_FOREVER_MS)) {
			LOG_WRN("Failed to send data");
		}
		break;

	case UART_RX_RDY:
		LOG_DBG("RX ready");
		buf = CONTAINER_OF(evt->data.rx.buf, struct uart_data_t, data[0]);
		buf->len += evt->data.rx.len;

		if (disable_req) {
			return;
		}

		/* Log occasionally to prevent spam */
		if ((++uart_log_count % 100) == 0) {
			LOG_INF("Received %d bytes (count: %u)", evt->data.rx.len, uart_log_count);
		}

		if ((evt->data.rx.buf[buf->len - 1] == '\n') ||
		    (evt->data.rx.buf[buf->len - 1] == '\r')) {
			disable_req = true;
			uart_rx_disable(uart);
		}
		break;

	case UART_RX_DISABLED:
		LOG_DBG("RX disabled");
		disable_req = false;

		buf = k_malloc(sizeof(*buf));
		if (buf) {
			buf->len = 0;
		} else {
			LOG_WRN("Failed to allocate RX buffer");
			k_work_reschedule(&uart_work, UART_WAIT_FOR_BUF_DELAY);
			return;
		}

		uart_rx_enable(uart, buf->data, sizeof(buf->data), UART_WAIT_FOR_RX);
		break;

	case UART_RX_BUF_REQUEST:
		LOG_DBG("RX buffer request");
		buf = k_malloc(sizeof(*buf));
		if (buf) {
			buf->len = 0;
			uart_rx_buf_rsp(uart, buf->data, sizeof(buf->data));
		} else {
			LOG_WRN("Failed to allocate RX buffer");
		}
		break;

	case UART_RX_BUF_RELEASED:
		LOG_DBG("RX buffer released");
		buf = CONTAINER_OF(evt->data.rx_buf.buf, struct uart_data_t, data[0]);

		if (buf->len > 0) {
			LOG_DBG("Queuing %d bytes for callback", buf->len);
			k_fifo_put(&fifo_uart_rx_data, buf);
		} else {
			k_free(buf);
		}
		break;

	case UART_TX_ABORTED:
		LOG_DBG("TX aborted");
		if (!aborted_buf) {
			aborted_buf = (uint8_t *)evt->data.tx.buf;
		}

		aborted_len += evt->data.tx.len;
		buf = CONTAINER_OF((void *)aborted_buf, struct uart_data_t, data);

		uart_tx(uart, &buf->data[aborted_len], buf->len - aborted_len, SYS_FOREVER_MS);
		break;

	default:
		break;
	}
}

/* Work handler */
static void uart_work_handler(struct k_work *item)
{
	struct uart_data_t *buf;

	buf = k_malloc(sizeof(*buf));
	if (buf) {
		buf->len = 0;
	} else {
		LOG_WRN("Failed to allocate UART receive buffer");
		k_work_reschedule(&uart_work, UART_WAIT_FOR_BUF_DELAY);
		return;
	}

	uart_rx_enable(uart, buf->data, sizeof(buf->data), UART_WAIT_FOR_RX);
}

/* RX processing thread */
static void uart_rx_thread(void)
{
	LOG_INF("UART RX thread started");

	for (;;) {
		struct uart_data_t *buf = k_fifo_get(&fifo_uart_rx_data, K_FOREVER);

		if (!buf) {
			LOG_ERR("Received NULL buffer");
			continue;
		}

		/* Forward to application callback */
		if (data_received_callback && buf->len > 0) {
			data_received_callback(buf->data, buf->len);
		}

		k_free(buf);
	}
}

K_THREAD_DEFINE(uart_rx_thread_id, 2048, uart_rx_thread, NULL, NULL, NULL, 7, 0, 0);

/* Public API */
int uart_bridge_init(uart_data_received_cb_t data_cb)
{
	int err;
	struct uart_data_t *rx;
	struct uart_data_t *tx;

	data_received_callback = data_cb;

	/* Get UART device */
	uart = get_uart_device();
	if (!device_is_ready(uart)) {
		LOG_ERR("UART device not ready");
		return -ENODEV;
	}

	LOG_INF("UART device ready");

	/* Allocate initial RX buffer */
	rx = k_malloc(sizeof(*rx));
	if (!rx) {
		LOG_ERR("Failed to allocate RX buffer");
		return -ENOMEM;
	}
	rx->len = 0;

	/* Initialize work queue */
	k_work_init_delayable(&uart_work, uart_work_handler);

	/* nRF54L15 has native async UART - no adapter needed */

	/* Set callback */
	err = uart_callback_set(uart, uart_cb, NULL);
	if (err) {
		LOG_ERR("Failed to set UART callback: %d", err);
		k_free(rx);
		return err;
	}

	/* Send welcome message */
	tx = k_malloc(sizeof(*tx));
	if (tx) {
		tx->len = snprintf(tx->data, sizeof(tx->data), "BLE Bridge Ready\r\n");
		if (tx->len > 0 && tx->len < sizeof(tx->data)) {
			err = uart_tx(uart, tx->data, tx->len, SYS_FOREVER_MS);
			if (err) {
				LOG_WRN("Failed to send welcome message: %d", err);
				k_free(tx);
			}
		} else {
			k_free(tx);
		}
	}

	/* Enable RX */
	err = uart_rx_enable(uart, rx->data, sizeof(rx->data), UART_WAIT_FOR_RX);
	if (err) {
		LOG_ERR("Failed to enable RX: %d", err);
		k_free(rx);
		return err;
	}

	uart_initialized = true;
	LOG_INF("UART bridge initialized");
	return 0;
}

int uart_bridge_send(const uint8_t *data, uint16_t len)
{
	int err;

	/* Check if UART is initialized */
	if (!uart_initialized) {
		LOG_DBG("UART not initialized, discarding %d bytes", len);
		return -ENODEV;
	}

	for (uint16_t pos = 0; pos < len;) {
		struct uart_data_t *tx = k_malloc(sizeof(*tx));

		if (!tx) {
			LOG_ERR("Failed to allocate TX buffer");
			k_sleep(K_MSEC(10));
			return -ENOMEM;
		}

		/* Reserve last byte for LF */
		size_t tx_data_size = sizeof(tx->data) - 1;

		if ((len - pos) > tx_data_size) {
			tx->len = tx_data_size;
		} else {
			tx->len = (len - pos);
		}

		memcpy(tx->data, &data[pos], tx->len);
		pos += tx->len;

		/* Append LF if CR triggered transmission */
		if ((pos == len) && (data[len - 1] == '\r')) {
			tx->data[tx->len] = '\n';
			tx->len++;
		}

		err = uart_tx(uart, tx->data, tx->len, SYS_FOREVER_MS);
		if (err) {
			k_fifo_put(&fifo_uart_tx_data, tx);
		}
	}

	return 0;
}