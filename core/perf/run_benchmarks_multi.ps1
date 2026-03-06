param(
    [string]$Pattern = "ALL",
    [string]$BuildDir = "",
    [string]$OutputFile = "",
    [int]$Runs = 0,
    [switch]$Build,
    [string]$ResultsDir = "",
    [string]$ResultsTag = "",
    [string]$IoThreads = "",
    [string]$MsgSizes = "",
    [string]$Transports = "",
    [switch]$PinCpu,
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
  -Pattern NAME                Pattern list (comma-separated) or ALL. Legacy MULTI_ prefix is optional.
                               Alias: stream/streams => STREAM,STREAM_CALLBACK,STREAM_LEN32BE
  -BuildDir PATH               Build directory.
  -OutputFile PATH             Tee console logs to file.
  -Runs N                      Iterations per configuration (default: 3).
  -Build                       Force clean build (default is reuse-build).
  -ResultsDir PATH             Override result root directory.
  -ResultsTag NAME             Optional tag appended to result filename.
  -IoThreads N                 Set PERF_IO_THREADS.
  -MsgSizes LIST               Comma-separated sizes.
  -Transports LIST             Comma-separated transports.
  -PinCpu                      Enable PERF_TASKSET=1.
  -Warmup N                    Override PERF_WARMUP_SECONDS (default: 2).
  -Duration N                  Override PERF_DURATION_SECONDS.
  -Clients N                   Override PERF_CLIENTS (default: 100, stream=10000).
  -Hwm N                       Override PERF_HWM (default: 100, stream=10 in binary).
  -SendHwm N                   Override PERF_SNDHWM (fallback: -Hwm).
  -RecvHwm N                   Override PERF_RCVHWM (fallback: -Hwm).
  -SendTimeoutMs N             Override PERF_SNDTIMEO_MS.
  -RecvTimeoutMs N             Override PERF_RCVTIMEO_MS.
  -ConnectConcurrency N        Override PERF_CONNECT_CONCURRENCY.
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
if ($IoThreads -and $IoThreads -notmatch '^\d+$') { throw "IoThreads must be a non-negative integer." }
if ($Hwm -and ($Hwm -notmatch '^\d+$' -or [int]$Hwm -lt 1)) { throw "Hwm must be a positive integer." }
if ($SendHwm -and ($SendHwm -notmatch '^\d+$' -or [int]$SendHwm -lt 1)) { throw "SendHwm must be a positive integer." }
if ($RecvHwm -and ($RecvHwm -notmatch '^\d+$' -or [int]$RecvHwm -lt 1)) { throw "RecvHwm must be a positive integer." }
if ($MsgSizes -and $MsgSizes -notmatch '^\d+(,\d+)*$') { throw "MsgSizes must be a comma-separated list of integers." }
if ($Transports -and $Transports -notmatch '^[a-z]+(,[a-z]+)*$') { throw "Transports must be a comma-separated list of names." }
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
    if ($normalized.StartsWith("MULTI_")) {
        $normalized = $normalized.Substring(6)
    }
    if ($normalized -eq "ROUTER_ROUTER_POLL") {
        throw "ROUTER_ROUTER_POLL is removed from multi benchmarks."
    }
    $PatternList += $normalized
}
if ($PatternList.Count -eq 0) {
    throw "No valid pattern specified."
}
$PatternCsv = ($PatternList -join ",")

if ($Runs -lt 0) { throw "Runs must be >= 0." }
if ($Runs -eq 0) {
    $Runs = 3
}

$ScriptDir = $PSScriptRoot
$Runner = Join-Path $ScriptDir "run_benchmarks.ps1"
if (-not (Test-Path $Runner)) {
    throw "runner script not found: $Runner"
}

$RunArgs = @("-Pattern", $PatternCsv, "-Runs", $Runs.ToString())
if ($BuildDir) { $RunArgs += @("-BuildDir", $BuildDir) }
if ($OutputFile) { $RunArgs += @("-OutputFile", $OutputFile) }
if ($ResultsDir) { $RunArgs += @("-ResultsDir", $ResultsDir) }
if ($ResultsTag) { $RunArgs += @("-ResultsTag", $ResultsTag) }
if ($IoThreads) { $RunArgs += @("-IoThreads", $IoThreads) }
if ($MsgSizes) { $RunArgs += @("-MsgSizes", $MsgSizes) }
if ($Transports) { $RunArgs += @("-Transports", $Transports) }
if ($PinCpu) { $RunArgs += "-PinCpu" }
if ($Build.IsPresent) { $RunArgs += "-Build" }

$RunEnv = @{}
$RunEnv["PERF_ALLOW_MULTI"] = "1"
$RunEnv["PERF_POLICY"] = "1"
$RunEnv["PERF_WARMUP_SECONDS"] = $Warmup.ToString()
$RunEnv["PERF_DURATION_SECONDS"] = $Duration.ToString()
$RunEnv["PERF_SNDTIMEO_MS"] = $SendTimeoutMs
$RunEnv["PERF_RCVTIMEO_MS"] = $RecvTimeoutMs
$RunEnv["PERF_TRANSPORT_TRANSITION_MS"] = $TransportTransitionMs.ToString()
$RunEnv["PERF_PATTERN_TRANSITION_MS"] = $PatternTransitionMs.ToString()
$RunEnv["PERF_SERVER_READY_TIMEOUT_MS"] = $ServerReadyTimeoutMs.ToString()
$RunEnv["PERF_CONNECT_READY_TIMEOUT_MS"] = $ConnectReadyTimeoutMs.ToString()
$RunEnv["PERF_MONITOR_HWM"] = $MonitorHwm.ToString()
$RunEnv["PERF_SERVER_SHUTDOWN_TIMEOUT_MS"] = $ServerShutdownTimeoutMs.ToString()
$RunEnv["PERF_SERVER_BIND_PORT"] = $ServerBindPort.ToString()
if ($Clients) { $RunEnv["PERF_CLIENTS"] = $Clients }
if ($Hwm) { $RunEnv["PERF_HWM"] = $Hwm }
if ($SendHwm) { $RunEnv["PERF_SNDHWM"] = $SendHwm }
if ($RecvHwm) { $RunEnv["PERF_RCVHWM"] = $RecvHwm }
if ($ConnectConcurrency) { $RunEnv["PERF_CONNECT_CONCURRENCY"] = $ConnectConcurrency }

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
