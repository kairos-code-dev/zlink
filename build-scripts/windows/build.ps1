param(
    [string]$Architecture = "x64"
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $ScriptDir)

switch ($Architecture.ToLower()) {
    "x64" { $ArchName = "x64" }
    "x86_64" { $ArchName = "x64" }
    "arm64" { $ArchName = "arm64" }
    "aarch64" { $ArchName = "arm64" }
    default {
        Write-Error "Unknown architecture: $Architecture"
        exit 1
    }
}

$BuildDir = Join-Path $ProjectRoot "build-$ArchName"
$DistDir = Join-Path $ProjectRoot "dist\windows-$ArchName"

Write-Host "=== Building zlink for Windows $ArchName ===" -ForegroundColor Green

# Clean and create build directory
if (Test-Path $BuildDir) {
    Remove-Item -Recurse -Force $BuildDir
}
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null

Push-Location $BuildDir

try {
    # Configure
    $cmakeArgs = @(
        $ProjectRoot,
        "-G", "Visual Studio 17 2022",
        "-A", $(if ($ArchName -eq "arm64") { "ARM64" } else { "x64" }),
        "-DCMAKE_BUILD_TYPE=Release",
        "-DBUILD_TESTS=ON",
        "-DWITH_DOCS=OFF",
        "-DBUILD_SHARED=ON",
        "-DBUILD_STATIC=ON"
    )

    Write-Host "Configuring CMake..."
    cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }

    # Build
    Write-Host "Building..."
    cmake --build . --config Release --parallel
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }

    # Copy artifacts
    $libDir = Join-Path $BuildDir "lib\Release"
    $binDir = Join-Path $BuildDir "bin\Release"

    if (Test-Path "$binDir\libzmq*.dll") {
        Copy-Item "$binDir\libzmq*.dll" $DistDir -Force
    }
    if (Test-Path "$libDir\libzmq*.lib") {
        Copy-Item "$libDir\libzmq*.lib" $DistDir -Force
    }
    if (Test-Path "$libDir\libzmq*.dll") {
        Copy-Item "$libDir\libzmq*.dll" $DistDir -Force
    }

    Copy-Item (Join-Path $ProjectRoot "include\zmq.h") $DistDir -Force
    Copy-Item (Join-Path $ProjectRoot "include\zmq_utils.h") $DistDir -Force

    Write-Host "=== Build complete ===" -ForegroundColor Green
    Write-Host "Artifacts in: $DistDir"
    Get-ChildItem $DistDir
}
finally {
    Pop-Location
}
