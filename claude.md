# Claude Code Project Configuration

## Agent Preferences

- **Always use `dev-cxx` agent** for all development tasks
- All code modifications, build script changes, and CI/CD updates should be delegated to the dev-cxx agent
- This applies to C++, shell scripts, PowerShell scripts, YAML workflows, and other code files

## Project Overview

- libzmq native library build project for multiple platforms
- Platforms: Windows (x64, ARM64), Linux (x64, ARM64), macOS (x64, ARM64)
- Build system: CMake with platform-specific scripts

## Build Scripts

- Linux: `build-scripts/linux/build.sh`
- macOS: `build-scripts/macos/build.sh`
- Windows: `build-scripts/windows/build.ps1`

## CI/CD

- GitHub Actions workflow: `.github/workflows/build.yml`
- Tests should run on all platforms with platform-specific considerations
