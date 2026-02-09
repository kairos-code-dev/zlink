#!/bin/bash
# setup.sh — zlink development environment setup for WSL Ubuntu 24.04
#
# Usage:
#   ./buildenv/setup.sh [OPTIONS]
#
# Options:
#   --all       Install everything (default)
#   --core      Core C/C++ build tools only
#   --node      Node.js binding environment
#   --python    Python binding environment
#   --java      Java binding environment
#   --dotnet    .NET binding environment
#   --check     Verify current environment (no install)
#   --help      Show this help message
#
# Multiple options can be combined:
#   ./buildenv/setup.sh --core --node --python

set -euo pipefail

# ---------------------------------------------------------------------------
# Load common utilities
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/_common.sh"

# ---------------------------------------------------------------------------
# Parse arguments
# ---------------------------------------------------------------------------
INSTALL_CORE=false
INSTALL_NODE=false
INSTALL_PYTHON=false
INSTALL_JAVA=false
INSTALL_DOTNET=false
DO_CHECK=false
EXPLICIT_SELECTION=false

show_help() {
    sed -n '2,/^$/s/^# \?//p' "$0"
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --all)     INSTALL_CORE=true; INSTALL_NODE=true; INSTALL_PYTHON=true
                   INSTALL_JAVA=true; INSTALL_DOTNET=true; EXPLICIT_SELECTION=true ;;
        --core)    INSTALL_CORE=true;   EXPLICIT_SELECTION=true ;;
        --node)    INSTALL_NODE=true;   EXPLICIT_SELECTION=true ;;
        --python)  INSTALL_PYTHON=true; EXPLICIT_SELECTION=true ;;
        --java)    INSTALL_JAVA=true;   EXPLICIT_SELECTION=true ;;
        --dotnet)  INSTALL_DOTNET=true; EXPLICIT_SELECTION=true ;;
        --check)   DO_CHECK=true ;;
        --help|-h) show_help ;;
        *)         error "Unknown option: $1"; show_help ;;
    esac
    shift
done

# Default: install everything
if [ "$EXPLICIT_SELECTION" = false ] && [ "$DO_CHECK" = false ]; then
    INSTALL_CORE=true
    INSTALL_NODE=true
    INSTALL_PYTHON=true
    INSTALL_JAVA=true
    INSTALL_DOTNET=true
fi

# --check only: delegate to check.sh
if [ "$DO_CHECK" = true ] && [ "$EXPLICIT_SELECTION" = false ]; then
    exec "$SCRIPT_DIR/check.sh"
fi

# ---------------------------------------------------------------------------
# Pre-flight checks
# ---------------------------------------------------------------------------
detect_platform

header "zlink Development Environment Setup"
info "Platform : $BUILDENV_DISTRO $BUILDENV_DISTRO_VERSION ($BUILDENV_ARCH)"
info "Project  : $PROJECT_ROOT"

if [ "$BUILDENV_DISTRO" != "ubuntu" ]; then
    warn "This script is optimized for Ubuntu 24.04. Other distros may need adjustments."
fi

# sudo check
if ! sudo -n true 2>/dev/null && ! sudo true 2>/dev/null; then
    error "sudo privileges are required. Please run with a user that has sudo access."
    exit 1
fi

# ---------------------------------------------------------------------------
# Helper: apt install with idempotency
# ---------------------------------------------------------------------------
apt_install() {
    local to_install=()
    for pkg in "$@"; do
        if ! dpkg -s "$pkg" &>/dev/null; then
            to_install+=("$pkg")
        fi
    done
    if [ ${#to_install[@]} -gt 0 ]; then
        info "Installing: ${to_install[*]}"
        sudo apt-get install -y -qq "${to_install[@]}"
    else
        info "Already installed: $*"
    fi
}

# ---------------------------------------------------------------------------
# Step 1: System base tools (always)
# ---------------------------------------------------------------------------
header "Step 1: System Base Tools"

sudo apt-get update -qq

apt_install \
    build-essential \
    cmake \
    pkg-config \
    git \
    curl \
    wget \
    unzip \
    autoconf \
    automake \
    libtool \
    lsb-release \
    ninja-build \
    ccache \
    ca-certificates \
    gnupg

success "System base tools ready"

# ---------------------------------------------------------------------------
# Step 2: Core C/C++
# ---------------------------------------------------------------------------
if [ "$INSTALL_CORE" = true ]; then
    header "Step 2: Core C/C++ Build Environment"

    apt_install \
        g++ \
        libssl-dev \
        libbsd-dev \
        clang-format \
        clang-tidy \
        gdb \
        valgrind \
        doxygen

    success "Core C/C++ environment ready"
fi

# ---------------------------------------------------------------------------
# Step 3: Node.js binding
# ---------------------------------------------------------------------------
if [ "$INSTALL_NODE" = true ]; then
    header "Step 3: Node.js Binding Environment"

    # python-is-python3 is needed by binding.gyp which invokes 'python'
    apt_install python-is-python3

    # Install Node.js from NodeSource if not already present or too old
    NEED_NODE=true
    if command -v node &>/dev/null; then
        CURRENT_NODE="$(extract_version "$(node --version)")"
        if version_gte "$CURRENT_NODE" "$REQUIRED_NODE_VERSION"; then
            info "Node.js $CURRENT_NODE already installed (>= $REQUIRED_NODE_VERSION)"
            NEED_NODE=false
        else
            warn "Node.js $CURRENT_NODE is too old (need >= $REQUIRED_NODE_VERSION)"
        fi
    fi

    if [ "$NEED_NODE" = true ]; then
        info "Installing Node.js $NODE_MAJOR_VERSION from NodeSource..."
        sudo mkdir -p /etc/apt/keyrings
        curl -fsSL https://deb.nodesource.com/gpgkey/nodesource-repo.gpg.key \
            | sudo gpg --dearmor -o /etc/apt/keyrings/nodesource.gpg 2>/dev/null || true
        echo "deb [signed-by=/etc/apt/keyrings/nodesource.gpg] https://deb.nodesource.com/node_${NODE_MAJOR_VERSION}.x nodistro main" \
            | sudo tee /etc/apt/sources.list.d/nodesource.list >/dev/null
        sudo apt-get update -qq
        sudo apt-get install -y -qq nodejs
    fi

    # node-gyp for native addon builds
    if ! command -v node-gyp &>/dev/null; then
        info "Installing node-gyp globally..."
        sudo npm install -g node-gyp
    else
        info "node-gyp already installed"
    fi

    success "Node.js binding environment ready"
fi

# ---------------------------------------------------------------------------
# Step 4: Python binding
# ---------------------------------------------------------------------------
if [ "$INSTALL_PYTHON" = true ]; then
    header "Step 4: Python Binding Environment"

    apt_install \
        python3 \
        python3-pip \
        python3-venv \
        python3-dev

    # Install build tools in user scope
    info "Installing Python build tools (setuptools, wheel)..."
    python3 -m pip install --user --upgrade "setuptools>=68" wheel 2>/dev/null \
        || python3 -m pip install --break-system-packages --user --upgrade "setuptools>=68" wheel 2>/dev/null \
        || warn "pip install failed — try manually: pip install 'setuptools>=68' wheel"

    success "Python binding environment ready"
fi

# ---------------------------------------------------------------------------
# Step 5: Java binding
# ---------------------------------------------------------------------------
if [ "$INSTALL_JAVA" = true ]; then
    header "Step 5: Java Binding Environment"

    # --- JDK 22 via Eclipse Adoptium Temurin ---
    NEED_JDK=true
    if command -v java &>/dev/null; then
        CURRENT_JAVA="$(extract_version "$(java --version 2>&1)")"
        if version_gte "$CURRENT_JAVA" "$REQUIRED_JDK_VERSION"; then
            info "JDK $CURRENT_JAVA already installed (>= $REQUIRED_JDK_VERSION)"
            NEED_JDK=false
        else
            warn "JDK $CURRENT_JAVA is too old (need >= $REQUIRED_JDK_VERSION)"
        fi
    fi

    if [ "$NEED_JDK" = true ]; then
        info "Installing JDK $REQUIRED_JDK_VERSION from Eclipse Adoptium..."
        sudo mkdir -p /etc/apt/keyrings
        wget -qO - https://packages.adoptium.net/artifactory/api/gpg/key/public \
            | sudo gpg --dearmor -o /etc/apt/keyrings/adoptium.gpg 2>/dev/null || true
        echo "deb [signed-by=/etc/apt/keyrings/adoptium.gpg] https://packages.adoptium.net/artifactory/deb $(lsb_release -cs) main" \
            | sudo tee /etc/apt/sources.list.d/adoptium.list >/dev/null
        sudo apt-get update -qq
        sudo apt-get install -y -qq "temurin-${REQUIRED_JDK_VERSION}-jdk"
    fi

    # --- Gradle (manual install, Ubuntu package is too old) ---
    NEED_GRADLE=true
    if command -v gradle &>/dev/null; then
        CURRENT_GRADLE="$(extract_version "$(gradle --version 2>&1)")"
        if version_gte "$CURRENT_GRADLE" "$REQUIRED_GRADLE_VERSION"; then
            info "Gradle $CURRENT_GRADLE already installed (>= $REQUIRED_GRADLE_VERSION)"
            NEED_GRADLE=false
        else
            warn "Gradle $CURRENT_GRADLE is too old (need >= $REQUIRED_GRADLE_VERSION)"
        fi
    fi

    if [ "$NEED_GRADLE" = true ]; then
        info "Installing Gradle $GRADLE_INSTALL_VERSION..."
        GRADLE_ZIP="gradle-${GRADLE_INSTALL_VERSION}-bin.zip"
        GRADLE_URL="https://services.gradle.org/distributions/${GRADLE_ZIP}"
        TMP_GRADLE="$(mktemp -d)"

        wget -q -P "$TMP_GRADLE" "$GRADLE_URL"
        sudo rm -rf "/opt/gradle/gradle-${GRADLE_INSTALL_VERSION}"
        sudo mkdir -p /opt/gradle
        sudo unzip -q -o "$TMP_GRADLE/$GRADLE_ZIP" -d /opt/gradle
        sudo ln -sf "/opt/gradle/gradle-${GRADLE_INSTALL_VERSION}/bin/gradle" /usr/local/bin/gradle
        rm -rf "$TMP_GRADLE"
    fi

    success "Java binding environment ready"
fi

# ---------------------------------------------------------------------------
# Step 6: .NET binding
# ---------------------------------------------------------------------------
if [ "$INSTALL_DOTNET" = true ]; then
    header "Step 6: .NET Binding Environment"

    NEED_DOTNET=true
    if command -v dotnet &>/dev/null; then
        CURRENT_DOTNET="$(extract_version "$(dotnet --version 2>&1)")"
        if version_gte "$CURRENT_DOTNET" "$REQUIRED_DOTNET_VERSION"; then
            info ".NET SDK $CURRENT_DOTNET already installed (>= $REQUIRED_DOTNET_VERSION)"
            NEED_DOTNET=false
        else
            warn ".NET SDK $CURRENT_DOTNET is too old (need >= $REQUIRED_DOTNET_VERSION)"
        fi
    fi

    if [ "$NEED_DOTNET" = true ]; then
        # Try Ubuntu native package first (avoids Microsoft repo conflicts)
        info "Installing .NET SDK $REQUIRED_DOTNET_VERSION..."
        if apt-cache show "dotnet-sdk-${REQUIRED_DOTNET_VERSION}" &>/dev/null; then
            sudo apt-get install -y -qq "dotnet-sdk-${REQUIRED_DOTNET_VERSION}"
        else
            # Fallback: Microsoft repository
            warn "Ubuntu native .NET package not found, using Microsoft repository..."
            wget -q "https://packages.microsoft.com/config/ubuntu/$(lsb_release -rs)/packages-microsoft-prod.deb" \
                -O /tmp/packages-microsoft-prod.deb
            sudo dpkg -i /tmp/packages-microsoft-prod.deb
            rm -f /tmp/packages-microsoft-prod.deb
            sudo apt-get update -qq
            sudo apt-get install -y -qq "dotnet-sdk-${REQUIRED_DOTNET_VERSION}"
        fi
    fi

    success ".NET binding environment ready"
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
header "Setup Complete"
echo ""

# Run verification
if [ -x "$SCRIPT_DIR/check.sh" ]; then
    "$SCRIPT_DIR/check.sh"
else
    info "Run ./buildenv/check.sh to verify the environment."
fi

echo ""
info "Next steps:"
info "  1. Build core library : ./core/build.sh"
info "  2. Verify environment : ./buildenv/check.sh"
