param(
    [ValidateSet("x64", "arm64")]
    [string]$Architecture = "x64",
    [string]$BuildType = "Release"
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $ScriptDir)

if ($Architecture -eq "arm64") {
    $CMAKE_ARCH = "ARM64"
    $OutputDir = "dist\windows-arm64"
} else {
    $CMAKE_ARCH = "x64"
    $OutputDir = "dist\windows-x64"
}

$BuildDir = Join-Path $ProjectRoot "build\windows-$Architecture"

Write-Host ""
Write-Host "==================================="
Write-Host "Windows Build Configuration"
Write-Host "==================================="
Write-Host "Architecture:      $Architecture"
Write-Host "Build type:        $BuildType"
Write-Host "Output directory:  $OutputDir"
Write-Host "CMake platform:    $CMAKE_ARCH"
Write-Host "==================================="
Write-Host ""

# Clean and create directories
if (Test-Path $BuildDir) {
    Remove-Item -Recurse -Force $BuildDir
}
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $ProjectRoot $OutputDir) | Out-Null

Push-Location $BuildDir

try {
    # Configure with CMake
    Write-Host "Configuring with CMake..."
    cmake $ProjectRoot `
        -G "Visual Studio 17 2022" `
        -A $CMAKE_ARCH `
        -DCMAKE_BUILD_TYPE=$BuildType `
        -DCMAKE_POLICY_VERSION_MINIMUM="3.5" `
        -DBUILD_SHARED=ON `
        -DBUILD_STATIC=OFF `
        -DBUILD_TESTS=ON `
        -DWITH_DOCS=OFF

    if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }

    # Build
    Write-Host "Building..."
    cmake --build . --config $BuildType --parallel

    if ($LASTEXITCODE -ne 0) { throw "Build failed" }

    # Copy artifacts
    Write-Host "Copying artifacts..."
    $OutputDirAbs = Join-Path $ProjectRoot $OutputDir

    # Find and copy DLL
    $DllFiles = Get-ChildItem -Path "bin\$BuildType" -Filter "*.dll" -ErrorAction SilentlyContinue
    if ($DllFiles.Count -eq 0) {
        $DllFiles = Get-ChildItem -Path "lib\$BuildType" -Filter "*.dll" -ErrorAction SilentlyContinue
    }

    if ($DllFiles.Count -gt 0) {
        foreach ($dll in $DllFiles) {
            Copy-Item $dll.FullName $OutputDirAbs -Force
            Write-Host "Copied: $($dll.Name)"
        }
        # Also copy as libzmq.dll for convenience
        Copy-Item $DllFiles[0].FullName (Join-Path $OutputDirAbs "libzmq.dll") -Force
    } else {
        throw "DLL not found!"
    }

    # Find and copy LIB
    $LibFiles = Get-ChildItem -Path "lib\$BuildType" -Filter "*.lib" -ErrorAction SilentlyContinue
    if ($LibFiles.Count -gt 0) {
        foreach ($lib in $LibFiles) {
            Copy-Item $lib.FullName $OutputDirAbs -Force
            Write-Host "Copied: $($lib.Name)"
        }
        Copy-Item $LibFiles[0].FullName (Join-Path $OutputDirAbs "libzmq.lib") -Force
    }

    # Copy headers
    Copy-Item (Join-Path $ProjectRoot "include\zmq.h") $OutputDirAbs -Force
    Copy-Item (Join-Path $ProjectRoot "include\zmq_utils.h") $OutputDirAbs -Force

    Write-Host ""
    Write-Host "==================================="
    Write-Host "Build completed successfully!"
    Write-Host "Output: $OutputDir"
    Write-Host "==================================="
    Get-ChildItem $OutputDirAbs
}
finally {
    Pop-Location
}
