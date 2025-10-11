# SPDX-License-Identifier: MIT
#
# CMakeLists.txt for UART-to-BLE Bridge
# PlatformIO + Zephyr RTOS Build Configuration

# Set board root for Seeed Studio platform
set(BOARD_ROOT "$ENV{ZEPHYR_BASE}/../../platforms/Seeed Studio/zephyr")

cmake_minimum_required(VERSION 3.20.0)

# Set C standard to C11 to fix Zephyr ATT compilation issue
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find Zephyr package
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# Project definition
project(uart_to_ble_bridge
    VERSION 1.0.0
    DESCRIPTION "UART to BLE Bridge for Seeed XIAO nRF54L15"
    LANGUAGES C
)

# Source files - Modular Architecture
target_sources(app PRIVATE
    # Core application
    ../src/main.c

    # BLE module
    ../src/ble/ble_service.c

    # UART module
    ../src/uart/uart_bridge.c

    # Security module
    ../src/security/security_manager.c

    # LED module
    ../src/led/led_status.c

    # Board support
    ../src/board/board_config.c
)

# Include directories
target_include_directories(app PRIVATE
    ../src
    ../src/ble
    ../src/uart
    ../src/security
    ../src/led
    ../src/board
)