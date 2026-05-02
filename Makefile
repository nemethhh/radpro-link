.PHONY: build pio-init build-clean pio-clean flash-build \
        test test-suite zephyr-init test-clean zephyr-clean \
        probe flash flash-jlink erase reset verify \
        rtt gdb-server gdb monitor \
        ble-scan radpro-test \
        help

COMPOSE       := docker compose
PYOCD_TARGET  := nrf54l

# Firmware artifact - populated after 'make build'
FIRMWARE_HEX  := $(firstword $(wildcard \
    build/seeed-xiao-nrf54l15/firmware.hex \
    build/seeed-xiao-nrf54l15/zephyr/zephyr.hex \
    .pio/build/seeed-xiao-nrf54l15/firmware.hex \
    .pio/build/seeed-xiao-nrf54l15/zephyr/zephyr.hex))
FIRMWARE_ELF  := $(firstword $(wildcard \
    build/seeed-xiao-nrf54l15/firmware.elf \
    build/seeed-xiao-nrf54l15/zephyr/zephyr.elf \
    .pio/build/seeed-xiao-nrf54l15/firmware.elf \
    .pio/build/seeed-xiao-nrf54l15/zephyr/zephyr.elf))

# Optional probe UID when multiple probes are connected.
# Example: make flash PROBE=8ABD0345
PROBE        ?=
PROBE_FLAG   := $(if $(PROBE),-u $(PROBE))

# Serial port for USB-UART monitor (CMSIS-DAP UART bridge → UART20 on nRF54L15)
PORT         ?= /dev/ttyACM1

# ─── Build ────────────────────────────────────────────────────────────────────

## Build firmware in Docker using PlatformIO
build: pio-init
	mkdir -p build
	$(COMPOSE) run --rm pio-build
	@echo ""
	@BUILT=$$(ls build/seeed-xiao-nrf54l15/zephyr/zephyr.hex 2>/dev/null \
	          || ls .pio/build/seeed-xiao-nrf54l15/zephyr/zephyr.hex 2>/dev/null); \
	if [ -n "$$BUILT" ]; then \
		echo "Firmware: $$BUILT"; \
		ls -lh "$$BUILT"; \
	else \
		echo "Warning: firmware hex not found after build"; \
	fi

## Initialize PlatformIO packages (cached in Docker volume, run once)
pio-init:
	$(COMPOSE) run --rm pio-init

## Remove firmware build artifacts from host
build-clean:
	rm -rf build .pio/build

## Remove PlatformIO + Zephyr Docker volumes (forces full re-download)
pio-clean:
	$(COMPOSE) down -v

## Build firmware then flash it in one step
flash-build: build flash

# ─── Unit Tests ───────────────────────────────────────────────────────────────

## Run all unit test suites
test: zephyr-init
	$(COMPOSE) run --rm unit-test

## Run a single unit test suite: make test-suite SUITE=security_manager
test-suite: zephyr-init
	$(COMPOSE) run --rm -w /workspace/tests/$(SUITE) --entrypoint bash unit-test \
		-c 'set -e; rm -rf build; cmake -B build -GNinja -DBOARD=unit_testing 2>&1; ninja -C build 2>&1; ./build/testbinary'

## Initialize Zephyr workspace (cached in Docker volume, run once)
zephyr-init:
	$(COMPOSE) run --rm zephyr-init

## Remove test build artifacts
test-clean:
	$(COMPOSE) run --rm -w /workspace --entrypoint bash unit-test \
		-c 'find tests -name build -type d -exec rm -rf {} + 2>/dev/null; echo "Clean complete"'

## Remove Zephyr workspace Docker volume (forces re-download)
zephyr-clean:
	$(COMPOSE) down -v

# ─── Probe / Flash ────────────────────────────────────────────────────────────

## List all connected CMSIS-DAP debug probes
probe:
	pyocd list

## Flash firmware to device via pyocd (CMSIS-DAP)
## Set PROBE=<UID> when multiple probes are connected, e.g.: make flash PROBE=8ABD0345
flash:
	@if [ -z "$(FIRMWARE_HEX)" ]; then \
		echo "Error: no firmware found. Run 'make build' first."; \
		echo "Expected: build/seeed-xiao-nrf54l15/zephyr/zephyr.hex"; \
		exit 1; \
	fi
	@echo "Flashing: $(FIRMWARE_HEX)"
	pyocd load -t $(PYOCD_TARGET) $(PROBE_FLAG) --erase chip $(FIRMWARE_HEX)

## Flash firmware via JLink (alternative when pyocd has issues)
flash-jlink:
	@if [ -z "$(FIRMWARE_HEX)" ]; then \
		echo "Error: no firmware found. Run 'make build' first."; \
		exit 1; \
	fi
	@printf 'loadfile %s\nreset\nexit\n' "$(FIRMWARE_HEX)" | \
		JLinkExe -device nRF54L15_M33 -if SWD -speed 4000 -autoconnect 1

## Erase the entire chip flash
erase:
	pyocd erase -t $(PYOCD_TARGET) $(PROBE_FLAG) --chip

## Reset the device (runs firmware from the start, no reflash)
reset:
	pyocd reset -t $(PYOCD_TARGET) $(PROBE_FLAG)

## Verify flash content matches firmware file
verify:
	@if [ -z "$(FIRMWARE_HEX)" ]; then \
		echo "Error: no firmware found."; \
		exit 1; \
	fi
	pyocd load -t $(PYOCD_TARGET) $(PROBE_FLAG) --trust-crc --dry-run $(FIRMWARE_HEX)

# ─── Debug / Console ─────────────────────────────────────────────────────────

## Connect to RTT console (requires firmware built with CONFIG_USE_SEGGER_RTT=y)
rtt:
	pyocd rtt -t $(PYOCD_TARGET) $(PROBE_FLAG)

## Start pyOCD GDB server on port 3333 (run in a separate terminal)
gdb-server:
	pyocd gdbserver -t $(PYOCD_TARGET) $(PROBE_FLAG) -p 3333

## Connect ARM GDB to a running GDB server (requires 'make gdb-server' in another terminal)
gdb:
	@if [ -z "$(FIRMWARE_ELF)" ]; then \
		echo "Warning: no ELF found, GDB will have no symbols"; \
		arm-none-eabi-gdb -ex "target remote :3333"; \
	else \
		arm-none-eabi-gdb -ex "target remote :3333" $(FIRMWARE_ELF); \
	fi

## Serial monitor on the CMSIS-DAP UART bridge (PORT=/dev/ttyACM1 by default)
## This is UART20 on the nRF54L15; useful if firmware writes to that UART
monitor:
	python3 -m serial.tools.miniterm --raw $(PORT) 115200

# ─── BLE Testing ─────────────────────────────────────────────────────────────

## Scan for BLE devices and highlight RadPro-Link
ble-scan:
	python3 scripts/ble_scan.py

## Connect to RadPro-Link via BLE and send RadPro protocol test commands
radpro-test:
	python3 scripts/radpro_test.py

# ─── Help ─────────────────────────────────────────────────────────────────────

help:
	@echo ""
	@echo "RadPro-Link — available make targets"
	@echo ""
	@echo "  Build"
	@echo "    build              Build firmware with PlatformIO (Docker)"
	@echo "    pio-init           Initialize PlatformIO packages (once)"
	@echo "    build-clean        Remove firmware build artifacts"
	@echo "    pio-clean          Remove Docker volumes (full re-download)"
	@echo "    flash-build        Build then flash in one step"
	@echo ""
	@echo "  Unit tests"
	@echo "    test               Run all unit test suites"
	@echo "    test-suite SUITE=X Run a single suite (e.g. security_manager)"
	@echo "    zephyr-init        Initialize Zephyr workspace (once)"
	@echo "    test-clean         Remove test build artifacts"
	@echo "    zephyr-clean       Remove Zephyr Docker volume"
	@echo ""
	@echo "  Probe / Flash"
	@echo "    probe              List connected debug probes"
	@echo "    flash              Flash firmware via pyocd (CMSIS-DAP)"
	@echo "    flash-jlink        Flash firmware via JLink"
	@echo "    erase              Erase entire chip flash"
	@echo "    reset              Reset device without reflashing"
	@echo "    verify             Verify flash matches firmware file"
	@echo ""
	@echo "  Debug / Console"
	@echo "    rtt                Connect to RTT console (needs RTT in firmware)"
	@echo "    gdb-server         Start GDB server on :3333"
	@echo "    gdb                Connect GDB to running GDB server"
	@echo "    monitor            Serial monitor on USB-UART bridge (UART20)"
	@echo ""
	@echo "  BLE Testing"
	@echo "    ble-scan           Scan for BLE devices"
	@echo "    radpro-test        BLE RadPro protocol command test"
	@echo ""
	@echo "  Variables"
	@echo "    PROBE=<UID>        Probe UID for multi-probe setups"
	@echo "    PORT=<dev>         Serial device for monitor (default: /dev/ttyACM1)"
	@echo ""
	@echo "  Connected probes:"
	@pyocd list 2>/dev/null | grep -v "^$$" || echo "    (none)"
	@echo ""
