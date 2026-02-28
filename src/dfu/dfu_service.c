/*
 * SPDX-License-Identifier: MIT
 * DFU Service Module - Implementation
 *
 * MCUmgr SMP server over BLE for firmware updates.
 * When CONFIG_MCUMGR is enabled, Zephyr automatically registers
 * the SMP BLE transport and command handlers via Kconfig.
 *
 * This module exists as an explicit initialization point and
 * future extension hook for DFU-related logic.
 */

#include "dfu_service.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(dfu_service, LOG_LEVEL_INF);

int dfu_service_init(void)
{
#ifdef CONFIG_MCUMGR
	LOG_INF("DFU service initialized (MCUmgr SMP over BLE)");
	LOG_INF("Use nRF Connect Device Manager app for firmware updates");
#else
	LOG_WRN("DFU service disabled (CONFIG_MCUMGR not set)");
#endif
	return 0;
}
