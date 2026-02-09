#!/bin/bash
# _common.sh — zlink buildenv shared utilities
# Source this file from setup.sh and check.sh

# ---------------------------------------------------------------------------
# Version requirements (single source of truth)
# ---------------------------------------------------------------------------
REQUIRED_CMAKE_VERSION="3.10"
REQUIRED_GCC_VERSION="13"
REQUIRED_NODE_VERSION="20"
REQUIRED_PYTHON_VERSION="3.9"
REQUIRED_JDK_VERSION="22"
REQUIRED_GRADLE_VERSION="8.7"
REQUIRED_DOTNET_VERSION="8.0"

GRADLE_INSTALL_VERSION="8.7"
NODE_MAJOR_VERSION="20"

# ---------------------------------------------------------------------------
# Colors
# ---------------------------------------------------------------------------
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[0;33m'
    BLUE='\033[0;34m'
    CYAN='\033[0;36m'
    BOLD='\033[1m'
    NC='\033[0m'
else
    RED='' GREEN='' YELLOW='' BLUE='' CYAN='' BOLD='' NC=''
fi

# ---------------------------------------------------------------------------
# Logging helpers
# ---------------------------------------------------------------------------
info()    { echo -e "${BLUE}[INFO]${NC} $*"; }
success() { echo -e "${GREEN}[OK]${NC}   $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC} $*"; }
error()   { echo -e "${RED}[ERR]${NC}  $*"; }
header()  { echo -e "\n${BOLD}${CYAN}=== $* ===${NC}"; }

# ---------------------------------------------------------------------------
# Version comparison
# Returns 0 if $1 >= $2  (both are dotted version strings)
# ---------------------------------------------------------------------------
version_gte() {
    local IFS=.
    local i a=($1) b=($2)
    for ((i = 0; i < ${#b[@]}; i++)); do
        local va=${a[i]:-0}
        local vb=${b[i]:-0}
        if ((va > vb)); then return 0; fi
        if ((va < vb)); then return 1; fi
    done
    return 0
}

# ---------------------------------------------------------------------------
# Extract major.minor (or major) version from a version string
# e.g. "v20.18.1" -> "20.18.1",  "cmake version 3.28.3" -> "3.28.3"
# ---------------------------------------------------------------------------
extract_version() {
    echo "$1" | grep -oP '\d+(\.\d+)+' | head -1
}

# ---------------------------------------------------------------------------
# Platform detection
# ---------------------------------------------------------------------------
detect_platform() {
    BUILDENV_OS="$(uname -s)"
    BUILDENV_ARCH="$(uname -m)"

    if [ "$BUILDENV_OS" != "Linux" ]; then
        error "This script is designed for Linux (WSL Ubuntu 24.04)."
        exit 1
    fi

    if [ -f /etc/os-release ]; then
        # shellcheck disable=SC1091
        . /etc/os-release
        BUILDENV_DISTRO="${ID}"
        BUILDENV_DISTRO_VERSION="${VERSION_ID}"
        BUILDENV_CODENAME="${VERSION_CODENAME}"
    else
        BUILDENV_DISTRO="unknown"
        BUILDENV_DISTRO_VERSION="unknown"
        BUILDENV_CODENAME="unknown"
    fi
}

# ---------------------------------------------------------------------------
# Resolve script directory (works even when sourced)
# ---------------------------------------------------------------------------
_resolve_buildenv_dir() {
    local src="${BASH_SOURCE[0]}"
    while [ -h "$src" ]; do
        local dir="$(cd -P "$(dirname "$src")" && pwd)"
        src="$(readlink "$src")"
        [[ $src != /* ]] && src="$dir/$src"
    done
    echo "$(cd -P "$(dirname "$src")" && pwd)"
}

BUILDENV_DIR="$(_resolve_buildenv_dir)"
PROJECT_ROOT="$(cd "$BUILDENV_DIR/.." && pwd)"
