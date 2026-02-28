# Docker Build Command Design

**Date**: 2026-02-28
**Topic**: Add `make build` target that builds firmware in a Docker Compose container

## Problem

Building the project currently requires a local PlatformIO installation (`pio run`). There is no containerised build path, unlike unit tests which already run in Docker via `make test`.

## Goal

Add a `make build` target that builds the firmware inside Docker using PlatformIO, with the Seeed platform cached in a Docker volume (so subsequent builds are fast) and build artifacts written to `./build/` on the host.

## Design

### Docker Compose Services

Two new services in `docker-compose.yml`:

**`pio-init`**
Installs the Seeed platform and ARM toolchain into the `pio-packages` named volume. Checks whether the platform is already installed before running (idempotent, same pattern as `zephyr-init`). Uses a pinned `platformio/platformio-core` image for reproducibility.

**`pio-build`**
Depends on `pio-init` completing successfully (`condition: service_completed_successfully`). Runs `pio run` in `/workspace`. Three mounts:
- `.:/workspace` — project source
- `pio-packages:/root/.platformio` — cached packages from `pio-init`
- `./build:/workspace/.pio/build` — bind-mount so artifacts land on the host

One new named volume: `pio-packages`

### Makefile Targets

| Target | Description |
|--------|-------------|
| `build` | Creates `./build/` then runs `$(COMPOSE) run --rm pio-build` (depends on `pio-init`) |
| `pio-init` | Runs `$(COMPOSE) run --rm pio-init` to populate the `pio-packages` volume |
| `build-clean` | Removes `./build/` from the host |
| `pio-clean` | Removes the `pio-packages` Docker volume (forces re-download) |

All four added to `.PHONY` and `help`.

### Build Artifacts

After a successful `make build`, artifacts appear at `./build/seeed-xiao-nrf54l15/`:
- `firmware.elf` — for debugging
- `firmware.bin` / `firmware.hex` — for flashing

`build/` added to `.gitignore`.

### Isolation

`make build` and `make test` are fully independent. `build` depends only on `pio-init`; `test` depends only on `zephyr-init`. The two volumes (`pio-packages`, `zephyr-workspace`) do not interact.

## Out of Scope

- `make flash` / upload via Docker (not requested)
- Merging `pio-init` and `zephyr-init` into a shared init service
