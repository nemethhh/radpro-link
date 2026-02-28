.PHONY: test test-suite zephyr-init test-clean build pio-init build-clean pio-clean help

COMPOSE := docker compose

## Run all unit test suites
test: zephyr-init
	$(COMPOSE) run --rm unit-test

## Run a single test suite (usage: make test-suite SUITE=security_manager)
test-suite: zephyr-init
	$(COMPOSE) run --rm -w /workspace/tests/$(SUITE) --entrypoint bash unit-test \
		-c 'set -e; rm -rf build; cmake -B build -GNinja -DBOARD=unit_testing 2>&1; ninja -C build 2>&1; ./build/testbinary'

## Initialize Zephyr workspace (only needed once, cached in Docker volume)
zephyr-init:
	$(COMPOSE) run --rm zephyr-init

## Remove build artifacts from test directories
test-clean:
	$(COMPOSE) run --rm -w /workspace --entrypoint bash unit-test \
		-c 'find tests -name build -type d -exec rm -rf {} + 2>/dev/null; echo "Clean complete"'

## Remove Zephyr workspace volume (forces re-download)
zephyr-clean:
	$(COMPOSE) down -v

## Build firmware in Docker using PlatformIO
build: pio-init
	mkdir -p build
	$(COMPOSE) run --rm pio-build

## Initialize PlatformIO packages (only needed once, cached in Docker volume)
pio-init:
	$(COMPOSE) run --rm pio-init

## Remove firmware build artifacts from host
build-clean:
	rm -rf build

## Remove PlatformIO packages volume (forces re-download; also removes zephyr-workspace)
pio-clean:
	$(COMPOSE) down -v

## Show available targets
help:
	@echo "Usage:"
	@echo "  make test                          Run all unit tests"
	@echo "  make test-suite SUITE=<name>       Run single suite (e.g., security_manager)"
	@echo "  make zephyr-init                   Initialize Zephyr workspace"
	@echo "  make test-clean                    Remove test build artifacts"
	@echo "  make zephyr-clean                  Remove Zephyr Docker volume"
	@echo "  make build                         Build firmware with PlatformIO"
	@echo "  make pio-init                      Initialize PlatformIO packages"
	@echo "  make build-clean                   Remove firmware build artifacts"
	@echo "  make pio-clean                     Remove PlatformIO Docker volume"
