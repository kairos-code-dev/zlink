# Repository Guidelines

## Project Structure and Module Organization
- `core/src/`: core libzlink implementation (C++98/11 style).
- `core/include/`: public headers like `core/include/zlink.h`.
- `core/tests/unittest/`: internal Unity tests named `unittest_*.cpp`.
- `core/tests/integration/`: focused functional Unity tests.
- `core/tests/e2e/`: umbrella and smoke-style Unity tests.
- `core/tests/`: shared test helpers, README, and lane runner script.
- `core/build-scripts/`: platform build scripts (e.g., `core/build-scripts/linux/build.sh`).
- `core/builds/`: build helpers, templates, and CI tooling (CMake modules, platform helpers).
- `core/external/`: bundled third-party sources (Boost, Unity, etc.).
- `core/tools/`: dev/build helper scripts.
- `core/packaging/`: packaging metadata (conan, debian, nuget, redhat).
- `core/dist/`: packaged build outputs by platform.
- `bindings/`: language wrappers (C++, Java, C#, Node.js).
- `doc/`: project documentation.

## Build, Test, and Development Commands
- `./core/build.sh`: clean CMake build in `core/build/` and runs tests (Linux-style `nproc`).
- `./core/build-scripts/linux/build.sh x64 ON`: Linux build with tests (macOS and Windows have equivalent scripts).
- `cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON`: configure into the only supported build directory.
- `cmake --build core/build`: compile from the fixed `core/build/` directory.
- `ctest --test-dir core/build --output-on-failure`: run all registered tests from `core/build/`; prefer lane-based commands below for real verification.
- `ctest --test-dir core/build --output-on-failure -L unittest -j$(nproc)`: run unit tests in parallel.
- `ctest --test-dir core/build --output-on-failure -L integration -j1`: run integration tests serially.
- `ctest --test-dir core/build --output-on-failure -L e2e -j1`: run e2e umbrella/scenario tests serially.
- `./core/tests/run_test_lanes.sh`: run the default sequential lane pipeline (`unittest` then `integration`).
- `./core/tests/run_test_lanes.sh --include-e2e`: run the full sequential lane pipeline (`unittest`, `integration`, `e2e`).
- Optional flags: `-DZLINK_CXX_STANDARD=17` (see `CXX_BUILD_EXAMPLES.md`).
- Do not configure or build into a top-level `build/` directory for this repository. Use `core/build/` only.

## Coding Style and Naming Conventions
- Follow `.clang-format`: 4-space indent, no tabs, 80-column limit, C++03 mode.
- Keep style consistent with existing `core/src/` patterns; use minimal C++11 unless required.
- Use existing naming patterns; new tests should match `test_*.cpp` or `unittest_*.cpp`.

## Software Design Philosophy
- This repository adopts John Ousterhout's *A Philosophy of Software Design* as a core development philosophy for software system design and evolution.
- All design, implementation, and refactoring work should aim to reduce overall system complexity, not just move code around or preserve incidental structure.
- Prefer deep modules, clear ownership boundaries, information hiding, and interfaces that let users know less about internal machinery.
- Treat change amplification, hidden coupling, temporal decomposition, and shallow wrappers as design smells that should be removed during design and refactoring.
- When changing public APIs or internal structures, prefer solutions that make lifecycle, ownership, and invariants easier to explain in a few sentences.
- If a change improves local performance or convenience but increases global complexity, default to rejecting it unless there is a compelling, documented reason.

## Testing Guidelines
- Tests use the Unity framework; add coverage in `tests/` for behavior changes and `unittests/` for internal logic.
- Some suites are platform-specific (IPC/TIPC, fuzzers); note skips in PRs.
- Test layout, lane policy, and runner usage: `core/tests/README.md`
- Do not launch multiple `ctest` processes concurrently for serial lanes; `RESOURCE_LOCK` only coordinates tests within one `ctest` process.

### Fail-Fast Policy (all test types)
The following rules apply to **all** test categories: unit tests (`unittests/`), functional tests (`tests/`), perf tests (`perf/`), and bench tests (`bench/`).

- **No retry logic.** Tests must never contain retry loops, backoff-and-retry, poll-until-success, or any form of automatic retry on failure. A failing assertion must surface immediately so the root cause can be identified quickly. If a test needs retry logic to pass, the test or the code under test has a bug — fix the bug, not the test.
- **Fail on first error.** A single test failure must fail the entire test executable. Do not catch, suppress, or continue past assertion failures. The goal is to stop early and preserve the failure context for diagnosis.
- **No sleep-based synchronization.** Do not use `sleep()` or fixed delays to wait for asynchronous state. Use deterministic synchronization (semaphore, condition variable, event flag) with a hard timeout that fails the test if exceeded.
- **Hard timeouts, not soft retries.** If a test must wait for an external condition (connection, message arrival), use a single bounded wait with `TEST_ASSERT` on timeout. Never loop back and retry the same operation.
- **No bug-hiding test edits.** If a test is incorrect, outdated, or violates the contract, fix the test to express the right behavior. Do not weaken, relax, or rewrite a test only to hide an implementation bug or make a failing case appear to pass.
- **Do not solve product bugs by changing tests.** Test edits are forbidden as a way to make failures disappear, except when the test's meaning has intentionally changed or the test itself is provably wrong. In all other cases, fix the code or the environment root cause instead.

## Commit and Pull Request Guidelines
- Commit messages typically use conventional prefixes like `feat:`, `fix:`, `docs:` with a short summary.
- Follow the C4 contribution model and keep PRs focused.
- PRs should include: summary, test commands/results, and platform(s) tested.

## Security and Configuration
- Report vulnerabilities via `SECURITY.md`.
- `VERSION` controls libzlink feature/version knobs; keep changes deliberate and documented.

## Release Workflow
- When asked to release a new `core` version, update `VERSION`, `core/include/zlink.h`, and `CHANGELOG.md` together before tagging.
- Write a versioned changelog entry with the user-visible fixes/changes included in that release so other contributors can identify the impact from the version alone.
- Prefer releasing from the current workspace commit only after confirming the release content is intentionally scoped; do not tag a dirty or ambiguous tree.
- Create and push the release tag in the form `core/vX.Y.Z`; the GitHub Actions core release build is triggered by that tag via `.github/workflows/build.yml`.
- After the GitHub Release build succeeds, run `/home/hep7/project/kairos/zlink/bindings/update_zlink_libs.sh core/vX.Y.Z --repo kairos-code-dev/zlink --expect-version X.Y.Z` to refresh bindings against the released native artifacts.
- Report back with the pushed commit, tag, GitHub Actions run, release URL, and the changelog summary for that version.
- If Conan publication is expected, verify `core/packaging/conan/conandata.yml` contains the new version metadata before pushing the tag; otherwise the Conan workflow will fail even if the GitHub Release succeeds.

## Agent Instructions
- `AGENTS.md` is the single source of truth for repo guidelines.
- Agents must address the user as `팀장님`.
- Agents must use `core/build/` as the only build directory for configure, build, test, perf, and bench commands in this repository.
- When the user asks for a `core` bug fix, agents must limit code changes to `core/` library and `core/tests/` regression coverage. Do not modify `core/perf/` or `core/bench/` unless the user explicitly asks for perf/bench code changes.
- Do not use ad-hoc repro programs, `/tmp` experiments, or one-off binaries to justify a fix. Reproduction must be added as a repository regression test under `core/tests/` first, then used to validate the `core` fix.
- For perf or bench failures, first determine whether the same issue reproduces with a `core/tests/` regression. If it does, fix the `core` bug and keep the regression test. If it does not, treat it as a perf/bench usage bug and report it separately instead of modifying `core`.
- Do not modify perf/bench helpers, runners, or wrappers as a workaround for a suspected `core` bug. Perf and bench are verification surfaces unless the user explicitly asks to change them.
- If any agent-specific files are added in the future, they must reference `AGENTS.md` and instruct contributors to update `AGENTS.md` when guidelines change.
- When the user says `posd` in the context of design or refactoring, interpret it as John Ousterhout's *A Philosophy of Software Design* and apply that book's principles.
- Even when `posd` is not explicitly mentioned, agents should follow the repository's POSD-based design philosophy when reviewing, designing, implementing, or refactoring code and APIs.

## External References
- Upstream reference project: `https://github.com/zeromq/libzmq`
