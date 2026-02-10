param(
    [string]$Pattern = "ALL",
    [string]$BuildDir = "",
    [string]$OutputFile = "",
    [int]$Runs = 1,
    [switch]$WithBaseline,
    [switch]$WithLibzlink,
    [switch]$SkipLibzlink,
    [switch]$CurrentOnly,
    [switch]$ReuseBuild,
    [Alias("Baseline", "SaveResults")]
    [switch]$Result = $true,
    [Alias("BaselineDir")]
    [string]$ResultsDir = "",
    [Alias("BaselineTag")]
    [string]$ResultsTag = "",
    [string]$IoThreads = "",
    [string]$MsgSizes = "",
    [string]$Size = "",
    [Alias("Transport")]
    [string]$Transports = "",
    [switch]$PinCpu,
    [switch]$Help
)

function Show-Usage {
    Write-Host @"
Usage: core\bench\benchwithzlink\run_benchmarks.ps1 [options]

Compare baseline zlink (previous version) vs current zlink (new build).
Note: PATTERN=ALL includes STREAM by default.

Before running:
  1. Copy previous version library to core\bench\benchwithzlink\baseline\zlink_dist\<platform>-<arch>\
     - Windows: libzlink.dll + libzlink.lib

IMPORTANT: Use PowerShell parameter syntax with '-', not bash '--' syntax.

Options:
  -Help                Show this help.
  -WithBaseline        Run baseline and refresh cache (default: use cache).
  -Pattern NAME        Benchmark pattern (e.g., PAIR, PUBSUB, DEALER_DEALER, ALL) [default: ALL].
  -BuildDir PATH       Build directory (default: core/build/windows-x64).
  -OutputFile PATH     Tee results to a file.
  -Result              Write results under core\bench\benchwithzlink\results\YYYYMMDD\.
  -ResultsDir PATH     Override results root directory.
  -ResultsTag NAME     Optional tag appended to the results filename.
  -Runs N              Iterations per configuration (default: 1).
  -CurrentOnly         Run only current zlink benchmarks (no baseline).
  -ReuseBuild          Reuse existing build dir without re-running CMake.
  -IoThreads N         Set BENCH_IO_THREADS for the benchmark run.
  -MsgSizes LIST       Comma-separated message sizes (e.g., 1024 or 64,1024,65536).
  -Size N              Convenience alias for -MsgSizes N.
  -Transports LIST     Comma-separated transports (e.g., tcp,tls,ws,wss).
  -PinCpu              Pin CPU core during benchmarks (Linux taskset).

Examples:
  .\core\bench\benchwithzlink\run_benchmarks.ps1
  .\core\bench\benchwithzlink\run_benchmarks.ps1 -Pattern PAIR
  .\core\bench\benchwithzlink\run_benchmarks.ps1 -Pattern DEALER_DEALER -Runs 5
  .\core\bench\benchwithzlink\run_benchmarks.ps1 -Pattern ALL -Result
  .\core\bench\benchwithzlink\run_benchmarks.ps1 -Runs 10 -Result -ResultsTag "v0.5.0-vs-v0.4.0"
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

# Determine script and repo root directories
$ScriptDir = $PSScriptRoot
$RootDir = $null
$ProbeDir = $ScriptDir
while ($ProbeDir) {
    if (Test-Path (Join-Path $ProbeDir ".git")) {
        $RootDir = $ProbeDir
        break
    }
    $Parent = Split-Path $ProbeDir -Parent
    if ($Parent -eq $ProbeDir) {
        break
    }
    $ProbeDir = $Parent
}
if (-not $RootDir) {
    # Fallback: assume repo root is two levels above script dir
    $RootDir = Split-Path (Split-Path $ScriptDir -Parent) -Parent
}

# Set default build directory
if (-not $BuildDir) {
    $BuildDir = Join-Path $RootDir "core\build\windows-x64"
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
$BaselineRoot = Join-Path $ScriptDir "baseline\zlink_dist\windows-x64"
$BaselineLibDir = Join-Path $BaselineRoot "lib"
$BaselineBinDir = Join-Path $BaselineRoot "bin"
if (-not $CurrentOnly -and ($WithBaseline -or $WithLibzlink)) {
    if (-not (Test-Path $BaselineRoot)) {
        Write-Error "Error: baseline directory not found: $BaselineRoot"
        Write-Error "Please create core\bench\benchwithzlink\\baseline\\zlink_dist\\windows-x64 and copy previous zlink library there."
        exit 1
    }
    if (-not (Test-Path $BaselineLibDir) -or -not (Test-Path $BaselineBinDir)) {
        Write-Error "Error: baseline bin/lib directories not found under $BaselineRoot"
        exit 1
    }
    $BaselineDllFiles = Get-ChildItem -Path $BaselineBinDir -Filter "libzlink*.dll" -ErrorAction SilentlyContinue
    $BaselineLibFiles = Get-ChildItem -Path $BaselineLibDir -Filter "libzlink*.lib" -ErrorAction SilentlyContinue
    if (-not $BaselineDllFiles -or -not $BaselineLibFiles) {
        Write-Error "Error: No baseline libzlink dll/lib found in $BaselineRoot"
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

    # Set dependency paths (OpenSSL via vcpkg; Boost headers via vcpkg or bundled)
    $VcpkgRoot = $env:VCPKG_INSTALLATION_ROOT
    $CoreVcpkgRoot = Join-Path $RootDir "core\deps\vcpkg"
    $LegacyVcpkgRoot = Join-Path $RootDir "deps\vcpkg"
    if (Test-Path $CoreVcpkgRoot) {
        $VcpkgRoot = $CoreVcpkgRoot
    } elseif (-not $VcpkgRoot -and (Test-Path $LegacyVcpkgRoot)) {
        $VcpkgRoot = $LegacyVcpkgRoot
    }
    if (-not $VcpkgRoot) {
        Write-Error "Error: vcpkg root not found. Expected core\\deps\\vcpkg or deps\\vcpkg, or set VCPKG_INSTALLATION_ROOT."
        exit 1
    }
    $VcpkgInstalled = Join-Path $VcpkgRoot "installed\x64-windows-static"
    $VcpkgInclude = Join-Path $VcpkgInstalled "include"
    $BundledBoost = Join-Path $RootDir "core\external\boost"

    $BoostIncludeDir = $null
    if ((Test-Path (Join-Path $VcpkgInclude "boost\asio.hpp")) -and
        (Test-Path (Join-Path $VcpkgInclude "boost\beast.hpp"))) {
        $BoostIncludeDir = $VcpkgInclude
    } elseif ((Test-Path (Join-Path $BundledBoost "boost\asio.hpp")) -and
              (Test-Path (Join-Path $BundledBoost "boost\beast.hpp"))) {
        $BoostIncludeDir = $BundledBoost
    }

    if (-not $BoostIncludeDir) {
        Write-Error "Error: Boost headers not found. Checked $VcpkgInclude and $BundledBoost."
        exit 1
    }

    $CMakeArgs = @(
        "-S", "$RootDir",
        "-B", "$BuildDir",
        "-G", "$CMakeGenerator",
        "-A", "$CMakeArch",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_PREFIX_PATH=$VcpkgInstalled",
        "-DBUILD_BENCHMARKS=ON",
        "-DBUILD_TESTS=OFF",
        "-DZLINK_CXX_STANDARD=17"
    )
    $CMakeArgs += "-DZLINK_BOOST_INCLUDE_DIR=$BoostIncludeDir"

    if ($CurrentOnly) {
        $CMakeArgs += "-DZLINK_BUILD_BENCH_ZMQ=OFF"
    }

    cmake @CMakeArgs

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
function Resolve-PythonExecutable {
    if ($env:PYTHON -and (Test-Path $env:PYTHON)) {
        return $env:PYTHON
    }

    foreach ($name in @("python", "python3")) {
        $cmd = Get-Command $name -ErrorAction SilentlyContinue
        if (-not $cmd -or -not $cmd.Source -or -not (Test-Path $cmd.Source)) {
            continue
        }
        if ($cmd.Source -like "*\WindowsApps\*") {
            continue
        }
        try {
            & $cmd.Source --version *> $null
            if ($LASTEXITCODE -eq 0) {
                return $cmd.Source
            }
        } catch {
            continue
        }
    }

    $pyLauncher = Get-Command py -ErrorAction SilentlyContinue
    if ($pyLauncher -and $pyLauncher.Source -and (Test-Path $pyLauncher.Source)) {
        try {
            $resolved = & $pyLauncher.Source -3 -c "import sys; print(sys.executable)" 2>$null
            $resolved = ($resolved | Select-Object -First 1).Trim()
            if ($resolved -and (Test-Path $resolved)) {
                return $resolved
            }
        } catch {
        }
    }

    return $null
}

$PythonExe = Resolve-PythonExecutable
if (-not $PythonExe) {
    Write-Error "Python not found. Install Python 3 or ensure it is on PATH."
    exit 1
}

Write-Host "Using Python: $PythonExe"

# Build run command
$RunScript = Join-Path $ScriptDir "run_comparison.py"
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
if ($PinCpu) {
    $RunEnv["BENCH_TASKSET"] = "1"
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
        $CacheDir = Join-Path $RootDir "core\bench\benchwithzlink"
        $CacheFile = Join-Path $CacheDir "baseline_cache_windows-x64.json"
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

    & $PythonExe $RunScript @RunArgs | Tee-Object -FilePath $OutputFile

    # Clean up environment variables
    foreach ($key in $RunEnv.Keys) {
        Remove-Item -Path "env:$key" -ErrorAction SilentlyContinue
    }
} else {
    # Set environment variables and run
    foreach ($key in $RunEnv.Keys) {
        Set-Item -Path "env:$key" -Value $RunEnv[$key]
    }

    & $PythonExe $RunScript @RunArgs

    # Clean up environment variables
    foreach ($key in $RunEnv.Keys) {
        Remove-Item -Path "env:$key" -ErrorAction SilentlyContinue
    }
}

exit $LASTEXITCODE
