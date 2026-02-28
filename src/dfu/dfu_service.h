/*
 * SPDX-License-Identifier: MIT
 * DFU Service Module - Header
 *
 * Provides BLE FOTA (Firmware Over-The-Air) update support
 * using MCUmgr SMP (Simple Management Protocol) over BLE.
 *
 * Use nRF Connect Device Manager mobile app to perform updates.
 */

#ifndef DFU_SERVICE_H
#define DFU_SERVICE_H

/**
 * @brief Initialize DFU service
 *
 * Registers MCUmgr SMP command handlers for image and OS management.
 * Must be called after bt_enable().
 *
 * @return 0 on success, negative errno on failure
 */
int dfu_service_init(void);

#endif /* DFU_SERVICE_H */
