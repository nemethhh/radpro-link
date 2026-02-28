# CI Unit Test Job Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a `unit-test` job to `.github/workflows/ci.yml` that runs all Zephyr unit test suites in parallel with the firmware build job.

**Architecture:** A new `unit-test` job runs in the `ghcr.io/zephyrproject-rtos/ci:v0.28.8` container, initialises a Zephyr v4.2.0 workspace (cached between runs), then iterates over every `tests/*/CMakeLists.txt` and runs cmake + ninja + `./build/testbinary`. The `release` job is updated to require both `build` and `unit-test` to pass.

**Tech Stack:** GitHub Actions, Zephyr CI container (`ghcr.io/zephyrproject-rtos/ci:v0.28.8`), west, cmake, ninja, Zephyr unit testing framework.

---

### Task 1: Add `unit-test` job and update `release` dependencies

**Files:**
- Modify: `.github/workflows/ci.yml`

**Step 1: Add the `unit-test` job after the `build` job**

Insert the following block between the closing `---` of the `build` job and the `release:` line:

```yaml
  unit-test:
    name: Unit Tests
    runs-on: ubuntu-latest
    container:
      image: ghcr.io/zephyrproject-rtos/ci:v0.28.8
      options: --user root

    steps:
      - name: Checkout code
        uses: actions/checkout@v4

      - name: Cache Zephyr workspace
        if: github.actor != 'nektos/act'
        uses: actions/cache@v4
        with:
          path: /zephyr-workspace
          key: zephyr-workspace-v4.2.0
          restore-keys: |
            zephyr-workspace-

      - name: Init Zephyr workspace
        run: |
          if [ -d /zephyr-workspace/zephyr ]; then
            echo "Zephyr workspace already initialised (cache hit)"
          else
            echo "Initialising Zephyr v4.2.0 workspace..."
            mkdir -p /zephyr-workspace
            cd /zephyr-workspace
            west init --mr v4.2.0
            west update --narrow --fetch-opt=--depth=1 zephyr
            echo "Zephyr workspace ready"
          fi
        env:
          ZEPHYR_BASE: /zephyr-workspace/zephyr

      - name: Run unit tests
        run: |
          set -e
          export ZEPHYR_BASE=/zephyr-workspace/zephyr
          export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
          export ZEPHYR_SDK_INSTALL_DIR=/opt/toolchains/zephyr-sdk-0.17.4

          PASS=0
          FAIL=0
          FAILED_SUITES=""

          for suite in tests/*/; do
            [ -f "${suite}CMakeLists.txt" ] || continue
            name=$(basename "$suite")
            echo ""
            echo "=== Suite: $name ==="
            cd /github/workspace/"$suite"
            rm -rf build
            if cmake -B build -GNinja -DBOARD=unit_testing 2>&1 && \
               ninja -C build 2>&1 && \
               ./build/testbinary 2>&1; then
              PASS=$((PASS + 1))
            else
              FAIL=$((FAIL + 1))
              FAILED_SUITES="$FAILED_SUITES $name"
              echo "FAILED: $name"
            fi
            cd /github/workspace
          done

          echo ""
          echo "=== RESULTS: $PASS passed, $FAIL failed ==="
          if [ -n "$FAILED_SUITES" ]; then
            echo "Failed suites:$FAILED_SUITES"
          fi
          [ "$FAIL" -eq 0 ]
```

**Step 2: Update `release` job to require both `build` and `unit-test`**

Change:
```yaml
    needs: [build]
```
to:
```yaml
    needs: [build, unit-test]
```

**Step 3: Verify the YAML is valid**

Run:
```bash
python3 -c "import yaml, sys; yaml.safe_load(open('.github/workflows/ci.yml'))" && echo "YAML OK"
```
Expected: `YAML OK`

**Step 4: Commit**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: add unit-test job running Zephyr test suites in parallel with build"
```

---

### Notes

- The container working directory is `/github/workspace` (GitHub Actions default for containers), not `/workspace` as in docker-compose.
- The Zephyr workspace cache key is `zephyr-workspace-v4.2.0` — bump this if the Zephyr version changes.
- The SDK path `/opt/toolchains/zephyr-sdk-0.17.4` is baked into the container image — no installation needed.
- The `if: github.actor != 'nektos/act'` guard on the cache step matches the existing pattern in the `build` job for local `act` testing compatibility.
