param(
    [string]$Pattern = "ALL",
    [string]$BuildDir = "",
    [string]$OutputFile = "",
    [int]$Runs = 3,
    [switch]$WithBaseline,
    [switch]$WithLibzlink,
    [switch]$SkipLibzlink,
    [switch]$CurrentOnly,
    [switch]$ReuseBuild,
    [Alias("Baseline", "SaveResults")]
    [switch]$Result,
    [Alias("BaselineDir")]
    [string]$ResultsDir = "",
    [Alias("BaselineTag")]
    [string]$ResultsTag = "",
    [string]$IoThreads = "",
    [string]$MsgSizes = "",
    [string]$Size = "",
    [Alias("Transport")]
    [string]$Transports = "",
    [switch]$Help
)

function Show-Usage {
    Write-Host @"
Usage: benchwithzlink\run_benchmarks.ps1 [options]

Compare baseline zlink (previous version) vs current zlink (new build).
Note: PATTERN=ALL includes STREAM by default.

Before running:
  1. Copy previous version library to benchwithzlink\baseline\lib\
     - Windows: libzlink.dll + libzlink.lib

IMPORTANT: Use PowerShell parameter syntax with '-', not bash '--' syntax.

Options:
  -Help                Show this help.
  -WithBaseline        Run baseline and refresh cache (default: use cache).
  -Pattern NAME        Benchmark pattern (e.g., PAIR, PUBSUB, DEALER_DEALER, ALL) [default: ALL].
  -BuildDir PATH       Build directory (default: build/windows-x64).
  -OutputFile PATH     Tee results to a file.
  -Result              Write results under benchwithzlink\results\YYYYMMDD\.
  -ResultsDir PATH     Override results root directory.
  -ResultsTag NAME     Optional tag appended to the results filename.
  -Runs N              Iterations per configuration (default: 3).
  -CurrentOnly         Run only current zlink benchmarks (no baseline).
  -ReuseBuild          Reuse existing build dir without re-running CMake.
  -IoThreads N         Set BENCH_IO_THREADS for the benchmark run.
  -MsgSizes LIST       Comma-separated message sizes (e.g., 1024 or 64,1024,65536).
  -Size N              Convenience alias for -MsgSizes N.
  -Transports LIST     Comma-separated transports (e.g., tcp,tls,ws,wss).

Examples:
  .\benchwithzlink\run_benchmarks.ps1
  .\benchwithzlink\run_benchmarks.ps1 -Pattern PAIR
  .\benchwithzlink\run_benchmarks.ps1 -Pattern DEALER_DEALER -Runs 5
  .\benchwithzlink\run_benchmarks.ps1 -Pattern ALL -Result
  .\benchwithzlink\run_benchmarks.ps1 -Runs 10 -Result -ResultsTag "v0.5.0-vs-v0.4.0"
"@
}

if ($Help) {
    Show-Usage
    exit 0
}

# Check for common mistakes (bash-style parameters)
if ($Pattern -like "--*") {
    Write-Error "Invalid parameter syntax detected: '$Pattern'"
    Write-Host ""
    Write-Host "PowerShell uses '-Parameter' syntax, not '--parameter' (bash style)." -ForegroundColor Yellow
    Write-Host "Example: Use '-Runs 10' instead of '--runs 10'" -ForegroundColor Yellow
    Write-Host ""
    Show-Usage
    exit 1
}

# Validate parameters
if ($Runs -lt 1) {
    Write-Error "Runs must be a positive integer."
    Show-Usage
    exit 1
}

if ($IoThreads -and $IoThreads -notmatch '^\d+$') {
    Write-Error "IoThreads must be a positive integer."
    Show-Usage
    exit 1
}

# Handle -Size as alias for -MsgSizes
if ($Size) {
    $MsgSizes = $Size
}

if ($MsgSizes -and $MsgSizes -notmatch '^\d+(,\d+)*$') {
    Write-Error "MsgSizes must be a comma-separated list of integers."
    Show-Usage
    exit 1
}

if ($Transports -and $Transports -notmatch '^[a-z]+(,[a-z]+)*$') {
    Write-Error "Transports must be a comma-separated list of names."
    Show-Usage
    exit 1
}

# Results save logic
if ($Result) {
    if ($OutputFile) {
        Write-Error "Error: -Result cannot be used with -OutputFile."
        exit 1
    }
    if (-not $ResultsDir) {
        $ResultsDir = Join-Path $PSScriptRoot "results"
    }
    $DateDir = Get-Date -Format "yyyyMMdd"
    $Timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $Name = "bench_windows_${Pattern}_${Timestamp}"
    if ($ResultsTag) {
        $Name = "${Name}_${ResultsTag}"
    }
    $OutputFile = Join-Path (Join-Path $ResultsDir $DateDir) "${Name}.txt"
}

# Determine script and root directories
$ScriptDir = $PSScriptRoot
$RootDir = Split-Path $ScriptDir -Parent

# Set default build directory
if (-not $BuildDir) {
    $BuildDir = Join-Path $RootDir "build\windows-x64"
}

# Convert to absolute paths
try {
    $BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
} catch {
    # Path doesn't exist yet, which is okay for build dir
}
$RootDir = [System.IO.Path]::GetFullPath($RootDir)

# Validate build directory is inside repo
if (-not $BuildDir.StartsWith($RootDir)) {
    Write-Error "Build directory must be inside repo root: $RootDir"
    exit 1
}

# Check baseline library exists when baseline run is requested
$BaselineLibDir = Join-Path $ScriptDir "baseline\lib"
if (-not $CurrentOnly -and ($WithBaseline -or $WithLibzlink)) {
    if (-not (Test-Path $BaselineLibDir)) {
        Write-Error "Error: baseline lib directory not found: $BaselineLibDir"
        Write-Error "Please create benchwithzlink\\baseline\\lib and copy previous zlink library there."
        exit 1
    }
    $BaselineLibFiles = Get-ChildItem -Path $BaselineLibDir -Filter "libzlink.*" -ErrorAction SilentlyContinue
    if (-not $BaselineLibFiles) {
        Write-Error "Error: No libzlink library found in $BaselineLibDir"
        Write-Error "Please copy previous zlink library (libzlink.dll + libzlink.lib) there."
        exit 1
    }
}

# Build or reuse
if ($ReuseBuild) {
    Write-Host "Reusing build directory: $BuildDir"
    if (-not (Test-Path $BuildDir)) {
        Write-Error "Error: build directory $BuildDir does not exist"
        exit 1
    }
} else {
    Write-Host "Cleaning build directory: $BuildDir"
    if (Test-Path $BuildDir) {
        Remove-Item -Recurse -Force $BuildDir
    }

    $CMakeGenerator = $env:CMAKE_GENERATOR
    if (-not $CMakeGenerator) {
        $CMakeGenerator = "Visual Studio 17 2022"
    }

    $CMakeArch = $env:CMAKE_ARCH
    if (-not $CMakeArch) {
        $CMakeArch = "x64"
    }

    Write-Host "Configuring CMake..."

    # Set vcpkg paths for OpenSSL and Boost dependencies
    $VcpkgInstalled = Join-Path $RootDir "deps\vcpkg\installed\x64-windows-static"
    $VcpkgInclude = Join-Path $VcpkgInstalled "include"

    cmake -S "$RootDir" -B "$BuildDir" `
        -G "$CMakeGenerator" `
        -A "$CMakeArch" `
        "-DCMAKE_BUILD_TYPE=Release" `
        "-DCMAKE_PREFIX_PATH=$VcpkgInstalled" `
        "-DZLINK_BOOST_INCLUDE_DIR=$VcpkgInclude" `
        "-DBUILD_BENCHMARKS=ON" `
        "-DBUILD_TESTS=OFF" `
        "-DZLINK_CXX_STANDARD=20"

    if ($LASTEXITCODE -ne 0) {
        Write-Error "CMake configuration failed"
        exit 1
    }

    Write-Host "Building..."
    cmake --build $BuildDir --config Release

    if ($LASTEXITCODE -ne 0) {
        Write-Error "Build failed"
        exit 1
    }
}

# Find Python
$PythonCmd = $null
$PythonCommands = @("py -3", "python", "python3")
foreach ($cmd in $PythonCommands) {
    $cmdParts = $cmd -split ' '
    try {
        $output = & $cmdParts[0] $cmdParts[1..($cmdParts.Length-1)] --version 2>&1
        if ($LASTEXITCODE -eq 0) {
            $PythonCmd = $cmd
            break
        }
    } catch {
        continue
    }
}

if (-not $PythonCmd) {
    Write-Error "Python not found. Install Python 3 or ensure it is on PATH."
    exit 1
}

Write-Host "Using Python: $PythonCmd"

# Build run command
$RunScript = Join-Path (Join-Path $RootDir "benchwithzlink") "run_comparison.py"
$RunArgs = @($Pattern, "--build-dir", $BuildDir, "--runs", $Runs.ToString())

# Environment variables
$RunEnv = @{}
if ($IoThreads) {
    $RunEnv["BENCH_IO_THREADS"] = $IoThreads
}
if ($MsgSizes) {
    $RunEnv["BENCH_MSG_SIZES"] = $MsgSizes
}
if ($Transports) {
    $RunEnv["BENCH_TRANSPORTS"] = $Transports
}

# Add flags
if ($CurrentOnly) {
    $RunArgs += "--current-only"
} else {
    $WithBaselineFlag = $WithBaseline -or $WithLibzlink
    if ($SkipLibzlink) {
        $WithBaselineFlag = $false
    }

    if ($WithBaselineFlag) {
        $RunArgs += "--refresh-libzlink"
    } else {
        $CacheFile = Join-Path $RootDir "benchwithzlink" "baseline_cache.json"
        if (-not (Test-Path $CacheFile)) {
            Write-Error "Baseline cache not found: $CacheFile"
            Write-Error "Run with -WithBaseline once to generate the baseline."
            exit 1
        }
    }
}

# Execute
Write-Host ""
Write-Host "Running benchmarks..."
Write-Host "Pattern: $Pattern"
Write-Host "Build Directory: $BuildDir"
Write-Host "Runs: $Runs"
Write-Host ""

if ($OutputFile -and $OutputFile -ne "") {
    $OutputDir = Split-Path $OutputFile -Parent
    if ($OutputDir -and $OutputDir -ne "" -and -not (Test-Path $OutputDir)) {
        New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
    }

    # Set environment variables and run with tee
    foreach ($key in $RunEnv.Keys) {
        Set-Item -Path "env:$key" -Value $RunEnv[$key]
    }

    $PythonCmdParts = $PythonCmd -split ' '
    & $PythonCmdParts[0] $PythonCmdParts[1..($PythonCmdParts.Length-1)] $RunScript @RunArgs | Tee-Object -FilePath $OutputFile

    # Clean up environment variables
    foreach ($key in $RunEnv.Keys) {
        Remove-Item -Path "env:$key" -ErrorAction SilentlyContinue
    }
} else {
    # Set environment variables and run
    foreach ($key in $RunEnv.Keys) {
        Set-Item -Path "env:$key" -Value $RunEnv[$key]
    }

    $PythonCmdParts = $PythonCmd -split ' '
    & $PythonCmdParts[0] $PythonCmdParts[1..($PythonCmdParts.Length-1)] $RunScript @RunArgs

    # Clean up environment variables
    foreach ($key in $RunEnv.Keys) {
        Remove-Item -Path "env:$key" -ErrorAction SilentlyContinue
    }
}

exit $LASTEXITCODE
