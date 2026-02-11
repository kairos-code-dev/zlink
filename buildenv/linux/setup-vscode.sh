#!/bin/bash
# setup-vscode.sh — Generate VS Code workspace configuration for zlink
#
# Usage:
#   ./buildenv/setup-vscode.sh [OPTIONS]
#
# Options:
#   --force     Overwrite existing .vscode/ files without prompting
#   --dry-run   Print generated config to stdout without writing files
#   --skip-extensions  Skip VS Code extension installation
#   --help      Show this help message
#

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
FORCE_OVERWRITE=false
DRY_RUN=false
INSTALL_EXTENSIONS=true
VSCODE_CLI=""

show_help() {
    sed -n '2,/^$/s/^# \?//p' "$0"
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --force)   FORCE_OVERWRITE=true ;;
        --dry-run) DRY_RUN=true ;;
        --skip-extensions) INSTALL_EXTENSIONS=false ;;
        --help|-h) show_help ;;
        *)         error "Unknown option: $1"; show_help ;;
    esac
    shift
done

# ---------------------------------------------------------------------------
# Tool detection functions
# ---------------------------------------------------------------------------
DETECTED_GPP_PATH=""
DETECTED_JAVA_HOME=""
HAS_GRADLEW=false
DETECTED_DOTNET_ROOT=""
INTELLISENSE_MODE=""
CPP_CONFIG_NAME=""
VCPKG_TRIPLET=""

detect_gpp_path() {
    if command -v g++ &>/dev/null; then
        DETECTED_GPP_PATH="$(command -v g++)"
        success "g++: $DETECTED_GPP_PATH"
    else
        DETECTED_GPP_PATH="/usr/bin/g++"
        warn "g++ not found — using fallback path $DETECTED_GPP_PATH"
    fi
}

detect_java_home() {
    if ! command -v java &>/dev/null; then
        warn "Java not found — Java settings will be skipped"
        return
    fi

    local java_bin
    java_bin="$(readlink -f "$(command -v java)")"
    DETECTED_JAVA_HOME="$(dirname "$(dirname "$java_bin")")"

    if [ ! -x "$DETECTED_JAVA_HOME/bin/java" ]; then
        warn "Could not determine JAVA_HOME from $java_bin"
        DETECTED_JAVA_HOME=""
        return
    fi

    local java_ver
    java_ver="$(extract_version "$("$DETECTED_JAVA_HOME/bin/java" --version 2>&1)")"
    success "Java: $DETECTED_JAVA_HOME (version $java_ver)"
}

detect_gradle_home() {
    if [ -x "$PROJECT_ROOT/bindings/java/gradlew" ]; then
        HAS_GRADLEW=true
        success "Gradle: using project gradlew wrapper"
        return
    fi

    if command -v gradle &>/dev/null; then
        local gradle_ver
        gradle_ver="$(extract_version "$(gradle --version 2>&1)")"
        success "Gradle: $(command -v gradle) (version $gradle_ver)"
    else
        warn "Gradle not found — Java test task will be skipped"
    fi
}

detect_dotnet_root() {
    if ! command -v dotnet &>/dev/null; then
        warn "dotnet not found — .NET settings will be skipped"
        return
    fi

    local dotnet_bin
    dotnet_bin="$(readlink -f "$(command -v dotnet)")"
    DETECTED_DOTNET_ROOT="$(dirname "$dotnet_bin")"

    local dotnet_ver
    dotnet_ver="$(extract_version "$(dotnet --version 2>&1)")"
    success ".NET: $DETECTED_DOTNET_ROOT (version $dotnet_ver)"
}

detect_intellisense_mode() {
    case "$BUILDENV_ARCH" in
        x86_64)  INTELLISENSE_MODE="linux-gcc-x64" ;;
        aarch64) INTELLISENSE_MODE="linux-gcc-arm64" ;;
        *)       INTELLISENSE_MODE="linux-gcc-x64" ;;
    esac
    CPP_CONFIG_NAME="Linux"
}

detect_vcpkg_triplet() {
    case "$BUILDENV_ARCH" in
        x86_64)  VCPKG_TRIPLET="x64-linux" ;;
        aarch64) VCPKG_TRIPLET="arm64-linux" ;;
        *)       VCPKG_TRIPLET="x64-linux" ;;
    esac
}

detect_vscode_cli() {
    if command -v code &>/dev/null; then
        VSCODE_CLI="code"
        success "VS Code CLI: $(command -v code)"
        return
    fi

    if command -v code-insiders &>/dev/null; then
        VSCODE_CLI="code-insiders"
        success "VS Code CLI: $(command -v code-insiders)"
        return
    fi

    VSCODE_CLI=""
    warn "VS Code CLI not found (code/code-insiders) — extension install will be skipped"
}

# ---------------------------------------------------------------------------
# JSON writer helper
# ---------------------------------------------------------------------------
write_json() {
    local outfile="$1"
    local content="$2"

    if [ "$DRY_RUN" = true ]; then
        echo ""
        echo "=== $(basename "$outfile") ==="
        echo "$content"
        return
    fi

    printf '%s\n' "$content" > "$outfile"

    if ! python3 -c "import json, sys; json.load(open(sys.argv[1]))" "$outfile" 2>/dev/null; then
        error "Invalid JSON generated: $outfile"
        return 1
    fi

    success "Generated $(basename "$outfile")"
}

# ---------------------------------------------------------------------------
# JSON generation functions
# ---------------------------------------------------------------------------
generate_settings_json() {
    local outfile="$VSCODE_DIR/settings.json"

    # Build conditional Java fragment
    local java_fragment=""
    if [ -n "$DETECTED_JAVA_HOME" ]; then
        java_fragment="
    \"java.import.gradle.java.home\": \"$DETECTED_JAVA_HOME\",
    \"java.jdt.ls.java.home\": \"$DETECTED_JAVA_HOME\",
    \"java.configuration.runtimes\": [
        {
            \"name\": \"JavaSE-${REQUIRED_JDK_VERSION}\",
            \"path\": \"$DETECTED_JAVA_HOME\",
            \"default\": true
        }
    ],"
    fi

    local content
    content=$(cat << EOF
{
    "cmake.sourceDirectory": "\${workspaceFolder}",
    "cmake.buildDirectory": "\${workspaceFolder}/core/build/vscode-ninja",
    "cmake.generator": "Ninja",
    "cmake.configureSettings": {
        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
    },
    "C_Cpp.default.compileCommands": "\${workspaceFolder}/compile_commands.json",
    "C_Cpp.default.intelliSenseMode": "$INTELLISENSE_MODE",
    "C_Cpp.default.compilerPath": "$DETECTED_GPP_PATH",
    "C_Cpp.default.browse.path": [
        "\${workspaceFolder}/core/include",
        "\${workspaceFolder}/core/src",
        "\${workspaceFolder}/core/tests",
        "\${workspaceFolder}/core/unittests",
        "\${workspaceFolder}/bindings/cpp/include",
        "\${workspaceFolder}/bindings/node/native/src",
        "\${workspaceFolder}/core/external/boost",
        "\${workspaceFolder}/core/deps/vcpkg/installed/$VCPKG_TRIPLET/include"
    ],
    "C_Cpp.default.browse.limitSymbolsToIncludedHeaders": false,
    "python.analysis.extraPaths": [
        "\${workspaceFolder}/bindings/python/src"
    ],
    "python.autoComplete.extraPaths": [
        "\${workspaceFolder}/bindings/python/src"
    ],${java_fragment}
    "files.watcherExclude": {
        "**/.git/**": true,
        "**/core/build/**": true,
        "**/build/**": true,
        "**/bindings/**/build/**": true,
        "**/bindings/**/.gradle/**": true,
        "**/bindings/**/prebuilds/**": true,
        "**/bindings/**/runtimes/**": true,
        "**/bindings/cpp/native/**": true,
        "**/bindings/java/src/main/resources/native/**": true,
        "**/bindings/python/src/zlink/native/**": true,
        "**/bindings/python/src/zlink.egg-info/**": true,
        "**/bindings/python/tests/__pycache__/**": true
    },
    "search.exclude": {
        "**/core/build": true,
        "**/build": true,
        "**/bindings/**/build": true,
        "**/bindings/**/.gradle": true,
        "**/bindings/**/prebuilds": true,
        "**/bindings/**/runtimes": true,
        "**/bindings/cpp/native": true,
        "**/bindings/java/src/main/resources/native": true,
        "**/bindings/python/src/zlink/native": true,
        "**/bindings/python/src/zlink.egg-info": true
    },
    "files.associations": {
        "*.ipp": "cpp",
        "*.tpp": "cpp"
    }
}
EOF
)

    write_json "$outfile" "$content"
}

generate_c_cpp_properties_json() {
    local outfile="$VSCODE_DIR/c_cpp_properties.json"

    local content
    content=$(cat << EOF
{
    "version": 4,
    "configurations": [
        {
            "name": "$CPP_CONFIG_NAME",
            "compileCommands": "\${workspaceFolder}/compile_commands.json",
            "includePath": [
                "\${workspaceFolder}/core/include",
                "\${workspaceFolder}/bindings/cpp/include",
                "\${workspaceFolder}/bindings/node/native/src",
                "\${workspaceFolder}/core/external/boost"
            ],
            "browse": {
                "path": [
                    "\${workspaceFolder}/core/include",
                    "\${workspaceFolder}/core/src",
                    "\${workspaceFolder}/bindings/cpp/include",
                    "\${workspaceFolder}/bindings/node/native/src"
                ],
                "limitSymbolsToIncludedHeaders": false
            },
            "intelliSenseMode": "$INTELLISENSE_MODE",
            "cStandard": "c11",
            "cppStandard": "c++17"
        }
    ]
}
EOF
)

    write_json "$outfile" "$content"
}

generate_extensions_json() {
    local outfile="$VSCODE_DIR/extensions.json"

    local content
    content=$(cat << 'EOF'
{
    "recommendations": [
        "ms-vscode.cpptools",
        "ms-vscode.cmake-tools",
        "vscjava.vscode-java-pack",
        "ms-dotnettools.csharp",
        "ms-python.python",
        "ms-python.vscode-pylance",
        "dbaeumer.vscode-eslint"
    ]
}
EOF
)

    write_json "$outfile" "$content"
}

generate_tasks_json() {
    local outfile="$VSCODE_DIR/tasks.json"

    # Build Java test command
    local java_task=""
    if [ -n "$DETECTED_JAVA_HOME" ]; then
        local java_cmd
        if [ "$HAS_GRADLEW" = true ]; then
            java_cmd="export JAVA_HOME=$DETECTED_JAVA_HOME && export PATH=\$JAVA_HOME/bin:\$PATH && ./bindings/java/gradlew -p bindings/java test --no-daemon"
        elif command -v gradle &>/dev/null; then
            java_cmd="export JAVA_HOME=$DETECTED_JAVA_HOME && export PATH=\$JAVA_HOME/bin:\$PATH && gradle -p bindings/java test --no-daemon"
        fi

        if [ -n "${java_cmd:-}" ]; then
            java_task=",
        {
            \"label\": \"bindings: test java\",
            \"type\": \"shell\",
            \"command\": \"$java_cmd\",
            \"options\": {
                \"cwd\": \"\${workspaceFolder}\"
            },
            \"group\": \"test\",
            \"problemMatcher\": []
        }"
        fi
    fi

    # Build .NET test command
    local dotnet_task=""
    if [ -n "$DETECTED_DOTNET_ROOT" ]; then
        local dotnet_cmd="dotnet test bindings/dotnet/tests/Zlink.Tests/Zlink.Tests.csproj -v minimal"
        # Add DOTNET_ROOT export if not in standard /usr/bin
        if [ "$DETECTED_DOTNET_ROOT" != "/usr/bin" ]; then
            dotnet_cmd="export DOTNET_ROOT=$DETECTED_DOTNET_ROOT && export PATH=\$DOTNET_ROOT:\$PATH && $dotnet_cmd"
        fi

        dotnet_task=",
        {
            \"label\": \"bindings: test dotnet\",
            \"type\": \"shell\",
            \"command\": \"$dotnet_cmd\",
            \"options\": {
                \"cwd\": \"\${workspaceFolder}\"
            },
            \"group\": \"test\",
            \"problemMatcher\": []
        }"
    fi

    local content
    content=$(cat << EOF
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "bindings: test node",
            "type": "shell",
            "command": "npm test",
            "options": {
                "cwd": "\${workspaceFolder}/bindings/node"
            },
            "group": "test",
            "problemMatcher": []
        },
        {
            "label": "bindings: test python",
            "type": "shell",
            "command": "python3 -m venv .venv-bindings && . .venv-bindings/bin/activate && pip install -e bindings/python && python -m unittest discover -s bindings/python/tests -v",
            "options": {
                "cwd": "\${workspaceFolder}"
            },
            "group": "test",
            "problemMatcher": []
        }${java_task}${dotnet_task},
        {
            "label": "bindings: test cpp",
            "type": "shell",
            "command": "./bindings/cpp/build.sh ON OFF && ctest --test-dir bindings/cpp/build --output-on-failure -R test_cpp_",
            "options": {
                "cwd": "\${workspaceFolder}"
            },
            "group": "test",
            "problemMatcher": []
        }
    ]
}
EOF
)

    write_json "$outfile" "$content"
}

install_recommended_extensions() {
    if [ "$DRY_RUN" = true ]; then
        info "Dry-run mode: skipping extension installation"
        return
    fi

    if [ "$INSTALL_EXTENSIONS" = false ]; then
        info "Skipping extension installation (--skip-extensions)"
        return
    fi

    if [ -z "$VSCODE_CLI" ]; then
        return
    fi

    local ext_file="$VSCODE_DIR/extensions.json"
    if [ ! -f "$ext_file" ]; then
        warn "extensions.json not found: $ext_file"
        return
    fi

    header "Installing VS Code Extensions"

    local ext
    mapfile -t extensions < <(
        python3 -c "import json,sys; data=json.load(open(sys.argv[1])); print('\n'.join(data.get('recommendations', [])))" \
            "$ext_file" 2>/dev/null || true
    )

    if [ ${#extensions[@]} -eq 0 ]; then
        warn "No extension recommendations found in $ext_file"
        return
    fi

    for ext in "${extensions[@]}"; do
        if "$VSCODE_CLI" --list-extensions 2>/dev/null | grep -Fxq "$ext"; then
            info "Already installed: $ext"
            continue
        fi

        if "$VSCODE_CLI" --install-extension "$ext" >/dev/null 2>&1; then
            success "Installed: $ext"
        else
            warn "Failed to install: $ext"
        fi
    done
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
main() {
    detect_platform

    header "VS Code Configuration Generator"
    info "Platform : $BUILDENV_DISTRO $BUILDENV_DISTRO_VERSION ($BUILDENV_ARCH)"
    info "Project  : $PROJECT_ROOT"

    VSCODE_DIR="$PROJECT_ROOT/.vscode"

    # --- Detect tools ---
    header "Detecting Tools"
    detect_gpp_path
    detect_intellisense_mode
    detect_vcpkg_triplet
    detect_java_home
    detect_gradle_home
    detect_dotnet_root
    detect_vscode_cli

    # --- Check for existing files ---
    if [ -d "$VSCODE_DIR" ] && [ "$FORCE_OVERWRITE" = false ] && [ "$DRY_RUN" = false ]; then
        local existing_files=()
        for f in settings.json c_cpp_properties.json extensions.json tasks.json; do
            [ -f "$VSCODE_DIR/$f" ] && existing_files+=("$f")
        done
        if [ ${#existing_files[@]} -gt 0 ]; then
            warn "Existing files: ${existing_files[*]}"
            if [ -t 0 ]; then
                read -rp "Overwrite? [y/N] " response
                case "$response" in
                    [yY]*) info "Overwriting..." ;;
                    *)     info "Aborted."; exit 0 ;;
                esac
            else
                error "Existing files found. Use --force to overwrite."
                exit 1
            fi
        fi
    fi

    mkdir -p "$VSCODE_DIR"

    # --- Generate files ---
    header "Generating VS Code Configuration"
    generate_settings_json
    generate_c_cpp_properties_json
    generate_extensions_json
    generate_tasks_json
    install_recommended_extensions

    # --- Summary ---
    if [ "$DRY_RUN" = false ]; then
        header "Done"
        info "Reload VS Code window to apply changes."
    fi
}

main "$@"
