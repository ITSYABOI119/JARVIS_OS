# Repository Guidelines

## Project Structure & Module Organization
Root documentation tracks plans and status (for example `README.md`, `CLAUDE.md`, and phase kickoff/spec files). Implementation is phase-based: `phase1/src/` hosts the QEMU/seL4 proof of concept (C and Python), while `phase2/src/` targets Raspberry Pi 4 with UART IPC. Hardware boot artifacts live in `phase2/firmware/` (`kernel8.img`, `config.txt`, `start4.elf`, `fixup4.dat`). Build/setup scripts are in `phase1/scripts/` and `phase2/scripts/`. Historical experiments are under `phase0/` and `archive/`.

## Build, Test, and Development Commands
- WSL setup: `./setup_wsl_dependencies.sh`.
- Full test run (WSL): `./run_all_tests_wsl.sh`.
- Phase 1 build (QEMU/seL4): `cd phase1 && mkdir -p build && cd build && cmake -G Ninja ../src && ninja` then `./simulate` (or `../scripts/launch-jarvis-qemu.sh`).
- Phase 2 Pi 4 build (WSL cross-compile): `cd phase2/scripts && ./build_and_copy_kernel.sh` (writes `phase2/firmware/kernel8.img`).
- Sample tests: `python3 phase2/src/ai/test_uart_ipc_client.py` or `gcc -O2 phase2/src/ipc/test_dual_ring.c ...`.

## Coding Style & Naming Conventions
C follows Linux kernel style (tabs for indentation, braces on the same line). Python follows PEP 8 (4 spaces). Tests use `test_*.c` and `test_*.py` next to the modules they cover; weekly status docs follow `phase2/weeks/week32/WEEK_32_STATUS.md` patterns. There is no repo-wide formatter config, so match the surrounding file.

## Testing Guidelines
Run the component tests closest to your changes first, then integration tests in `phase1/src/integration_tests/`. For Pi 4 work, validate boot artifacts in `phase2/firmware/` and update `phase2/docs/SD_CARD_SETUP.md` or `phase2/docs/TROUBLESHOOTING.md` when instructions change.

## Commit & Pull Request Guidelines
Recent commits use short, descriptive subjects such as `Week 32: ARM64 build complete`, `Update CLAUDE.md: Week 32 completion`, or `Upgrade IDLE model: TinyLlama 1.1B -> Llama 3.2 1B`. Include the week number for weekly deliverables and highlight the primary outcome. PRs should summarize scope, list tests run, link related issues/docs, and update progress trackers (for example `phase1/docs/PHASE_1_PROGRESS_TRACKER.md` or `phase2/weeks/WEEK_32_STATUS.md`).

## Agent-Specific Instructions
Follow `CLAUDE.md` for weekly workflow, test matrices, and phase-specific constraints.
