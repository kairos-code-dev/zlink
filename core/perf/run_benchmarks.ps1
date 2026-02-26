param(
    [string]$Pattern = "ALL",
    [string]$BuildDir = "",
    [string]$OutputFile = "",
    [int]$Runs = 1,
    [switch]$Build,
    [switch]$Save,
    [string]$SaveVersion = "",
    [string]$ResultsDir = "",
    [string]$ResultsTag = "",
    [Alias("duration")]
    [string]$Duration = "",
    [string]$IoThreads = "",
    [string]$MsgSizes = "",
    [string]$Transports = "",
    [switch]$PinCpu,
    [ValidateSet("observe", "trend", "gate")]
    [string]$Mode = "observe",
    [string]$BaselineFile = "",
    [int]$RollingN = 10,
    [switch]$Help
)

$ErrorActionPreference = "Stop"

function Show-Usage {
    Write-Host @"
Usage: core\perf\run_benchmarks.ps1 [options]

Measure current zlink single-pattern performance.

Options:
  -Help                        Show this help.
  -Pattern NAME                Pattern list (comma-separated) or ALL.
  -BuildDir PATH               Build directory (default: core\build\windows-x64).
  -Build                       Force clean build (default is reuse-build).
  -OutputFile PATH             Tee console logs to a file.
  -Save                        Save baseline under results\single\baseline\ (complete only, timestamp version).
  -SaveVersion VERSION         Baseline version to use with -Save (e.g., v1.5.0).
  -ResultsDir PATH             Override result root directory.
  -ResultsTag NAME             Optional tag in saved result filename.
  -Runs N                      Iterations per pattern/transport/size (default by mode: observe=1, trend=3, gate=5).
  -Duration N                  Override single duration seconds (default: 5).
  -IoThreads N                 Set PERF_IO_THREADS.
  -MsgSizes LIST               Comma-separated message sizes.
  -Transports LIST             Comma-separated transports.
  -PinCpu                      Enable PERF_TASKSET=1.
  -Mode MODE                   observe|trend|gate (default: observe).
  -BaselineFile PATH           Baseline file for gate mode.
  -RollingN N                  Rolling baseline window for trend mode (default: 10).

Notes:
  - tmp result is always saved under results\single\tmp\.
  - report result save is always enabled.
  - reuse-build is always enabled unless -Build is provided.
  - -OutputFile and report save can be used together.
  - -save-baseline is removed. Use -Save [-SaveVersion VERSION].
"@
}

if ($Help) {
    Show-Usage
    exit 0
}

$UseReuseBuild = -not $Build.IsPresent
$SaveReport = $true

$RollingNExplicit = $PSBoundParameters.ContainsKey("RollingN")
if (-not $RollingNExplicit) {
    $rollingEnv = $env:PERF_ROLLING_N
    if ($rollingEnv) {
        $parsedRolling = 0
        if ([int]::TryParse($rollingEnv, [ref]$parsedRolling) -and $parsedRolling -ge 1) {
            $RollingN = $parsedRolling
        }
    }
}
if ($RollingN -lt 1) {
    throw "RollingN must be >= 1."
}
if ($IoThreads -and $IoThreads -notmatch '^\d+$') {
    throw "IoThreads must be a non-negative integer."
}
if ($Duration -and $Duration -notmatch '^\d+$') {
    throw "Duration must be a positive integer."
}
if ($Duration -and [int]$Duration -lt 1) {
    throw "Duration must be >= 1."
}
if ($MsgSizes -and $MsgSizes -notmatch '^\d+(,\d+)*$') {
    throw "MsgSizes must be a comma-separated list of integers."
}
if ($Transports -and $Transports -notmatch '^[a-z]+(,[a-z]+)*$') {
    throw "Transports must be a comma-separated list of names."
}
if ($SaveVersion) {
    $Save = $true
}
if (-not $ResultsTag) {
    $ResultsTag = $env:PERF_RESULTS_TAG
}
$AllowMulti = ($env:PERF_ALLOW_MULTI -eq "1")
$MultiPatternCount = 0
$SinglePatternCount = 0

$RunsExplicit = $PSBoundParameters.ContainsKey("Runs")
if (-not $RunsExplicit) {
    switch ($Mode) {
        "observe" { $Runs = 1 }
        "trend" { $Runs = 3 }
        "gate" { $Runs = 5 }
    }
}
if ($Runs -lt 1) {
    throw "Runs must be >= 1."
}

$ScriptDir = $PSScriptRoot
$RootDir = $null
$ProbeDir = $ScriptDir
while ($ProbeDir) {
    if (Test-Path (Join-Path $ProbeDir ".git")) {
        $RootDir = $ProbeDir
        break
    }
    $Parent = Split-Path $ProbeDir -Parent
    if ($Parent -eq $ProbeDir) { break }
    $ProbeDir = $Parent
}
if (-not $RootDir) {
    $RootDir = Split-Path (Split-Path $ScriptDir -Parent) -Parent
}
$RootDir = [System.IO.Path]::GetFullPath($RootDir)

if (-not $BuildDir) {
    $BuildDir = Join-Path $RootDir "core\build\windows-x64"
}
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
if (-not $BuildDir.StartsWith($RootDir, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Build directory must be inside repo root: $RootDir"
}

$BenchComparisonScript = if ($AllowMulti) {
    Join-Path $ScriptDir "run_comparison.py"
} else {
    Join-Path $ScriptDir "single\run_comparison.py"
}
$BenchComparisonScript = [System.IO.Path]::GetFullPath($BenchComparisonScript)
if (-not (Test-Path $BenchComparisonScript)) {
    throw "comparison script not found: $BenchComparisonScript"
}

if (-not $Pattern) {
    throw "Pattern name is required."
}

$PatternList = @()
if ($Pattern.Trim().ToUpperInvariant() -eq "ALL") {
    $PatternList = @("ALL")
    $SinglePatternCount = 1
} else {
    $PatternList = $Pattern.Split(",") | ForEach-Object { $_.Trim().ToUpperInvariant() } | Where-Object { $_ -ne "" }
    if ($PatternList.Count -eq 0) {
        throw "Error: no valid pattern specified."
    }
    foreach ($p in $PatternList) {
        if ($p.StartsWith("MULTI_")) {
            $MultiPatternCount += 1
            if (-not $AllowMulti) {
                throw "run_benchmarks.ps1 is single-pattern mode only. Use run_benchmarks_multi.ps1 for MULTI_* patterns."
            }
        } else {
            $SinglePatternCount += 1
        }
    }
    $Pattern = ($PatternList -join ",")
}

if ($MultiPatternCount -gt 0 -and $SinglePatternCount -gt 0) {
    throw "cannot mix single and multi patterns in one run."
}

$NeedResultsDir = $true
if ($NeedResultsDir -and -not $ResultsDir) {
    $ResultsDir = Join-Path $ScriptDir "results"
}
if ($ResultsDir) {
    $ResultsDir = [System.IO.Path]::GetFullPath($ResultsDir)
}

$ResultFile = ""
$Timestamp = (Get-Date).ToString("yyyyMMdd_HHmmss")
$Name = "perf_windows_${Timestamp}"
if ($ResultsTag) {
    $Name = "${Name}_${ResultsTag}"
}
$ResultSuite = if ($MultiPatternCount -gt 0) { "multi" } else { "single" }
$ResultFile = Join-Path (Join-Path (Join-Path $ResultsDir $ResultSuite) "tmp") "${Name}.txt"
if ($OutputFile) {
    $OutputFile = [System.IO.Path]::GetFullPath($OutputFile)
}

if ($ResultFile -and $OutputFile -and ($ResultFile -ieq $OutputFile)) {
    throw "-OutputFile cannot point to the same file as report result output."
}

function Cleanup-OldResultDirs {
    param(
        [string]$RootPath
    )

    if (-not $RootPath -or -not (Test-Path $RootPath)) {
        return
    }

    $RetentionRaw = $env:PERF_RESULTS_RETENTION_DAYS
    if (-not $RetentionRaw) {
        $RetentionRaw = "90"
    }

    $Retention = 0
    if (-not [int]::TryParse($RetentionRaw, [ref]$Retention)) {
        return
    }
    if ($Retention -le 0) {
        return
    }

    $Cutoff = (Get-Date).ToUniversalTime().AddDays(-$Retention)

    Get-ChildItem -Path $RootPath -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^\d{8}$' } |
        ForEach-Object {
            $dirDate = $null
            if ([datetime]::TryParseExact($_.Name, "yyyyMMdd", $null,
                                          [System.Globalization.DateTimeStyles]::AssumeUniversal,
                                          [ref]$dirDate)) {
                if ($dirDate -lt $Cutoff) {
                    Remove-Item -Recurse -Force $_.FullName
                }
            }
        }
}

if ($ResultsDir) {
    Cleanup-OldResultDirs -RootPath $ResultsDir
}

function Resolve-BenchmarkBinDir {
    param([string]$BuildRoot)

    $Candidates = @(
        (Join-Path $BuildRoot "bin\Release"),
        (Join-Path $BuildRoot "bin\Debug"),
        (Join-Path $BuildRoot "bin"),
        $BuildRoot
    )

    foreach ($Candidate in $Candidates) {
        if (Test-Path (Join-Path $Candidate "perf_pair.exe")) {
            return $Candidate
        }
    }
    return $Candidates[0]
}

$NeedConfigureBuild = -not $UseReuseBuild
if ($UseReuseBuild) {
    $BenchBinDir = Resolve-BenchmarkBinDir -BuildRoot $BuildDir
    $HasSingle = Test-Path (Join-Path $BenchBinDir "perf_pair.exe")
    $HasMulti = Test-Path (Join-Path $BenchBinDir "comp_current_multi_dealer_dealer.exe")
    if ((Test-Path $BuildDir) -and ($HasSingle -or $HasMulti)) {
        Write-Host "Reusing build directory: $BuildDir"
    } else {
        Write-Host "Reusable build not found. Configuring/building: $BuildDir"
        $NeedConfigureBuild = $true
        $UseReuseBuild = $false
    }
}

if ($NeedConfigureBuild) {
    Write-Host "Cleaning build directory: $BuildDir"
    if (Test-Path $BuildDir) {
        Remove-Item -Recurse -Force $BuildDir
    }

    $CMakeGenerator = if ($env:CMAKE_GENERATOR) { $env:CMAKE_GENERATOR } else { "Visual Studio 17 2022" }
    $CMakeArch = if ($env:CMAKE_ARCH) { $env:CMAKE_ARCH } else { "x64" }

    Write-Host "Configuring CMake..."

    $CMakeArgs = @(
        "-S", "$RootDir",
        "-B", "$BuildDir",
        "-G", "$CMakeGenerator",
        "-A", "$CMakeArch",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DBUILD_BENCHMARKS=ON",
        "-DZLINK_BUILD_TESTS=OFF",
        "-DZLINK_BUILD_BENCH_ZMQ=OFF",
        "-DZLINK_BUILD_BENCH_ZLINK=ON",
        "-DZLINK_BUILD_BENCH_BEAST=OFF",
        "-DZLINK_BUILD_BENCH_STREAMCOMPARE=OFF",
        "-DZLINK_BUILD_BENCH_ROUTER_COMPARE=OFF",
        "-DZLINK_CXX_STANDARD=17"
    )

    & cmake @CMakeArgs
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configuration failed"
    }

    Write-Host "Building..."
    & cmake --build $BuildDir --config Release
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed"
    }
}

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
    throw "Python not found. Install Python 3 or ensure it is on PATH."
}

Write-Host "Using Python: $PythonExe"

$RunArgs = @($Pattern, "--build-dir", $BuildDir, "--runs", $Runs.ToString())
if ($PinCpu) {
    $RunArgs += "--pin-cpu"
}

$RunArgs += @("--mode", $Mode, "--rolling-n", $RollingN.ToString())
if ($ResultsDir) {
    $RunArgs += @("--results-dir", $ResultsDir)
}
if ($ResultsTag) {
    $RunArgs += @("--results-tag", $ResultsTag)
}
if ($BaselineFile) {
    $RunArgs += @("--baseline-file", $BaselineFile)
}
$RunArgs += @("--result-file", $ResultFile)
if ($SaveReport) {
    $RunArgs += "--result"
}
if ($Save) {
    $RunArgs += "--save"
    if ($SaveVersion) {
        $RunArgs += $SaveVersion
    }
}

$RunEnv = @{}
if (-not $IoThreads) { $IoThreads = $env:PERF_IO_THREADS }
if (-not $MsgSizes) { $MsgSizes = $env:PERF_MSG_SIZES }
if (-not $Transports) { $Transports = $env:PERF_TRANSPORTS }
if (-not $Duration) { $Duration = $env:PERF_SINGLE_DURATION_SECONDS }
if (-not $Duration) { $Duration = "5" }

if ($IoThreads) {
    $RunEnv["PERF_IO_THREADS"] = $IoThreads
}
if ($MsgSizes) {
    $RunEnv["PERF_MSG_SIZES"] = $MsgSizes
}
if ($Transports) {
    $RunEnv["PERF_TRANSPORTS"] = $Transports
}
if ($Duration) {
    if ($Duration -notmatch '^\d+$' -or [int]$Duration -lt 1) {
        throw "Duration must be a positive integer."
    }
    $RunArgs += @("--duration", $Duration)
    $RunEnv["PERF_SINGLE_DURATION_SECONDS"] = $Duration
}
if ($PinCpu) {
    $RunEnv["PERF_TASKSET"] = "1"
}
if ($UseReuseBuild) {
    $RunEnv["PERF_NO_AUTOBUILD"] = "1"
}

Write-Host ""
Write-Host "Running benchmarks..."
Write-Host "Pattern: $Pattern"
Write-Host "Build Directory: $BuildDir"
Write-Host "Runs: $Runs"
if ($Duration) { Write-Host "Duration: $Duration s" }
Write-Host "Mode: $Mode"
Write-Host "Comparison Script: $BenchComparisonScript"
Write-Host ""

foreach ($key in $RunEnv.Keys) {
    Set-Item -Path "env:$key" -Value $RunEnv[$key]
}

try {
    if ($OutputFile) {
        $OutputDir = Split-Path $OutputFile -Parent
        if ($OutputDir -and -not (Test-Path $OutputDir)) {
            New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
        }
        & $PythonExe $BenchComparisonScript @RunArgs | Tee-Object -FilePath $OutputFile
    } else {
        & $PythonExe $BenchComparisonScript @RunArgs
    }
    $ExitCode = $LASTEXITCODE
} finally {
    foreach ($key in $RunEnv.Keys) {
        Remove-Item -Path "env:$key" -ErrorAction SilentlyContinue
    }
}

exit $ExitCode
