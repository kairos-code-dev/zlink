param(
    [string]$Pattern = "ALL",
    [string]$BuildDir = "",
    [string]$OutputFile = "",
    [int]$Runs = 0,
    [switch]$Build,
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
    [Alias("MultiWarmupSeconds")]
    [int]$Warmup = 3,
    [Alias("MultiDurationSeconds")]
    [int]$Duration = 5,
    [Alias("MultiClients")]
    [string]$Clients = "",
    [Alias("MultiHwm")]
    [string]$Hwm = "",
    [Alias("MultiSndtimeoMs")]
    [string]$SendTimeoutMs = "5000",
    [Alias("MultiRcvtimeoMs")]
    [string]$RecvTimeoutMs = "5000",
    [Alias("MultiConnectConcurrency")]
    [string]$ConnectConcurrency = "",
    [Alias("MultiDrainMs")]
    [string]$DrainMs = "",
    [Alias("MultiTransportTransitionMs")]
    [int]$TransportTransitionMs = 3000,
    [Alias("MultiPatternTransitionMs")]
    [int]$PatternTransitionMs = 3000,
    [Alias("MultiServerReadyTimeoutMs")]
    [int]$ServerReadyTimeoutMs = 10000,
    [Alias("MultiConnectReadyTimeoutMs")]
    [int]$ConnectReadyTimeoutMs = 5000,
    [Alias("MultiMonitorHwm")]
    [int]$MonitorHwm = 200000,
    [Alias("MultiServerShutdownTimeoutMs")]
    [int]$ServerShutdownTimeoutMs = 5000,
    [Alias("MultiServerBindPort")]
    [int]$ServerBindPort = 0,
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
  -Build                       Force clean build (default is reuse-build).
  -Save                        Save baseline file (complete only, timestamp version).
  -SaveVersion VERSION         Baseline version to use with -Save.
  -ResultsDir PATH             Override result root directory.
  -ResultsTag NAME             Optional tag appended to result filename.
  -IoThreads N                 Set PERF_IO_THREADS.
  -MsgSizes LIST               Comma-separated sizes.
  -Transports LIST             Comma-separated transports.
  -PinCpu                      Enable PERF_TASKSET=1.
  -Mode MODE                   observe|trend|gate (default: observe).
  -RollingN N                  Rolling baseline window (default: 10).
  -BaselineFile PATH           Fixed baseline override file for gate mode.
  -WarnThroughputPct N         Throughput warning threshold.
  -FailThroughputPct N         Throughput fail threshold.
  -WarnLatencyPct N            Latency warning threshold.
  -FailLatencyPct N            Latency fail threshold.
  -Warmup N                    Override PERF_MULTI_WARMUP_SECONDS.
  -Duration N                  Override PERF_MULTI_DURATION_SECONDS.
  -Clients N                   Override PERF_MULTI_CLIENTS.
  -Hwm N                       Override PERF_MULTI_HWM (default: pattern-specific in binary).
  -SendTimeoutMs N             Override PERF_MULTI_SNDTIMEO_MS.
  -RecvTimeoutMs N             Override PERF_MULTI_RCVTIMEO_MS.
  -ConnectConcurrency N        Override PERF_MULTI_CONNECT_CONCURRENCY.
  -DrainMs N                   Override PERF_MULTI_DRAIN_MS.
  -TransportTransitionMs N     Transport transition cooldown(ms).
  -PatternTransitionMs N       Pattern transition cooldown(ms).
  -ServerReadyTimeoutMs N      Server READY wait timeout(ms).
  -ConnectReadyTimeoutMs N     Client/server connect-ready wait timeout(ms).
  -MonitorHwm N                Monitor socket HWM (default: 200000).
  -ServerShutdownTimeoutMs N   Server shutdown wait timeout(ms).
  -ServerBindPort N            Server bind port (0=auto).
"@
}

if ($Help) {
    Show-Usage
    exit 0
}

if ($RollingN -lt 1) { throw "RollingN must be >= 1." }
if ($Warmup -lt 0) { throw "Warmup must be >= 0." }
if ($Duration -lt 1) { throw "Duration must be >= 1." }
if ($TransportTransitionMs -lt 0) { throw "TransportTransitionMs must be >= 0." }
if ($PatternTransitionMs -lt 0) { throw "PatternTransitionMs must be >= 0." }
if ($ServerReadyTimeoutMs -lt 0) { throw "ServerReadyTimeoutMs must be >= 0." }
if ($ConnectReadyTimeoutMs -lt 0) { throw "ConnectReadyTimeoutMs must be >= 0." }
if ($MonitorHwm -lt 0) { throw "MonitorHwm must be >= 0." }
if ($ServerShutdownTimeoutMs -lt 0) { throw "ServerShutdownTimeoutMs must be >= 0." }
if ($ServerBindPort -lt 0 -or $ServerBindPort -gt 65535) {
    throw "ServerBindPort must be in range 0..65535."
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

$RunArgs = @("-Pattern", $PatternCsv, "-Mode", $ModeLower, "-RollingN", $RollingN.ToString(), "-Runs", $Runs.ToString())
if ($BuildDir) { $RunArgs += @("-BuildDir", $BuildDir) }
if ($OutputFile) { $RunArgs += @("-OutputFile", $OutputFile) }
if ($ResultsDir) { $RunArgs += @("-ResultsDir", $ResultsDir) }
if ($ResultsTag) { $RunArgs += @("-ResultsTag", $ResultsTag) }
if ($IoThreads) { $RunArgs += @("-IoThreads", $IoThreads) }
if ($MsgSizes) { $RunArgs += @("-MsgSizes", $MsgSizes) }
if ($Transports) { $RunArgs += @("-Transports", $Transports) }
if ($PinCpu) { $RunArgs += "-PinCpu" }
if ($Save) {
    $RunArgs += "-Save"
    if ($SaveVersion) {
        $RunArgs += @("-SaveVersion", $SaveVersion)
    }
}
if ($BaselineFile) { $RunArgs += @("-BaselineFile", $BaselineFile) }
if ($Build.IsPresent) { $RunArgs += "-Build" }

$RunEnv = @{}
$RunEnv["PERF_ALLOW_MULTI"] = "1"
$RunEnv["PERF_MULTI_POLICY"] = "1"
$RunEnv["PERF_MODE"] = $ModeLower
$RunEnv["PERF_ROLLING_N"] = $RollingN.ToString()
$RunEnv["PERF_WARN_THROUGHPUT_PCT"] = $WarnThroughputPct.ToString([System.Globalization.CultureInfo]::InvariantCulture)
$RunEnv["PERF_FAIL_THROUGHPUT_PCT"] = $FailThroughputPct.ToString([System.Globalization.CultureInfo]::InvariantCulture)
$RunEnv["PERF_WARN_LATENCY_PCT"] = $WarnLatencyPct.ToString([System.Globalization.CultureInfo]::InvariantCulture)
$RunEnv["PERF_FAIL_LATENCY_PCT"] = $FailLatencyPct.ToString([System.Globalization.CultureInfo]::InvariantCulture)
$RunEnv["PERF_MULTI_WARMUP_SECONDS"] = $Warmup.ToString()
$RunEnv["PERF_MULTI_DURATION_SECONDS"] = $Duration.ToString()
$RunEnv["PERF_MULTI_SNDTIMEO_MS"] = $SendTimeoutMs
$RunEnv["PERF_MULTI_RCVTIMEO_MS"] = $RecvTimeoutMs
$RunEnv["PERF_MULTI_TRANSPORT_TRANSITION_MS"] = $TransportTransitionMs.ToString()
$RunEnv["PERF_MULTI_PATTERN_TRANSITION_MS"] = $PatternTransitionMs.ToString()
$RunEnv["PERF_MULTI_SERVER_READY_TIMEOUT_MS"] = $ServerReadyTimeoutMs.ToString()
$RunEnv["PERF_MULTI_CONNECT_READY_TIMEOUT_MS"] = $ConnectReadyTimeoutMs.ToString()
$RunEnv["PERF_MULTI_MONITOR_HWM"] = $MonitorHwm.ToString()
$RunEnv["PERF_MULTI_SERVER_SHUTDOWN_TIMEOUT_MS"] = $ServerShutdownTimeoutMs.ToString()
$RunEnv["PERF_MULTI_SERVER_BIND_PORT"] = $ServerBindPort.ToString()
if ($Clients) { $RunEnv["PERF_MULTI_CLIENTS"] = $Clients }
if ($Hwm) { $RunEnv["PERF_MULTI_HWM"] = $Hwm }
if ($ConnectConcurrency) { $RunEnv["PERF_MULTI_CONNECT_CONCURRENCY"] = $ConnectConcurrency }
if ($DrainMs) { $RunEnv["PERF_MULTI_DRAIN_MS"] = $DrainMs }

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
