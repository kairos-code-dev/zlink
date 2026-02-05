# Repository Guidelines

## Project Structure and Module Organization
- `core/src/`: core libzlink implementation (C++98/11 style).
- `core/include/`: public headers like `core/include/zlink.h`.
- `core/tests/`: functional test suite (Unity), files named `core/tests/test_*.cpp`.
- `core/unittests/`: internal tests named `core/unittests/unittest_*.cpp`.
- `build-scripts/`: platform build scripts (e.g., `build-scripts/linux/build.sh`).
- `core/benchwithzlink/` and `core/perf/`: benchmarks and comparison tooling.
- `dist/`: packaged build outputs by platform.
- `bindings/`: language wrappers (C++, Java, C#, Node.js).
- `docs/`: project documentation.
- `tools/`: dev/build helper scripts.

## Build, Test, and Development Commands
- `./build.sh`: clean CMake build in `core/build/` and runs tests (Linux-style `nproc`).
- `./build-scripts/linux/build.sh x64 ON`: Linux build with tests (macOS and Windows have equivalent scripts).
- `cmake -B build -DZLINK_BUILD_TESTS=ON`: configure; `cmake --build build` to compile.
- `ctest --output-on-failure`: run tests from a build dir (e.g., `core/build/linux-x64`).
- Autotools fallback: `./autogen.sh`, `./configure`, `make`, `make check` (do not use `-j` with `make check`).
- Optional flags: `-DBUILD_BENCHMARKS=ON`, `-DZLINK_CXX_STANDARD=17` (see `CXX_BUILD_EXAMPLES.md`).

## Coding Style and Naming Conventions
- Follow `.clang-format`: 4-space indent, no tabs, 80-column limit, C++03 mode.
- Keep style consistent with existing `core/src/` patterns; use minimal C++11 unless required.
- Use existing naming patterns; new tests should match `test_*.cpp` or `unittest_*.cpp`.

## Testing Guidelines
- Tests use the Unity framework; add coverage in `tests/` for behavior changes and `unittests/` for internal logic.
- Some suites are platform-specific (IPC/TIPC, fuzzers); note skips in PRs.

## Commit and Pull Request Guidelines
- Commit messages typically use conventional prefixes like `feat:`, `fix:`, `docs:` with a short summary.
- Follow the C4 contribution model and keep PRs focused.
- PRs should include: summary, test commands/results, platform(s) tested, and benchmark notes for perf-sensitive changes.

## Security and Configuration
- Report vulnerabilities via `SECURITY.md`.
- `VERSION` controls libzlink feature/version knobs; keep changes deliberate and documented.

## Communication
- Address the user as "팀장님".

## Agent Instructions
- `AGENTS.md` is the single source of truth for repo guidelines.
- If any agent-specific files are added in the future, they must reference `AGENTS.md` and instruct contributors to update `AGENTS.md` when guidelines change.

## External References
- libzlink reference source: `/home/ulalax/project/ulalax/libzlink-ref`
