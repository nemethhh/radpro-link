# Docker Build Command Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add `make build` that builds firmware in a Docker container using PlatformIO, with packages cached in a Docker volume and artifacts written to `./build/` on the host.

**Architecture:** Two new Docker Compose services mirror the test pattern: `pio-init` installs the Seeed platform + toolchain into a named volume on first run, `pio-build` runs `pio run` with the packages volume mounted. Makefile targets wrap these services.

**Tech Stack:** Docker Compose, `platformio/platformio-core` image, PlatformIO CLI (`pio pkg install`, `pio run`)

---

### Task 1: Add `pio-init` service to `docker-compose.yml`

**Files:**
- Modify: `docker-compose.yml`

**Context:** `pio-init` installs all packages declared in `platformio.ini` into the `pio-packages` volume. It uses a marker file `/root/.platformio/.initialized` to skip installation on subsequent runs — the same pattern used by `zephyr-init` (which checks for `/zephyr-workspace/zephyr`).

The image `platformio/platformio-core` is the official PlatformIO CLI image. Check https://hub.docker.com/r/platformio/platformio-core/tags and pin to the latest stable tag (e.g. `6.1.16`). The `pio pkg install` command reads `platformio.ini` and installs the Seeed platform from its GitHub URL plus the two `platform_packages` entries.

**Step 1: Add the `pio-init` service block**

In `docker-compose.yml`, add after the `unit-test` service (before the `volumes:` section):

```yaml
  pio-init:
    image: platformio/platformio-core:latest
    volumes:
      - .:/workspace
      - pio-packages:/root/.platformio
    working_dir: /workspace
    entrypoint: ["/bin/sh", "-c"]
    command:
      - |
        if [ -f /root/.platformio/.initialized ]; then
          echo "PlatformIO packages already initialized"
          exit 0
        fi
        echo "Installing PlatformIO packages..."
        pio pkg install
        touch /root/.platformio/.initialized
        echo "PlatformIO packages ready"
```

**Step 2: Add `pio-packages` to the `volumes:` block**

At the bottom of `docker-compose.yml`, the `volumes:` section currently reads:
```yaml
volumes:
  zephyr-workspace:
```

Change it to:
```yaml
volumes:
  zephyr-workspace:
  pio-packages:
```

**Step 3: Verify the config parses**

Run: `docker compose config`
Expected: no errors, `pio-init` service and `pio-packages` volume appear in output.

**Step 4: Commit**

```bash
git add docker-compose.yml
git commit -m "feat: add pio-init service and pio-packages volume to docker-compose"
```

---

### Task 2: Add `pio-build` service to `docker-compose.yml`

**Files:**
- Modify: `docker-compose.yml`

**Context:** `pio-build` depends on `pio-init` completing successfully (same pattern as `unit-test` depends on `zephyr-init`). The bind-mount `./build:/workspace/.pio/build` maps the PlatformIO output directory to the host. **Important:** the host `./build/` directory must exist before Docker creates the bind-mount — if Docker creates it, it will be owned by root. The Makefile handles this with `mkdir -p build` (see Task 3).

**Step 1: Add the `pio-build` service block**

In `docker-compose.yml`, add after the `pio-init` service (before the `volumes:` section):

```yaml
  pio-build:
    image: platformio/platformio-core:latest
    depends_on:
      pio-init:
        condition: service_completed_successfully
    volumes:
      - .:/workspace
      - pio-packages:/root/.platformio
      - ./build:/workspace/.pio/build
    working_dir: /workspace
    entrypoint: ["/bin/sh", "-c"]
    command:
      - pio run
```

**Step 2: Verify the config parses**

Run: `docker compose config`
Expected: no errors, `pio-build` service appears with three volumes and `depends_on: pio-init`.

**Step 3: Commit**

```bash
git add docker-compose.yml
git commit -m "feat: add pio-build service to docker-compose"
```

---

### Task 3: Add Makefile targets

**Files:**
- Modify: `Makefile`

**Context:** Four new targets following the exact same style as the existing ones. `build` depends on `pio-init` at the Makefile level (mirroring `test: zephyr-init`) even though docker-compose already enforces the dependency — this makes `make build` self-contained and the intent explicit. The `mkdir -p build` ensures the host directory exists before Docker creates the bind-mount.

`pio-clean` uses `$(COMPOSE) down -v` (same as `zephyr-clean`), which removes all named volumes. This is a documented trade-off: it also removes `zephyr-workspace`. Users needing surgical cleanup can run `docker volume rm <project>_pio-packages` directly.

**Step 1: Update `.PHONY` line**

Change:
```makefile
.PHONY: test test-suite zephyr-init test-clean help
```
To:
```makefile
.PHONY: test test-suite zephyr-init test-clean build pio-init build-clean pio-clean help
```

**Step 2: Add the four new targets**

After the `zephyr-clean` target, add:

```makefile
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
```

**Step 3: Update `help` target**

Add these lines to the `help` target's echo block:

```makefile
	@echo "  make build                         Build firmware with PlatformIO"
	@echo "  make pio-init                      Initialize PlatformIO packages"
	@echo "  make build-clean                   Remove firmware build artifacts"
	@echo "  make pio-clean                     Remove PlatformIO Docker volume"
```

**Step 4: Verify help output**

Run: `make help`
Expected: all eight targets listed without errors.

**Step 5: Commit**

```bash
git add Makefile
git commit -m "feat: add build, pio-init, build-clean, pio-clean make targets"
```

---

### Task 4: End-to-end verification

**Context:** Run the full flow to confirm everything works together.

**Step 1: Initialize PlatformIO packages**

Run: `make pio-init`
Expected: Downloads Seeed platform from GitHub, ARM toolchain, and Zephyr framework into the `pio-packages` volume. Ends with `PlatformIO packages ready`. This will take several minutes on first run.

**Step 2: Build the firmware**

Run: `make build`
Expected:
- `pio-init` prints `PlatformIO packages already initialized` (fast, uses cached volume)
- `pio run` compiles the firmware
- Final output contains `[SUCCESS]` or similar PlatformIO success message

**Step 3: Verify artifacts on host**

Run: `ls build/seeed-xiao-nrf54l15/`
Expected: directory contains `firmware.elf` and at least one of `firmware.bin` or `firmware.hex`.

**Step 4: Verify build-clean**

Run: `make build-clean && ls build 2>&1`
Expected: `ls: cannot access 'build': No such file or directory`

**Step 5: Commit**

No code changes — this task is verification only. If any step fails, fix the issue before proceeding.

---

### Task 5: Final commit and cleanup

**Step 1: Verify full state**

Run: `git status`
Expected: clean working tree (all changes committed in Tasks 1–3).

**Step 2: Run a second build to confirm cache works**

Run: `make build`
Expected: `pio-init` is instant (`PlatformIO packages already initialized`), build completes without re-downloading packages.
