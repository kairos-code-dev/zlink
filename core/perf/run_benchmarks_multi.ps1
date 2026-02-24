param(
    [string]$Pattern = "ALL",
    [string]$BuildDir = "",
    [string]$OutputFile = "",
    [int]$Runs = 0,
    [switch]$ReuseBuild,
    [switch]$Result,
    [switch]$Save,
    [string]$SaveVersion = "",
    [string]$ResultsDir = "",
    [string]$ResultsTag = "",
    [string]$IoThreads = "",
    [string]$MsgSizes = "",
    [string]$Transports = "",
    [switch]$PinCpu,
    [ValidateSet("observe", "trend", "gate")]
    [string]$Mode = "observe",
    [int]$RollingN = 10,
    [string]$BaselineFile = "",
    [double]$WarnThroughputPct = 10,
    [double]$FailThroughputPct = 15,
    [double]$WarnLatencyPct = 10,
    [double]$FailLatencyPct = 15,
    [int]$MultiWarmupSeconds = 3,
    [int]$MultiDurationSeconds = 5,
    [string]$MultiClients = "",
    [string]$MultiInflight = "",
    [string]$MultiHwm = "",
    [string]$MultiSndtimeoMs = "5000",
    [string]$MultiRcvtimeoMs = "5000",
    [string]$MultiConnectConcurrency = "",
    [string]$MultiDrainMs = "",
    [int]$MultiTransportTransitionMs = 3000,
    [int]$MultiPatternTransitionMs = 3000,
    [int]$MultiServerReadyTimeoutMs = 10000,
    [int]$MultiConnectReadyTimeoutMs = 5000,
    [int]$MultiMonitorHwm = 200000,
    [int]$MultiServerShutdownTimeoutMs = 5000,
    [int]$MultiServerBindPort = 0,
    [switch]$Help
)

$ErrorActionPreference = "Stop"

function Show-Usage {
    Write-Host @"
Usage: core\perf\run_benchmarks_multi.ps1 [options]

Run only multi-socket benchmark patterns.

Options:
  -Pattern NAME                MULTI_* pattern list (comma-separated) or ALL.
  -BuildDir PATH               Build directory.
  -OutputFile PATH             Tee console logs to file.
  -Runs N                      Iterations per configuration (default: observe/trend=3, gate=5).
  -ReuseBuild                  Reuse existing build dir.
  -Result                      Save report result file (complete only).
  -Save                        Save baseline file (complete only, timestamp version).
  -SaveVersion VERSION         Baseline version to use with -Save.
  -ResultsDir PATH             Override result root directory.
  -ResultsTag NAME             Optional tag appended to result filename.
  -IoThreads N                 Set BENCH_IO_THREADS.
  -MsgSizes LIST               Comma-separated sizes.
  -Transports LIST             Comma-separated transports.
  -PinCpu                      Enable BENCH_TASKSET=1.
  -Mode MODE                   observe|trend|gate (default: observe).
  -RollingN N                  Rolling baseline window (default: 10).
  -BaselineFile PATH           Fixed baseline override file for gate mode.
  -WarnThroughputPct N         Throughput warning threshold.
  -FailThroughputPct N         Throughput fail threshold.
  -WarnLatencyPct N            Latency warning threshold.
  -FailLatencyPct N            Latency fail threshold.
  -MultiWarmupSeconds N        Override BENCH_MULTI_WARMUP_SECONDS.
  -MultiDurationSeconds N      Override BENCH_MULTI_DURATION_SECONDS.
  -MultiClients N              Override BENCH_MULTI_CLIENTS.
  -MultiInflight N             Override BENCH_MULTI_INFLIGHT.
  -MultiHwm N                  Override BENCH_MULTI_HWM (default: pattern-specific in binary).
  -MultiSndtimeoMs N           Override BENCH_MULTI_SNDTIMEO_MS.
  -MultiRcvtimeoMs N           Override BENCH_MULTI_RCVTIMEO_MS.
  -MultiConnectConcurrency N   Override BENCH_MULTI_CONNECT_CONCURRENCY.
  -MultiDrainMs N              Override BENCH_MULTI_DRAIN_MS.
  -MultiTransportTransitionMs N  Transport transition cooldown(ms).
  -MultiPatternTransitionMs N    Pattern transition cooldown(ms).
  -MultiServerReadyTimeoutMs N   Server READY wait timeout(ms).
  -MultiConnectReadyTimeoutMs N  Client/server connect-ready wait timeout(ms).
  -MultiMonitorHwm N             Monitor socket HWM (default: 200000).
  -MultiServerShutdownTimeoutMs N  Server shutdown wait timeout(ms).
  -MultiServerBindPort N         Server bind port (0=auto).
"@
}

if ($Help) {
    Show-Usage
    exit 0
}

if ($RollingN -lt 1) { throw "RollingN must be >= 1." }
if ($MultiWarmupSeconds -lt 0) { throw "MultiWarmupSeconds must be >= 0." }
if ($MultiDurationSeconds -lt 1) { throw "MultiDurationSeconds must be >= 1." }
if ($MultiTransportTransitionMs -lt 0) { throw "MultiTransportTransitionMs must be >= 0." }
if ($MultiPatternTransitionMs -lt 0) { throw "MultiPatternTransitionMs must be >= 0." }
if ($MultiServerReadyTimeoutMs -lt 0) { throw "MultiServerReadyTimeoutMs must be >= 0." }
if ($MultiConnectReadyTimeoutMs -lt 0) { throw "MultiConnectReadyTimeoutMs must be >= 0." }
if ($MultiMonitorHwm -lt 0) { throw "MultiMonitorHwm must be >= 0." }
if ($MultiServerShutdownTimeoutMs -lt 0) { throw "MultiServerShutdownTimeoutMs must be >= 0." }
if ($MultiServerBindPort -lt 0 -or $MultiServerBindPort -gt 65535) {
    throw "MultiServerBindPort must be in range 0..65535."
}
if ($WarnThroughputPct -lt 0 -or $FailThroughputPct -lt 0 -or $WarnLatencyPct -lt 0 -or $FailLatencyPct -lt 0) {
    throw "Threshold values must be >= 0."
}
if ($IoThreads -and $IoThreads -notmatch '^\d+$') { throw "IoThreads must be a non-negative integer." }
if ($MsgSizes -and $MsgSizes -notmatch '^\d+(,\d+)*$') { throw "MsgSizes must be a comma-separated list of integers." }
if ($Transports -and $Transports -notmatch '^[a-z]+(,[a-z]+)*$') { throw "Transports must be a comma-separated list of names." }
if ($SaveVersion) { $Save = $true }
$DefaultPatterns = @(
    "MULTI_DEALER_DEALER",
    "MULTI_DEALER_ROUTER",
    "MULTI_ROUTER_ROUTER",
    "MULTI_PUBSUB",
    "MULTI_GATEWAY",
    "MULTI_SPOT",
    "MULTI_STREAM",
    "MULTI_STREAM_CALLBACK",
    "MULTI_STREAM_LEN32BE"
)

$PatternList = @()
if ($Pattern.Trim().ToUpperInvariant() -eq "ALL") {
    $PatternList = $DefaultPatterns
} else {
    foreach ($part in $Pattern.Split(",")) {
        $p = $part.Trim().ToUpperInvariant()
        if (-not $p) { continue }
        if ($p -eq "MULTI_ROUTER_ROUTER_POLL") {
            throw "MULTI_ROUTER_ROUTER_POLL is removed from multi benchmarks."
        }
        if (-not $p.StartsWith("MULTI_")) {
            throw "run_benchmarks_multi.ps1 accepts only MULTI_* patterns."
        }
        $PatternList += $p
    }
}
if ($PatternList.Count -eq 0) {
    throw "No valid MULTI_* pattern specified."
}
$PatternCsv = ($PatternList -join ",")

$ModeLower = $Mode.ToLowerInvariant()
if ($Runs -lt 0) { throw "Runs must be >= 0." }
if ($Runs -eq 0) {
    if ($ModeLower -eq "gate") { $Runs = 5 } else { $Runs = 3 }
}

$ScriptDir = $PSScriptRoot
$Runner = Join-Path $ScriptDir "run_benchmarks.ps1"
if (-not (Test-Path $Runner)) {
    throw "runner script not found: $Runner"
}

$UseReuseBuild = $ReuseBuild.IsPresent
if (-not $ReuseBuild.IsPresent -and $env:BENCH_MULTI_FORCE_CLEAN -ne "1") {
    $UseReuseBuild = $true
}

$RunArgs = @("-Pattern", $PatternCsv, "-Mode", $ModeLower, "-RollingN", $RollingN.ToString(), "-Runs", $Runs.ToString())
if ($BuildDir) { $RunArgs += @("-BuildDir", $BuildDir) }
if ($OutputFile) { $RunArgs += @("-OutputFile", $OutputFile) }
if ($ResultsDir) { $RunArgs += @("-ResultsDir", $ResultsDir) }
if ($ResultsTag) { $RunArgs += @("-ResultsTag", $ResultsTag) }
if ($IoThreads) { $RunArgs += @("-IoThreads", $IoThreads) }
if ($MsgSizes) { $RunArgs += @("-MsgSizes", $MsgSizes) }
if ($Transports) { $RunArgs += @("-Transports", $Transports) }
if ($PinCpu) { $RunArgs += "-PinCpu" }
if ($Result) { $RunArgs += "-Result" }
if ($Save) {
    $RunArgs += "-Save"
    if ($SaveVersion) {
        $RunArgs += @("-SaveVersion", $SaveVersion)
    }
}
if ($BaselineFile) { $RunArgs += @("-BaselineFile", $BaselineFile) }
if ($UseReuseBuild) { $RunArgs += "-ReuseBuild" }

$RunEnv = @{}
$RunEnv["BENCH_ALLOW_MULTI"] = "1"
$RunEnv["BENCH_MULTI_POLICY"] = "1"
$RunEnv["BENCH_MODE"] = $ModeLower
$RunEnv["BENCH_ROLLING_N"] = $RollingN.ToString()
$RunEnv["BENCH_WARN_THROUGHPUT_PCT"] = $WarnThroughputPct.ToString([System.Globalization.CultureInfo]::InvariantCulture)
$RunEnv["BENCH_FAIL_THROUGHPUT_PCT"] = $FailThroughputPct.ToString([System.Globalization.CultureInfo]::InvariantCulture)
$RunEnv["BENCH_WARN_LATENCY_PCT"] = $WarnLatencyPct.ToString([System.Globalization.CultureInfo]::InvariantCulture)
$RunEnv["BENCH_FAIL_LATENCY_PCT"] = $FailLatencyPct.ToString([System.Globalization.CultureInfo]::InvariantCulture)
$RunEnv["BENCH_MULTI_WARMUP_SECONDS"] = $MultiWarmupSeconds.ToString()
$RunEnv["BENCH_MULTI_DURATION_SECONDS"] = $MultiDurationSeconds.ToString()
$RunEnv["BENCH_MULTI_SNDTIMEO_MS"] = $MultiSndtimeoMs
$RunEnv["BENCH_MULTI_RCVTIMEO_MS"] = $MultiRcvtimeoMs
$RunEnv["BENCH_MULTI_TRANSPORT_TRANSITION_MS"] = $MultiTransportTransitionMs.ToString()
$RunEnv["BENCH_MULTI_PATTERN_TRANSITION_MS"] = $MultiPatternTransitionMs.ToString()
$RunEnv["BENCH_MULTI_SERVER_READY_TIMEOUT_MS"] = $MultiServerReadyTimeoutMs.ToString()
$RunEnv["BENCH_MULTI_CONNECT_READY_TIMEOUT_MS"] = $MultiConnectReadyTimeoutMs.ToString()
$RunEnv["BENCH_MULTI_MONITOR_HWM"] = $MultiMonitorHwm.ToString()
$RunEnv["BENCH_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS"] = $MultiServerShutdownTimeoutMs.ToString()
$RunEnv["BENCH_MULTI_SERVER_BIND_PORT"] = $MultiServerBindPort.ToString()
if ($MultiClients) { $RunEnv["BENCH_MULTI_CLIENTS"] = $MultiClients }
if ($MultiInflight) { $RunEnv["BENCH_MULTI_INFLIGHT"] = $MultiInflight }
if ($MultiHwm) { $RunEnv["BENCH_MULTI_HWM"] = $MultiHwm }
if ($MultiConnectConcurrency) { $RunEnv["BENCH_MULTI_CONNECT_CONCURRENCY"] = $MultiConnectConcurrency }
if ($MultiDrainMs) { $RunEnv["BENCH_MULTI_DRAIN_MS"] = $MultiDrainMs }

$PreviousEnv = @{}
foreach ($key in $RunEnv.Keys) {
    $PreviousEnv[$key] = [Environment]::GetEnvironmentVariable($key, "Process")
    [Environment]::SetEnvironmentVariable($key, $RunEnv[$key], "Process")
}

try {
    & $Runner @RunArgs
    $ExitCode = $LASTEXITCODE
} finally {
    foreach ($key in $RunEnv.Keys) {
        [Environment]::SetEnvironmentVariable($key, $PreviousEnv[$key], "Process")
    }
}

exit $ExitCode
