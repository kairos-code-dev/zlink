#!/bin/bash
# check.sh — Verify zlink development environment
#
# Usage:
#   ./buildenv/check.sh
#
# Checks all required tools and their versions, printing a summary table.

set -euo pipefail

# ---------------------------------------------------------------------------
# Load common utilities
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/_common.sh"

detect_platform

# ---------------------------------------------------------------------------
# State
# ---------------------------------------------------------------------------
TOTAL=0
PASS=0
FAIL=0
WARN_COUNT=0

# ---------------------------------------------------------------------------
# Check helpers
# ---------------------------------------------------------------------------

# check_cmd <label> <command> <required_version> [version_extract_cmd]
# Prints a status line and updates counters.
check_cmd() {
    local label="$1"
    local cmd="$2"
    local req_ver="$3"
    local ver_cmd="${4:-}"

    TOTAL=$((TOTAL + 1))

    if ! command -v "$cmd" &>/dev/null; then
        printf "  ${RED}%-14s${NC}  %-20s  %s\n" "$label" "NOT FOUND" "need >= $req_ver"
        FAIL=$((FAIL + 1))
        return
    fi

    local raw_ver=""
    if [ -n "$ver_cmd" ]; then
        raw_ver="$(eval "$ver_cmd" 2>&1 || true)"
    else
        raw_ver="$($cmd --version 2>&1 || true)"
    fi
    local ver="$(extract_version "$raw_ver")"

    if [ -z "$ver" ]; then
        printf "  ${YELLOW}%-14s${NC}  %-20s  %s\n" "$label" "installed (unknown)" "need >= $req_ver"
        WARN_COUNT=$((WARN_COUNT + 1))
        return
    fi

    if version_gte "$ver" "$req_ver"; then
        printf "  ${GREEN}%-14s${NC}  %-20s  %s\n" "$label" "$ver" ">= $req_ver"
        PASS=$((PASS + 1))
    else
        printf "  ${RED}%-14s${NC}  %-20s  %s\n" "$label" "$ver" "NEED >= $req_ver"
        FAIL=$((FAIL + 1))
    fi
}

# check_pkg <label> <dpkg_name>
# Simply checks whether a Debian package is installed.
check_pkg() {
    local label="$1"
    local pkg="$2"

    TOTAL=$((TOTAL + 1))

    if dpkg -s "$pkg" &>/dev/null; then
        printf "  ${GREEN}%-14s${NC}  %-20s\n" "$label" "installed"
        PASS=$((PASS + 1))
    else
        printf "  ${RED}%-14s${NC}  %-20s\n" "$label" "NOT FOUND"
        FAIL=$((FAIL + 1))
    fi
}

# ---------------------------------------------------------------------------
# Header
# ---------------------------------------------------------------------------
header "zlink Development Environment Check"
info "Platform : $BUILDENV_DISTRO $BUILDENV_DISTRO_VERSION ($BUILDENV_ARCH)"
info "Project  : $PROJECT_ROOT"
echo ""

# ---------------------------------------------------------------------------
# System base tools
# ---------------------------------------------------------------------------
echo -e "${BOLD}  System Base Tools${NC}"
echo -e "  -----------------------------------------------"
check_cmd "cmake"       cmake       "$REQUIRED_CMAKE_VERSION"
check_cmd "g++"         g++         "$REQUIRED_GCC_VERSION"
check_pkg "pkg-config"  pkg-config
check_pkg "ninja-build" ninja-build
check_pkg "ccache"      ccache
check_pkg "autoconf"    autoconf
check_pkg "automake"    automake
check_pkg "libtool"     libtool
echo ""

# ---------------------------------------------------------------------------
# Core C/C++
# ---------------------------------------------------------------------------
echo -e "${BOLD}  Core C/C++ Libraries & Tools${NC}"
echo -e "  -----------------------------------------------"
check_pkg "libssl-dev"  libssl-dev
check_pkg "libbsd-dev"  libbsd-dev
check_pkg "clang-format" clang-format
check_pkg "clang-tidy"  clang-tidy
check_pkg "gdb"         gdb
check_pkg "valgrind"    valgrind
check_pkg "doxygen"     doxygen
echo ""

# ---------------------------------------------------------------------------
# Node.js
# ---------------------------------------------------------------------------
echo -e "${BOLD}  Node.js Binding${NC}"
echo -e "  -----------------------------------------------"
check_cmd "node"        node        "$REQUIRED_NODE_VERSION"   "node --version"
check_cmd "npm"         npm         "0"                        "npm --version"
check_cmd "node-gyp"    node-gyp    "0"                        "node-gyp --version"
check_pkg "python-is-python3" python-is-python3
echo ""

# ---------------------------------------------------------------------------
# Python
# ---------------------------------------------------------------------------
echo -e "${BOLD}  Python Binding${NC}"
echo -e "  -----------------------------------------------"
check_cmd "python3"     python3     "$REQUIRED_PYTHON_VERSION" "python3 --version"
check_cmd "pip3"        pip3        "0"                        "pip3 --version"
check_pkg "python3-venv" python3-venv
check_pkg "python3-dev" python3-dev

# Check setuptools version
TOTAL=$((TOTAL + 1))
SETUPTOOLS_VER="$(python3 -c 'import setuptools; print(setuptools.__version__)' 2>/dev/null || echo "")"
if [ -n "$SETUPTOOLS_VER" ] && version_gte "$SETUPTOOLS_VER" "68"; then
    printf "  ${GREEN}%-14s${NC}  %-20s  %s\n" "setuptools" "$SETUPTOOLS_VER" ">= 68"
    PASS=$((PASS + 1))
elif [ -n "$SETUPTOOLS_VER" ]; then
    printf "  ${RED}%-14s${NC}  %-20s  %s\n" "setuptools" "$SETUPTOOLS_VER" "NEED >= 68"
    FAIL=$((FAIL + 1))
else
    printf "  ${RED}%-14s${NC}  %-20s\n" "setuptools" "NOT FOUND"
    FAIL=$((FAIL + 1))
fi
echo ""

# ---------------------------------------------------------------------------
# Java
# ---------------------------------------------------------------------------
echo -e "${BOLD}  Java Binding${NC}"
echo -e "  -----------------------------------------------"
check_cmd "java"        java        "$REQUIRED_JDK_VERSION"    "java --version"
check_cmd "javac"       javac       "$REQUIRED_JDK_VERSION"    "javac --version"
check_cmd "gradle"      gradle      "$REQUIRED_GRADLE_VERSION" "gradle --version"
echo ""

# ---------------------------------------------------------------------------
# .NET
# ---------------------------------------------------------------------------
echo -e "${BOLD}  .NET Binding${NC}"
echo -e "  -----------------------------------------------"
check_cmd "dotnet"      dotnet      "$REQUIRED_DOTNET_VERSION" "dotnet --version"
echo ""

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
echo -e "  ==============================================="
printf "  Total: %d  |  ${GREEN}Pass: %d${NC}  |  ${RED}Fail: %d${NC}  |  ${YELLOW}Warn: %d${NC}\n" \
    "$TOTAL" "$PASS" "$FAIL" "$WARN_COUNT"
echo -e "  ==============================================="

if [ "$FAIL" -gt 0 ]; then
    echo ""
    warn "Some tools are missing or outdated. Run ./buildenv/setup.sh to install."
    exit 1
else
    echo ""
    success "All development tools are properly installed."
    exit 0
fi
