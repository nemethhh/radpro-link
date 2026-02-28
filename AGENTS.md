# Repository Guidelines

## Project Structure & Module Organization
`radpro-link` is a PlatformIO + Zephyr firmware project for XIAO nRF54L15.

- `src/`: application code, organized by module:
  - `ble/`, `uart/`, `security/`, `led/`, `board/`, `dfu/`, plus `main.c`
- `zephyr/`: Zephyr app config (`prj.conf`), build wiring (`CMakeLists.txt`), board overlay/config.
- `docs/`: reference notes and plans. `docs/platform-seeedboards/` is a vendored platform snapshot; avoid unrelated edits there.
- `.github/workflows/ci.yml`: CI build/release pipeline.

## Build, Test, and Development Commands
Run from repository root:

- `pio run`: build firmware (same command CI uses).
- `pio run -t upload`: flash firmware to connected target.
- `pio run -t clean`: clean build output.
- `pio run -v`: verbose build for troubleshooting.

Testing:
- Unit-test flow is intended via Zephyr Twister (`platformio.ini` comments):
  - `cd test && twister -p native_sim -T unit/`
- Note: `test/` is not currently present; add it when introducing tests.

## Coding Style & Naming Conventions
- Follow `.editorconfig`:
  - C/C++: tabs, tab width 8, max line length 120.
  - CMake/Kconfig/INI/DTS overlays: spaces (4).
  - Markdown: keep lines readable (80 preferred).
- Use C11 (`-std=gnu11`) and Zephyr logging (`LOG_INF/WRN/ERR/DBG`) instead of `printf`.
- Naming: module-based files (`<module>/<module>_*.c|.h`), clear public API in headers, `snake_case` functions/macros consistent with existing code.

## Testing Guidelines
- Validate every change with at least `pio run`.
- For behavior changes, include manual verification notes (board, connection flow, expected logs/LED pattern).
- When adding tests, place them under `test/unit/` and keep names descriptive (example: `test_ble_security_*`).

## Commit & Pull Request Guidelines
- Prefer Conventional Commit style used in history: `feat:`, `fix:`, `refactor:`, `docs:`, `chore:`.
- Keep commits focused and explain why, not just what.
- PRs should include:
  - concise summary,
  - linked issue (if any),
  - build status (`pio run` result),
  - hardware validation notes for firmware behavior changes.
