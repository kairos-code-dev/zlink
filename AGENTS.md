# Repository Guidelines

## Project Structure and Module Organization
- `src/`: core libzmq implementation (C++98/11 style).
- `include/`: public headers like `include/zmq.h`.
- `tests/`: functional test suite (Unity), files named `tests/test_*.cpp`.
- `unittests/`: internal tests named `unittests/unittest_*.cpp`.
- `build-scripts/`: platform build scripts (e.g., `build-scripts/linux/build.sh`).
- `benchwithzmq/` and `perf/`: benchmarks and comparison tooling.
- `dist/`: packaged build outputs by platform.

## Build, Test, and Development Commands
- `./build.sh`: clean CMake build in `build/` and runs tests (Linux-style `nproc`).
- `./build-scripts/linux/build.sh x64 ON`: Linux build with tests (macOS and Windows have equivalent scripts).
- `cmake -B build -DZMQ_BUILD_TESTS=ON`: configure; `cmake --build build` to compile.
- `ctest --output-on-failure`: run tests from a build dir (e.g., `build/linux-x64`).
- Autotools fallback: `./autogen.sh`, `./configure`, `make`, `make check` (do not use `-j` with `make check`).
- Optional flags: `-DBUILD_BENCHMARKS=ON`, `-DZMQ_CXX_STANDARD=20` (see `CXX20_BUILD_EXAMPLES.md`).

## Coding Style and Naming Conventions
- Follow `.clang-format`: 4-space indent, no tabs, 80-column limit, C++03 mode.
- Keep style consistent with existing `src/` patterns; use minimal C++11 unless required.
- Use existing naming patterns; new tests should match `test_*.cpp` or `unittest_*.cpp`.

## Testing Guidelines
- Tests use the Unity framework; add coverage in `tests/` for behavior changes and `unittests/` for internal logic.
- Some suites are platform-specific (IPC/TIPC, fuzzers); note skips in PRs.

## Commit and Pull Request Guidelines
- Commit messages typically use conventional prefixes like `feat:`, `fix:`, `docs:` with a short summary.
- Follow the C4 contribution model (see `GEMINI.md`) and keep PRs focused.
- PRs should include: summary, test commands/results, platform(s) tested, and benchmark notes for perf-sensitive changes.

## Security and Configuration
- Report vulnerabilities via `SECURITY.md`.
- `VERSION` controls libzmq feature/version knobs; keep changes deliberate and documented.
