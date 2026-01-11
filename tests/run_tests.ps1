# run_tests.ps1 - Test runner for Windows
#
# This script builds and runs libzmq validation tests
#

param(
    [string]$BuildType = "Release",
    [string]$Architecture = "x64"
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptDir
$BuildDir = Join-Path $ScriptDir "build"

# Color output functions
function Write-Info($message) {
    Write-Host $message -ForegroundColor Cyan
}

function Write-Success($message) {
    Write-Host $message -ForegroundColor Green
}

function Write-Warning($message) {
    Write-Host $message -ForegroundColor Yellow
}

function Write-Error-Custom($message) {
    Write-Host $message -ForegroundColor Red
}

Write-Info "=== libzmq Test Suite ===`n"

# Validate architecture
if ($Architecture -notin @("x64", "x86", "arm64")) {
    Write-Error-Custom "[ERROR] Invalid architecture: $Architecture"
    Write-Host "Supported architectures: x64, x86, arm64"
    exit 1
}

# ARM64-specific warning
if ($Architecture -eq "arm64") {
    $CurrentArch = $env:PROCESSOR_ARCHITECTURE
    if ($CurrentArch -ne "ARM64") {
        Write-Warning "=== ARM64 Architecture Warning ==="
        Write-Warning "You are building for ARM64 but running on $CurrentArch"
        Write-Warning "ARM64 tests can only execute on native ARM64 Windows systems"
        Write-Warning "The build will proceed, but tests may fail if not on ARM64 hardware"
        Write-Host ""
    }
}

# Check if dist directory exists
$DistRoot = Join-Path $ProjectRoot "dist"
if (-not (Test-Path $DistRoot)) {
    Write-Error-Custom "[ERROR] dist/ directory not found"
    Write-Host "Please build libzmq first using:"
    Write-Host "  .\build-scripts\windows\build.ps1"
    exit 1
}

# Determine platform directory
$PlatformDir = "windows-$Architecture"
$DistPath = Join-Path $DistRoot $PlatformDir

Write-Info "Platform: $PlatformDir"
Write-Info "Library path: $DistPath"
Write-Host ""

# Check if libzmq exists
$LibZmqDll = Join-Path $DistPath "libzmq.dll"
if (-not (Test-Path $LibZmqDll)) {
    Write-Error-Custom "[ERROR] libzmq.dll not found in $DistPath"
    Write-Host "Please build libzmq first."
    exit 1
}

# Create build directory
Write-Warning "Preparing test build..."
if (Test-Path $BuildDir) {
    Remove-Item -Path $BuildDir -Recurse -Force
}
New-Item -ItemType Directory -Path $BuildDir | Out-Null
Push-Location $BuildDir

try {
    # Configure with CMake
    Write-Warning "Configuring tests..."

    # Determine CMake generator and architecture flag
    $CMakeArch = switch ($Architecture) {
        "x64"   { "x64" }
        "x86"   { "Win32" }
        "arm64" { "ARM64" }
        default { "x64" }
    }

    $CMakeArgs = @(
        "..",
        "-A", $CMakeArch,
        "-DCMAKE_BUILD_TYPE=$BuildType"
    )

    $CMakeProcess = Start-Process -FilePath "cmake" -ArgumentList $CMakeArgs -NoNewWindow -Wait -PassThru
    if ($CMakeProcess.ExitCode -ne 0) {
        Write-Error-Custom "[ERROR] CMake configuration failed"
        exit 1
    }

    # Build tests
    Write-Warning "Building tests..."

    $BuildArgs = @(
        "--build", ".",
        "--config", $BuildType
    )

    $BuildProcess = Start-Process -FilePath "cmake" -ArgumentList $BuildArgs -NoNewWindow -Wait -PassThru
    if ($BuildProcess.ExitCode -ne 0) {
        Write-Error-Custom "[ERROR] Test build failed"
        exit 1
    }

    Write-Success "[OK] Tests built successfully`n"

    # Copy DLLs from dist to test directory (ensure they're available at runtime)
    $TestDir = Join-Path $BuildDir $BuildType

    # Debug: Show test executable dependencies
    Write-Warning "Checking test executable dependencies..."
    $dumpbin = Get-Command dumpbin -ErrorAction SilentlyContinue
    if ($dumpbin) {
        Write-Host ""
        Write-Info "=== test_basic.exe dependencies ==="
        & dumpbin /DEPENDENTS "$TestDir\test_basic.exe" 2>&1 | ForEach-Object { Write-Host $_ }
        Write-Host ""
    }
    Write-Warning "Copying DLLs to test directory..."
    Get-ChildItem -Path $DistPath -Filter "*.dll" | ForEach-Object {
        $TargetPath = Join-Path $TestDir $_.Name
        Copy-Item $_.FullName $TargetPath -Force
        Write-Host "  Copied: $($_.Name)"
    }
    Write-Host ""

    # List files in test directory for debugging
    Write-Warning "Files in test directory:"
    Get-ChildItem -Path $TestDir | ForEach-Object {
        Write-Host "  $($_.Name)"
    }
    Write-Host ""

    # Set library path for runtime
    $env:PATH = "$DistPath;$TestDir;$env:PATH"

    # Locate test executables
    $TestBasic = Join-Path $BuildDir "$BuildType\test_basic.exe"
    $TestCurve = Join-Path $BuildDir "$BuildType\test_curve.exe"

    if (-not (Test-Path $TestBasic)) {
        Write-Error-Custom "[ERROR] test_basic.exe not found at $TestBasic"
        exit 1
    }

    if (-not (Test-Path $TestCurve)) {
        Write-Error-Custom "[ERROR] test_curve.exe not found at $TestCurve"
        exit 1
    }

    # Run tests
    Write-Info "=== Running Tests ===`n"

    $TestsPassed = 0
    $TestsFailed = 0

    # Change to test directory so DLLs are found
    Push-Location $TestDir

    # Test 1: Basic functionality
    Write-Warning "Running test_basic..."
    try {
        $proc = Start-Process -FilePath ".\test_basic.exe" -NoNewWindow -Wait -PassThru
        if ($proc.ExitCode -eq 0) {
            Write-Success "[PASS] test_basic`n"
            $TestsPassed++
        } else {
            Write-Error-Custom "[FAIL] test_basic (exit code: $($proc.ExitCode))`n"
            $TestsFailed++
        }
    } catch {
        Write-Error-Custom "[FAIL] test_basic (exception: $_)`n"
        $TestsFailed++
    }

    # Test 2: CURVE encryption
    Write-Warning "Running test_curve..."
    try {
        $proc = Start-Process -FilePath ".\test_curve.exe" -NoNewWindow -Wait -PassThru
        if ($proc.ExitCode -eq 0) {
            Write-Success "[PASS] test_curve`n"
            $TestsPassed++
        } else {
            Write-Error-Custom "[FAIL] test_curve (exit code: $($proc.ExitCode))`n"
            $TestsFailed++
        }
    } catch {
        Write-Error-Custom "[FAIL] test_curve (exception: $_)`n"
        $TestsFailed++
    }

    Pop-Location

    # Summary
    Write-Info "=== Test Summary ==="
    Write-Host "Tests passed: " -NoNewline
    Write-Success "$TestsPassed"
    Write-Host "Tests failed: " -NoNewline
    Write-Error-Custom "$TestsFailed"
    Write-Host ""

    if ($TestsFailed -eq 0) {
        Write-Success "✓ All tests passed!"
        Write-Host ""
        Write-Info "libzmq validation successful:"
        Write-Host "  ✓ Library loads correctly"
        Write-Host "  ✓ Basic socket operations work"
        Write-Host "  ✓ CURVE encryption is functional"
        Write-Host "  ✓ libsodium is statically linked"
        exit 0
    } else {
        Write-Error-Custom "✗ Some tests failed"
        Write-Host ""
        Write-Warning "Troubleshooting:"
        Write-Host "  1. Verify libzmq was built correctly"
        Write-Host "  2. Check library dependencies:"
        Write-Host "     dumpbin /DEPENDENTS $LibZmqDll"
        Write-Host "  3. Ensure libsodium is statically linked (no external libsodium dependency)"
        Write-Host "  4. Verify Visual C++ Redistributables are installed"
        exit 1
    }
} finally {
    Pop-Location
}
