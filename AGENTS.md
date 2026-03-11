# Repository Guidelines

## Project Structure and Module Organization
- `core/src/`: core libzlink implementation (C++98/11 style).
- `core/include/`: public headers like `core/include/zlink.h`.
- `core/tests/`: functional test suite (Unity), files named `core/tests/test_*.cpp`.
- `core/unittests/`: internal tests named `core/unittests/unittest_*.cpp`.
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
- `cmake -B build -DZLINK_BUILD_TESTS=ON`: configure; `cmake --build build` to compile.
- `ctest --output-on-failure`: run tests from a build dir (e.g., `core/build/linux-x64`).
- Optional flags: `-DZLINK_CXX_STANDARD=17` (see `CXX_BUILD_EXAMPLES.md`).

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
- If any agent-specific files are added in the future, they must reference `AGENTS.md` and instruct contributors to update `AGENTS.md` when guidelines change.
- When the user says `posd` in the context of design or refactoring, interpret it as John Ousterhout's *A Philosophy of Software Design* and apply that book's principles.

## External References
- Upstream reference project: `https://github.com/zeromq/libzmq`
