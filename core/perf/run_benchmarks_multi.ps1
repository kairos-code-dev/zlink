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
    [int]$Warmup = 2,
    [Alias("MultiDurationSeconds")]
    [int]$Duration = 5,
    [Alias("MultiClients")]
    [string]$Clients = "",
    [Alias("MultiHwm")]
    [string]$Hwm = "",
    [Alias("MultiSndHwm")]
    [string]$SendHwm = "",
    [Alias("MultiRcvHwm")]
    [string]$RecvHwm = "",
    [Alias("MultiSndtimeoMs")]
    [string]$SendTimeoutMs = "200",
    [Alias("MultiRcvtimeoMs")]
    [string]$RecvTimeoutMs = "200",
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
    [int]$MonitorHwm = 1000,
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
  -Pattern NAME                Pattern list (comma-separated) or ALL. MULTI_ prefix is optional.
                               Alias: stream/streams => STREAM,STREAM_CALLBACK,STREAM_LEN32BE
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
  -Warmup N                    Override PERF_MULTI_WARMUP_SECONDS (default: 2).
  -Duration N                  Override PERF_MULTI_DURATION_SECONDS.
  -Clients N                   Override PERF_MULTI_CLIENTS (default: 100, stream=10000).
  -Hwm N                       Override PERF_MULTI_HWM (default: 100, stream=10 in binary).
  -SendHwm N                   Override PERF_MULTI_SNDHWM (fallback: -Hwm).
  -RecvHwm N                   Override PERF_MULTI_RCVHWM (fallback: -Hwm).
  -SendTimeoutMs N             Override PERF_MULTI_SNDTIMEO_MS.
  -RecvTimeoutMs N             Override PERF_MULTI_RCVTIMEO_MS.
  -ConnectConcurrency N        Override PERF_MULTI_CONNECT_CONCURRENCY.
  -DrainMs N                   Override PERF_MULTI_DRAIN_MS.
  -TransportTransitionMs N     Transport transition cooldown(ms).
  -PatternTransitionMs N       Pattern transition cooldown(ms).
  -ServerReadyTimeoutMs N      Server READY wait timeout(ms).
  -ConnectReadyTimeoutMs N     Client/server connect-ready wait timeout(ms).
  -MonitorHwm N                Monitor socket HWM (default: 1000).
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
if ($Hwm -and ($Hwm -notmatch '^\d+$' -or [int]$Hwm -lt 1)) { throw "Hwm must be a positive integer." }
if ($SendHwm -and ($SendHwm -notmatch '^\d+$' -or [int]$SendHwm -lt 1)) { throw "SendHwm must be a positive integer." }
if ($RecvHwm -and ($RecvHwm -notmatch '^\d+$' -or [int]$RecvHwm -lt 1)) { throw "RecvHwm must be a positive integer." }
if ($MsgSizes -and $MsgSizes -notmatch '^\d+(,\d+)*$') { throw "MsgSizes must be a comma-separated list of integers." }
if ($Transports -and $Transports -notmatch '^[a-z]+(,[a-z]+)*$') { throw "Transports must be a comma-separated list of names." }
if ($SaveVersion) { $Save = $true }
$DefaultPatterns = @(
    "DEALER_DEALER",
    "DEALER_ROUTER",
    "ROUTER_ROUTER",
    "PUBSUB",
    "GATEWAY",
    "SPOT",
    "STREAM",
    "STREAM_CALLBACK",
    "STREAM_LEN32BE"
)

function Add-UniquePattern {
    param(
        [System.Collections.Generic.List[string]]$List,
        [string]$PatternName
    )
    if ([string]::IsNullOrWhiteSpace($PatternName)) { return }
    if (-not $List.Contains($PatternName)) {
        $List.Add($PatternName)
    }
}

function Expand-AndAddPatternAlias {
    param(
        [System.Collections.Generic.List[string]]$List,
        [string]$RawPattern
    )
    $p = $RawPattern.Trim().ToUpperInvariant()
    if (-not $p) { return }
    if ($p.StartsWith("MULTI_")) {
        $p = $p.Substring(6)
    }

    switch ($p) {
        "STREAM" {
            Add-UniquePattern -List $List -PatternName "STREAM"
            Add-UniquePattern -List $List -PatternName "STREAM_CALLBACK"
            Add-UniquePattern -List $List -PatternName "STREAM_LEN32BE"
            break
        }
        "STREAMS" {
            Add-UniquePattern -List $List -PatternName "STREAM"
            Add-UniquePattern -List $List -PatternName "STREAM_CALLBACK"
            Add-UniquePattern -List $List -PatternName "STREAM_LEN32BE"
            break
        }
        default {
            Add-UniquePattern -List $List -PatternName $p
            break
        }
    }
}

$ExpandedPatterns = New-Object 'System.Collections.Generic.List[string]'
if ($Pattern.Trim().ToUpperInvariant() -eq "ALL") {
    foreach ($p in $DefaultPatterns) {
        Expand-AndAddPatternAlias -List $ExpandedPatterns -RawPattern $p
    }
} else {
    foreach ($part in $Pattern.Split(",")) {
        Expand-AndAddPatternAlias -List $ExpandedPatterns -RawPattern $part
    }
}

$PatternList = @()
foreach ($p in $ExpandedPatterns) {
    $normalized = $p
    if (-not $normalized.StartsWith("MULTI_")) {
        $normalized = "MULTI_$normalized"
    }
    if ($normalized -eq "MULTI_ROUTER_ROUTER_POLL") {
        throw "ROUTER_ROUTER_POLL is removed from multi benchmarks."
    }
    $PatternList += $normalized
}
if ($PatternList.Count -eq 0) {
    throw "No valid pattern specified."
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
if ($SendHwm) { $RunEnv["PERF_MULTI_SNDHWM"] = $SendHwm }
if ($RecvHwm) { $RunEnv["PERF_MULTI_RCVHWM"] = $RecvHwm }
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
